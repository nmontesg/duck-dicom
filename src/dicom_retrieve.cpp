#include "dcmtk2duckdb_logger.hpp"
#include "dcmtk/dcmdata/dcfilefo.h"
#include "dcmtk/dcmdata/dcjson.h"
#include "dcmtk/dcmdata/dcobject.h"
#include "dicom_retrieve.hpp"
#include "dicom_utils.hpp"
#include "duckdb_tls_options.hpp"
#include "duckdb/parser/parsed_data/create_table_function_info.hpp"

namespace duckdb {

unique_ptr<FunctionData> RetrieveDicomFuncSingleBind(ClientContext &context, TableFunctionBindInput &input,
                                                     vector<LogicalType> &return_types, vector<string> &names) {
	auto result = make_uniq<RetrieveDicomBindData>();
	for (auto &val : input.inputs) {
		if (val.IsNull()) {
			throw BinderException("retrieve_dicom: unique_id arguments cannot be NULL");
		}
		result->uids.push_back(val.ToString());
	}
	RetrieveDicomFuncBind(context, input, return_types, names, *result);
	return std::move(result);
}

unique_ptr<FunctionData> RetrieveDicomFuncListBind(ClientContext &context, TableFunctionBindInput &input,
                                                   vector<LogicalType> &return_types, vector<string> &names) {
	auto result = make_uniq<RetrieveDicomBindData>();
	auto &map_elements = ListValue::GetChildren(input.inputs[0]);
	for (auto &element : map_elements) {
		if (element.IsNull()) {
			continue;
		}
		result->uids.push_back(element.ToString());
	}
	RetrieveDicomFuncBind(context, input, return_types, names, *result);
	return std::move(result);
}

void RetrieveDicomFuncBind(ClientContext &context, TableFunctionBindInput &input, vector<LogicalType> &return_types,
                           vector<string> &names, RetrieveDicomBindData &bind_data) {
	RedirectDCMTKLogsToDuckDB(context);

	bool use_secret = false;
	string secret_name;

	for (const auto &kv : input.named_parameters) {
		if (kv.first == "secret") {
			use_secret = true;
			secret_name = StringValue::Get(kv.second);
		} else if (kv.first == "host") {
			bind_data.host = StringValue::Get(kv.second);
		} else if (kv.first == "port") {
			bind_data.port = UIntegerValue::Get(kv.second);
		} else if (kv.first == "incoming_port") {
			bind_data.incoming_port = UIntegerValue::Get(kv.second);
		} else if (kv.first == "aetitle") {
			bind_data.called_ae_title = StringValue::Get(kv.second);
		} else if (kv.first == "calling_aetitle") {
			bind_data.calling_ae_title = StringValue::Get(kv.second);
		} else if (kv.first == "qr_level") {
			auto input_qr_level = StringValue::Get(kv.second);
			auto it = find(DuckDBDicomUtils::QUERY_RETRIEVE_LEVELS.begin(),
			               DuckDBDicomUtils::QUERY_RETRIEVE_LEVELS.end(), StringUtil::Upper(input_qr_level));
			if (it == DuckDBDicomUtils::QUERY_RETRIEVE_LEVELS.end()) {
				throw InvalidInputException("Unknown Query/Retrieve level " + input_qr_level);
			}
			bind_data.query_retrieve_level = StringUtil::Upper(input_qr_level);
		} else if (kv.first == "acse_timeout") {
			bind_data.acse_timeout = UIntegerValue::Get(kv.second);
		} else if (kv.first == "dimse_timeout") {
			bind_data.dimse_timeout = UIntegerValue::Get(kv.second);
		} else if (kv.first == "max_receive_pdu_length") {
			bind_data.max_receive_pdu_length = UIntegerValue::Get(kv.second);
		} else if (kv.first == "tls_key_file") {
			bind_data.tls_private_key_ca_files.first = StringValue::Get(kv.second);
			bind_data.use_tls = true;
		} else if (kv.first == "tls_ca_file") {
			bind_data.tls_private_key_ca_files.second = StringValue::Get(kv.second);
			bind_data.use_tls = true;
		} else if (kv.first == "peer_ca_file") {
			bind_data.peer_ca_file = StringValue::Get(kv.second);
			bind_data.use_tls = true;
		} else if (kv.first == "load_pixel_data") {
			bind_data.read_options.load_pixel_data = BooleanValue::Get(kv.second);
		} else {
			throw InvalidInputException("Unknown retrieve_dicom argument " + kv.first);
		}
	}

	string id_col_name =
	    StringUtil::Lower(bind_data.query_retrieve_level) +
	    (bind_data.query_retrieve_level == DuckDBDicomUtils::QUERY_RETRIEVE_LEVELS[0] ? "_id" : "_instance_uid");

	if (use_secret) {
		DuckDBDicomUtils::GetSecretParams<RetrieveDicomBindData>(context, secret_name, bind_data);
	}

	if (bind_data.host.empty() || !bind_data.port) {
		throw InvalidInputException("Could not find DICOM peer host and/or port. Please pass a named DICOM secret or "
		                            "specify host and port explicitly.");
	}

	if (!bind_data.incoming_port) {
		throw InvalidInputException("Could not find port for incoming DICOM association.");
	}

	if (bind_data.use_tls) {
		DuckDBDicomUtils::CheckTlsParams<RetrieveDicomBindData>(context, bind_data);
	}

	names.push_back(id_col_name);
	return_types.push_back(LogicalType::VARCHAR);

	names.push_back("dicom_response");
	return_types.push_back(LogicalType::JSON());
}

unique_ptr<GlobalTableFunctionState> RetrieveDicomGlobalInit(ClientContext &context, TableFunctionInitInput &input) {
	auto state = make_uniq<QueryDicomGlobalState>();
	return state;
}

void RetrieveDicomFunc(ClientContext &context, TableFunctionInput &data, DataChunk &output) {
	auto &bind_data = data.bind_data->Cast<RetrieveDicomBindData>();
	auto &global_state = data.global_state->Cast<QueryDicomGlobalState>();
	auto &uid_vector = output.data[0];
	auto &response_vector = output.data[1];

	string dicom_logtype = "dicom";
	auto &logger = Logger::Get(context);

	if (global_state.is_processed) {
		output.SetCardinality(0);
		return;
	}

	OFString temp_str;
	std::ostringstream oss;

	T_ASC_Network *net = nullptr;
	T_ASC_Parameters *params = nullptr;
	T_ASC_Association *assoc = nullptr;

	OFStandard::initializeNetwork();

	OFCondition cond = ASC_initializeNetwork(NET_ACCEPTORREQUESTOR, static_cast<int>(bind_data.incoming_port),
	                                         static_cast<int>(bind_data.acse_timeout), &net);
	if (cond.bad()) {
		oss.clear();
		oss << "cannot create network: " << DimseCondition::dump(temp_str, cond);
		logger.WriteLog(dicom_logtype.c_str(), LogLevel::LOG_FATAL, oss.str());
		throw IOException("Could not initialize DICOM network.");
	}

	if (OFStandard::dropPrivileges().bad()) {
		throw IOException("setuid() failed, maximum number of processes/threads for uid already running.");
	}

	cond = ASC_createAssociationParameters(&params, bind_data.max_receive_pdu_length, dcmConnectionTimeout.get());
	if (cond.bad()) {
		oss.clear();
		oss << "cannot create association parameters: " << DimseCondition::dump(temp_str, cond);
		logger.WriteLog(dicom_logtype.c_str(), LogLevel::LOG_FATAL, oss.str());
		throw IOException("Could not create association parameters.");
	}

	ASC_setAPTitles(params, bind_data.calling_ae_title.c_str(), bind_data.called_ae_title.c_str(), nullptr);
	ASC_setProtocolFamily(params, dcmIncomingProtocolFamily.get());
	DIC_NODENAME peerHost;
	OFStandard::snprintf(peerHost, sizeof(peerHost), "%s:%d", bind_data.host.c_str(), bind_data.port);
	ASC_setPresentationAddresses(params, OFStandard::getHostName().c_str(), peerHost);

	DuckDBTlsOptions tlsOptions = DuckDBTlsOptions(NET_ACCEPTORREQUESTOR, bind_data.tls_private_key_ca_files.first,
	                                               bind_data.tls_private_key_ca_files.second, bind_data.peer_ca_file);
	if (bind_data.use_tls) {
		cond = tlsOptions.createTransportLayer();
		if (cond.bad()) {
			throw IOException("Could not create secure transport layer.");
		}
		cond = ASC_setTransportLayer(net, tlsOptions.getTransportLayer(), 0);
		if (cond.bad()) {
			throw IOException("Could not set secure transport layer.");
		}
		cond = ASC_setTransportLayerType(params, bind_data.use_tls);
		if (cond.bad()) {
			throw IOException("Could not set secure transport layer type.");
		}
	}

	const char *transferSyntaxes[] = {UID_LittleEndianImplicitTransferSyntax};
	cond = ASC_addPresentationContext(params, 1, bind_data.abstract_syntax_find.c_str(), transferSyntaxes, 1);
	if (cond.bad()) {
		throw IOException("Could not create association paramters for C-FIND operation.");
	}
	cond = ASC_addPresentationContext(params, 3, bind_data.abstract_syntax_move.c_str(), transferSyntaxes, 1);
	if (cond.bad()) {
		throw IOException("Could not create association paramters for C-MOVE operation.");
	}

	cond = ASC_requestAssociation(net, params, &assoc);
	if (cond.bad()) {
		if (cond == DUL_ASSOCIATIONREJECTED) {
			T_ASC_RejectParameters rej;
			ASC_getRejectParameters(params, &rej);
			ASC_printRejectParameters(temp_str, &rej);
			logger.WriteLog(dicom_logtype.c_str(), LogLevel::LOG_ERROR, string(temp_str.c_str()));
		} else {
			DimseCondition::dump(temp_str, cond);
			logger.WriteLog(dicom_logtype.c_str(), LogLevel::LOG_ERROR, string(temp_str.c_str()));
		}
		throw IOException("Could not request DICOM association: " + string(cond.text()));
	}

	DUL_PRESENTATIONCONTEXTID presentationContextID =
	    ASC_findAcceptedPresentationContextID(assoc, bind_data.abstract_syntax_move.c_str());
	if (presentationContextID == 0) {
		throw IOException("Could not find valid presentation context ID.");
	}

	// populate the request
	T_DIMSE_C_MoveRQ req;
	DIC_US msgId = assoc->nextMsgID++;
	req.MessageID = msgId;
	OFStandard::strlcpy(req.AffectedSOPClassUID, bind_data.abstract_syntax_move.c_str(),
	                    sizeof(req.AffectedSOPClassUID));
	req.Priority = DIMSE_PRIORITY_MEDIUM;
	req.DataSetType = DIMSE_DATASET_PRESENT;

	/* set the destination to be me */
	ASC_getAPTitles(assoc->params, req.MoveDestination, sizeof(req.MoveDestination), nullptr, 0, nullptr, 0);
	logger.WriteLog(dicom_logtype.c_str(), LogLevel::LOG_INFO, "Sending move request to peer");
	logger.WriteLog(dicom_logtype.c_str(), LogLevel::LOG_DEBUG,
	                DIMSE_dumpMessage(temp_str, req, DIMSE_OUTGOING, nullptr, presentationContextID).c_str());

	unsigned int total_retrieved_datasets = 0;
	auto uid_data = FlatVector::GetData<string_t>(uid_vector);
	for (auto &unique_id : bind_data.uids) {
		oss.str("");
		oss.clear();
		DcmDataset queryDataset;
		queryDataset.clear();
		DuckDBDicomUtils::ParseDicomDataset(bind_data.query_retrieve_level, unique_id, &queryDataset);
		oss << DcmObject::PrintHelper(queryDataset);
		logger.WriteLog(dicom_logtype.c_str(), LogLevel::LOG_INFO, "Request identifiers:\n" + oss.str());

		T_DIMSE_C_MoveRSP rsp;

		auto statusDetail = std::make_unique<DcmDataset>();
		auto responseIds = std::make_unique<DcmDataset>();

		DcmDataset *statusDetailPtr = statusDetail.get();
		DcmDataset *responseIdsPtr = responseIds.get();

		RetrieveDicomMoveCallbackData moveCallbackData(logger);
		RetrieveSubOpCallbackData subOpCallbackData(
		    logger, bind_data.use_tls, bind_data.max_receive_pdu_length, bind_data.block_mode, bind_data.dimse_timeout,
		    bind_data.read_options.load_pixel_data, response_vector, total_retrieved_datasets);

		cond = DIMSE_moveUser(assoc, presentationContextID, &req, &queryDataset, moveCallback, &moveCallbackData,
		                      bind_data.block_mode, static_cast<int>(bind_data.dimse_timeout), net, subOpCallback,
		                      &subOpCallbackData, &rsp, &statusDetailPtr, &responseIdsPtr);

		unsigned int start = total_retrieved_datasets;
		unsigned int end = total_retrieved_datasets + subOpCallbackData.num_recv_datasets;
		for (unsigned int i = start; i < end; i++) {
			uid_data[i] = StringVector::AddString(uid_vector, unique_id);
		}

		total_retrieved_datasets += subOpCallbackData.num_recv_datasets;
	}

	if (cond == EC_Normal) {
		/* release association */
		logger.WriteLog(dicom_logtype.c_str(), LogLevel::LOG_INFO, "Releasing association");
		cond = ASC_releaseAssociation(assoc);
		if (cond.bad()) {
			logger.WriteLog(dicom_logtype.c_str(), LogLevel::LOG_FATAL, "Association release failed:");
			logger.WriteLog(dicom_logtype.c_str(), LogLevel::LOG_FATAL, DimseCondition::dump(temp_str, cond).c_str());
			throw IOException("Association release failed");
		}
	} else if (cond == DUL_PEERREQUESTEDRELEASE) {
		logger.WriteLog(dicom_logtype.c_str(), LogLevel::LOG_ERROR,
		                "Protocol Error: Peer requested release (Aborting)");
		logger.WriteLog(dicom_logtype.c_str(), LogLevel::LOG_INFO, "Aborting Association");
		cond = ASC_abortAssociation(assoc);
		if (cond.bad()) {
			oss.clear();
			oss << "Association Abort Failed: " << DimseCondition::dump(temp_str, cond);
			logger.WriteLog(dicom_logtype.c_str(), LogLevel::LOG_FATAL, oss.str());
			throw IOException("Could not abort association");
		}
	} else if (cond == DUL_PEERABORTEDASSOCIATION) {
		logger.WriteLog(dicom_logtype.c_str(), LogLevel::LOG_INFO, "Peer Aborted Association");
	} else {
		std::ostringstream oss;
		oss << "Move SCU Failed: " << DimseCondition::dump(temp_str, cond);
		logger.WriteLog(dicom_logtype.c_str(), LogLevel::LOG_ERROR, oss.str());
		logger.WriteLog(dicom_logtype.c_str(), LogLevel::LOG_INFO, "Aborting Association");
		cond = ASC_abortAssociation(assoc);
		if (cond.bad()) {
			oss.clear();
			oss << "Association Abort Failed: " << DimseCondition::dump(temp_str, cond);
			logger.WriteLog(dicom_logtype.c_str(), LogLevel::LOG_FATAL, oss.str());
			throw IOException("Association Abort Failed");
		}
	}

	cond = ASC_destroyAssociation(&assoc);
	if (cond.bad()) {
		oss.clear();
		oss << DimseCondition::dump(temp_str, cond);
		logger.WriteLog(dicom_logtype.c_str(), LogLevel::LOG_FATAL, oss.str());
		throw IOException("Could not destroy association");
	}
	cond = ASC_dropNetwork(&net);
	if (cond.bad()) {
		oss.clear();
		oss << DimseCondition::dump(temp_str, cond);
		logger.WriteLog(dicom_logtype.c_str(), LogLevel::LOG_FATAL, oss.str());
		throw IOException("Could not drop network");
	}

	OFStandard::shutdownNetwork();

	cond = tlsOptions.writeRandomSeed();
	if (cond.bad()) {
		oss.clear();
		oss << DimseCondition::dump(temp_str, cond);
		logger.WriteLog(dicom_logtype.c_str(), LogLevel::LOG_WARNING, oss.str());
	}

	output.SetCardinality(total_retrieved_datasets);
	global_state.is_processed = true;
}

void moveCallback(void *callback_data, T_DIMSE_C_MoveRQ *request, int response_count, T_DIMSE_C_MoveRSP *response) {
	RetrieveDicomMoveCallbackData *myCallbackData;
	myCallbackData = static_cast<RetrieveDicomMoveCallbackData *>(callback_data);
	myCallbackData->logger.WriteLog("move_callback", LogLevel::LOG_INFO, "Received move response %d", response_count);
}

void subOpCallback(void *sub_op_callback_data, T_ASC_Network *a_net, T_ASC_Association **sub_assoc) {
	if (a_net == nullptr) {
		return;
	}

	RetrieveSubOpCallbackData *callbackData;
	if (sub_op_callback_data) {
		callbackData = static_cast<RetrieveSubOpCallbackData *>(sub_op_callback_data);
	}

	if (*sub_assoc == nullptr) {
		acceptSubAssoc(a_net, sub_assoc, callbackData->max_receive_pdu_length, callbackData->use_tls,
		               callbackData->logger);
	} else {
		subOpSCP(sub_assoc, callbackData->block_mode, callbackData->dimse_timeout, callbackData->logger,
		         callbackData->num_recv_datasets, callbackData->load_pixel_data, callbackData->response_vector,
		         callbackData->offset);
	}
}

OFCondition acceptSubAssoc(T_ASC_Network *aNet, T_ASC_Association **assoc, unsigned int maxReceivePDULength,
                           bool useTls, Logger &logger) {
	const char *transferSyntaxes[] = {UID_LittleEndianImplicitTransferSyntax};
	const char *knownAbstractSyntaxes[] = {UID_VerificationSOPClass};
	int numTransferSyntaxes = 1;
	OFString temp_str;
	OFCondition cond = ASC_receiveAssociation(aNet, assoc, maxReceivePDULength, nullptr, nullptr, useTls);
	if (cond.good()) {
		logger.WriteLog("accept_sub_assoc", LogLevel::LOG_INFO, "Sub-association received");
		std::ostringstream oss;
		oss << ASC_dumpParameters(temp_str, (*assoc)->params, ASC_ASSOC_RQ);
		logger.WriteLog("accept_sub_assoc", LogLevel::LOG_DEBUG, "Parameters:\n" + oss.str());
		cond = ASC_acceptContextsWithPreferredTransferSyntaxes((*assoc)->params, knownAbstractSyntaxes,
		                                                       DIM_OF(knownAbstractSyntaxes), transferSyntaxes,
		                                                       numTransferSyntaxes);
		if (cond.good()) {
			cond = ASC_acceptContextsWithPreferredTransferSyntaxes((*assoc)->params, dcmAllStorageSOPClassUIDs,
			                                                       numberOfDcmAllStorageSOPClassUIDs, transferSyntaxes,
			                                                       numTransferSyntaxes);
		}
	}
	if (cond.good()) {
		cond = ASC_acknowledgeAssociation(*assoc);
	}
	if (cond.good()) {
		logger.WriteLog("accept_sub_assoc", LogLevel::LOG_INFO, "Sub-Association Acknowledged (Max Send PDV: %d)",
		                (*assoc)->sendPDVLength);
		if (ASC_countAcceptedPresentationContexts((*assoc)->params) == 0) {
			logger.WriteLog("accept_sub_assoc", LogLevel::LOG_INFO, "    (but no valid presentation contexts)");
		}
		ASC_dumpParameters(temp_str, (*assoc)->params, ASC_ASSOC_AC);
		logger.WriteLog("accept_sub_assoc", LogLevel::LOG_DEBUG, temp_str.c_str());
	} else {
		logger.WriteLog("accept_sub_assoc", LogLevel::LOG_ERROR, DimseCondition::dump(temp_str, cond).c_str());
		ASC_dropAssociation(*assoc);
		ASC_destroyAssociation(assoc);
	}
	return cond;
}

OFCondition subOpSCP(T_ASC_Association **sub_assoc, T_DIMSE_BlockingMode block_mode, unsigned int dimse_timeout,
                     Logger &logger, unsigned int &num_datasets, bool load_pixel_data, Vector &response_vector,
                     unsigned int offset) {
	T_DIMSE_Message msg;
	T_ASC_PresentationContextID presID;

	if (!ASC_dataWaiting(*sub_assoc, 0)) {
		return DIMSE_NODATAAVAILABLE;
	}

	OFCondition cond =
	    DIMSE_receiveCommand(*sub_assoc, block_mode, static_cast<int>(dimse_timeout), &presID, &msg, nullptr);

	if (cond == EC_Normal) {
		switch (msg.CommandField) {
		case DIMSE_C_STORE_RQ:
			// process C-STORE-Request
			cond = storeSCP(*sub_assoc, &msg, presID, logger, block_mode, dimse_timeout, num_datasets, load_pixel_data,
			                response_vector, offset);
			break;
		default:
			OFString tempStr;
			// we cannot handle this kind of message
			cond = DIMSE_BADCOMMANDTYPE;
			std::ostringstream oss;
			oss << STD_NAMESPACE hex << STD_NAMESPACE setfill('0') << STD_NAMESPACE setw(4)
			    << OFstatic_cast(unsigned, msg.CommandField);
			logger.WriteLog("sub_op_scp", LogLevel::LOG_ERROR,
			                "Expected C-STORE request but received DIMSE command 0x" + oss.str());
			logger.WriteLog("sub_op_scp", LogLevel::LOG_DEBUG,
			                DIMSE_dumpMessage(tempStr, msg, DIMSE_INCOMING, nullptr, presID).c_str());
			break;
		}
	}
	/* clean up on association termination */
	if (cond == DUL_PEERREQUESTEDRELEASE) {
		cond = ASC_acknowledgeRelease(*sub_assoc);
		ASC_dropSCPAssociation(*sub_assoc);
		ASC_destroyAssociation(sub_assoc);
		return cond;
	} else if (cond == DUL_PEERABORTEDASSOCIATION) {
	} else if (cond != EC_Normal) {
		OFString temp_str;
		std::ostringstream oss;
		oss << "DIMSE failure (aborting sub-association): " << DimseCondition::dump(temp_str, cond);
		logger.WriteLog("sub_op_scp", LogLevel::LOG_ERROR, oss.str());
		/* some kind of error so abort the association */
		cond = ASC_abortAssociation(*sub_assoc);
	}

	if (cond != EC_Normal) {
		ASC_dropAssociation(*sub_assoc);
		ASC_destroyAssociation(sub_assoc);
	}
	return cond;
}

OFCondition storeSCP(T_ASC_Association *assoc, T_DIMSE_Message *msg, T_ASC_PresentationContextID presID, Logger &logger,
                     T_DIMSE_BlockingMode blockMode, unsigned int dimseTimeout, unsigned int &num_responses,
                     bool load_pixel_data, Vector &response_vector, unsigned int offset) {
	OFCondition cond = EC_Normal;
	T_DIMSE_C_StoreRQ *req;

	req = &msg->msg.CStoreRQ;

	OFString temp_str;
	logger.WriteLog("store_scp", LogLevel::LOG_INFO, "Received Store Request");
	logger.WriteLog("store_scp", LogLevel::LOG_DEBUG,
	                DIMSE_dumpMessage(temp_str, *req, DIMSE_INCOMING, nullptr, presID).c_str());

	DcmFileFormat dcmff;
	DcmDataset *dset = dcmff.getDataset();

	cond = DIMSE_storeProvider(assoc, presID, req, nullptr, true, &dset, nullptr, nullptr, blockMode,
	                           static_cast<int>(dimseTimeout));

	thread_local std::ostringstream jsonStream;
	jsonStream.str("");
	jsonStream.clear();
	DcmJsonFormatCompact format;
	if (!load_pixel_data) {
		delete dset->remove(DcmTagKey(0x7FE0, 0x0010));
	}
	dset->writeJson(jsonStream, format);

	auto response_data = FlatVector::GetData<string_t>(response_vector);
	response_data[req->MessageID - 1 + offset] = StringVector::AddString(response_vector, "{" + jsonStream.str() + "}");

	num_responses = req->MessageID;

	if (cond.bad()) {
		std::ostringstream oss;
		oss << "Store SCP Failed: " << DimseCondition::dump(temp_str, cond);
		logger.WriteLog("store_scp", LogLevel::LOG_ERROR, oss.str());
	}

	return cond;
}

void RegisterDicomRetrieve(ExtensionLoader &loader) {
	// retrieve_dicom table function
	TableFunction retrieve_dicom_single_func("retrieve_dicom", {}, RetrieveDicomFunc, RetrieveDicomFuncSingleBind,
	                                         RetrieveDicomGlobalInit);
	retrieve_dicom_single_func.varargs = {LogicalType::VARCHAR};

	TableFunction retrieve_dicom_list_func("retrieve_dicom", {LogicalType::LIST(LogicalType::VARCHAR)},
	                                       RetrieveDicomFunc, RetrieveDicomFuncListBind, RetrieveDicomGlobalInit);

	for (auto *fn : {&retrieve_dicom_single_func, &retrieve_dicom_list_func}) {
		fn->named_parameters["secret"] = LogicalType::VARCHAR;
		fn->named_parameters["host"] = LogicalType::VARCHAR;
		fn->named_parameters["port"] = LogicalType::UINTEGER;
		fn->named_parameters["incoming_port"] = LogicalType::UINTEGER;
		fn->named_parameters["aetitle"] = LogicalType::VARCHAR;
		fn->named_parameters["calling_aetitle"] = LogicalType::VARCHAR;
		fn->named_parameters["qr_level"] = LogicalType::VARCHAR;
		fn->named_parameters["acse_timeout"] = LogicalType::UINTEGER;
		fn->named_parameters["dimse_timeout"] = LogicalType::UINTEGER;
		fn->named_parameters["max_receive_pdu_length"] = LogicalType::UINTEGER;
		fn->named_parameters["tls_key_file"] = LogicalType::VARCHAR;
		fn->named_parameters["tls_ca_file"] = LogicalType::VARCHAR;
		fn->named_parameters["peer_ca_file"] = LogicalType::VARCHAR;
		fn->named_parameters["load_pixel_data"] = LogicalType::BOOLEAN;

		CreateTableFunctionInfo retrieve_dicom_info(*fn);
		FunctionDescription retrieve_dicom_desc;
		retrieve_dicom_desc.parameter_types = {LogicalType::VARCHAR,  LogicalType::VARCHAR,  LogicalType::UINTEGER,
		                                       LogicalType::UINTEGER, LogicalType::VARCHAR,  LogicalType::VARCHAR,
		                                       LogicalType::VARCHAR,  LogicalType::UINTEGER, LogicalType::UINTEGER,
		                                       LogicalType::UINTEGER, LogicalType::VARCHAR,  LogicalType::VARCHAR,
		                                       LogicalType::VARCHAR,  LogicalType::BOOLEAN};
		retrieve_dicom_desc.parameter_names = {
		    "secret",          "host",        "port",         "incoming_port", "aetitle",
		    "calling_aetitle", "qr_level",    "acse_timeout", "dimse_output",  "max_receive_pdu_length",
		    "tls_key_file",    "tls_ca_file", "peer_ca_file"};
		retrieve_dicom_desc.description = "Retrieve DICOM data from remote modalities using C-MOVE commands";
		retrieve_dicom_desc.examples = {
		    "FROM retrieve_dicom('1.3.54.24.5...', host='localhost', port=4242, incoming_port=11112, "
		    "qr_level='study');",
		    "FROM retrieve_dicom(['1.95.3...', '1.56.7...'], secret='my_dicom_conn_secret', qr_level='series');"};
		retrieve_dicom_desc.categories = {"medical"};
		retrieve_dicom_info.descriptions.push_back(retrieve_dicom_desc);

		loader.RegisterFunction(*fn);
	}
}

} // namespace duckdb
