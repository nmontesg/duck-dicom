#include "dcmtk/dcmtls/tlsopt.h"
#include "dcmtk2duckdb_logger.hpp"
#include "dicom_extension.hpp"
#include "dicom_query.hpp"
#include "dicom_types.hpp"
#include "duckdb_findscu_callback.hpp"
#include "duckdb_tls_options.hpp"
#include "duckdb.hpp"
#include "duckdb/catalog/catalog_transaction.hpp"
#include "duckdb/main/secret/secret_manager.hpp"
#include "duckdb/parser/parsed_data/create_table_function_info.hpp"

namespace duckdb {

void ParseMatchKeys(const Value &input_query, QueryDicomBindData &bind_data) {
	if (input_query.IsNull()) {
		return;
	}
	const auto &map_elements = ListValue::GetChildren(input_query);
	for (const auto &element : map_elements) {
		if (element.IsNull()) {
			continue;
		}
		const vector<Value> &struct_children = StructValue::GetChildren(element);

		string key = struct_children[0].ToString();
		string val = struct_children[1].ToString();

		const string overrideKey = key + "=" + val;
		bind_data.query.push_back(overrideKey.c_str());
	}
}

void ParseRetrieveKeys(const Value &input_query, QueryDicomBindData &bind_data) {
	if (input_query.IsNull()) {
		return;
	}
	const auto &map_elements = ListValue::GetChildren(input_query);
	for (const auto &element : map_elements) {
		if (element.IsNull()) {
			continue;
		}
		const string retrieveKey = StringValue::Get(element);
		bind_data.query.push_back(retrieveKey.c_str());
	}
}

unique_ptr<FunctionData> QueryDicomFuncBind(ClientContext &context, TableFunctionBindInput &input,
                                            vector<LogicalType> &return_types, vector<string> &names) {
	RedirectDCMTKLogsToDuckDB(context);

	auto result = make_uniq<QueryDicomBindData>();
	bool use_secret = false;
	string secret_name;

	for (const auto &kv : input.named_parameters) {
		if (kv.first == "secret") {
			use_secret = true;
			secret_name = StringValue::Get(kv.second);
		} else if (kv.first == "host") {
			result->host = StringValue::Get(kv.second);
		} else if (kv.first == "port") {
			result->port = UIntegerValue::Get(kv.second);
		} else if (kv.first == "aetitle") {
			result->calledAETitle = StringValue::Get(kv.second);
		} else if (kv.first == "calling_aetitle") {
			result->callingAETitle = StringValue::Get(kv.second);
		} else if (kv.first == "qr_level") {
			auto input_qr_level = StringValue::Get(kv.second);
			auto it =
			    find(QUERY_RETRIEVE_LEVELS.begin(), QUERY_RETRIEVE_LEVELS.end(), StringUtil::Upper(input_qr_level));
			if (it == QUERY_RETRIEVE_LEVELS.end()) {
				throw InvalidInputException("Unknown Query/Retrieve level " + input_qr_level);
			}
			result->query_retrieve_level = StringUtil::Upper(input_qr_level);
		} else if (kv.first == "acse_timeout") {
			result->acseTimeout = UIntegerValue::Get(kv.second);
		} else if (kv.first == "dimse_timeout") {
			result->dimseTimeout = UIntegerValue::Get(kv.second);
		} else if (kv.first == "max_receive_pdu_length") {
			result->maxReceivePDULength = UIntegerValue::Get(kv.second);
		} else if (kv.first == "tls_key_file") {
			result->tlsPrivateKeyCAFiles.first = StringValue::Get(kv.second);
			result->useTls = true;
		} else if (kv.first == "tls_ca_file") {
			result->tlsPrivateKeyCAFiles.second = StringValue::Get(kv.second);
			result->useTls = true;
		} else if (kv.first == "peer_ca_file") {
			result->peerCAFile = StringValue::Get(kv.second);
			result->useTls = true;
		} else if (kv.first == "match_keys") {
			ParseMatchKeys(kv.second, *result);
		} else if (kv.first == "retrieve_keys") {
			ParseRetrieveKeys(kv.second, *result);
		} else {
			throw InvalidInputException("Unknown query_dicom argument " + kv.first);
		}
	}

	string qr_key = "QueryRetrieveLevel=" + StringUtil::Upper(result->query_retrieve_level);
	result->query.push_back(qr_key.c_str());

	if (use_secret) {
		auto &secret_manager = SecretManager::Get(context);
		auto transaction = CatalogTransaction::GetSystemCatalogTransaction(context);
		auto secret_entry = secret_manager.GetSecretByName(transaction, secret_name);
		if (!secret_entry) {
			throw InvalidInputException("Could not find secret " + secret_name);
		}
		const auto &kv_secret = dynamic_cast<const KeyValueSecret &>(*secret_entry->secret);

		result->host = kv_secret.TryGetValue("host", true).GetValue<string>();
		result->port = kv_secret.TryGetValue("port", true).GetValue<uint>();
		if (!kv_secret.TryGetValue("aetitle").IsNull()) {
			result->calledAETitle = kv_secret.TryGetValue("aetitle", true).GetValue<string>();
		}
		if (!kv_secret.TryGetValue("tls_key_file").IsNull()) {
			result->tlsPrivateKeyCAFiles.first = kv_secret.TryGetValue("tls_key_file", true).GetValue<string>();
			result->useTls = true;
		}
		if (!kv_secret.TryGetValue("tls_ca_file").IsNull()) {
			result->tlsPrivateKeyCAFiles.second = kv_secret.TryGetValue("tls_ca_file", true).GetValue<string>();
			result->useTls = true;
		}
		if (!kv_secret.TryGetValue("peer_ca_file").IsNull()) {
			result->peerCAFile = kv_secret.TryGetValue("peer_ca_file", true).GetValue<string>();
			result->useTls = true;
		}
	}

	if (result->host.empty() || !result->port) {
		throw InvalidInputException("Could not find DICOM peer host and/or port. Please pass a named DICOM secret or "
		                            "specify host and port explicitly.");
	}
	if (result->useTls) {
		if (result->tlsPrivateKeyCAFiles.first.empty()) {
			throw InvalidInputException("TLS setup is missing private key file.");
		}
		if (result->tlsPrivateKeyCAFiles.second.empty()) {
			throw InvalidInputException("TLS setup is missing certificate file.");
		}
		if (result->peerCAFile.empty()) {
			throw InvalidInputException("TlS setup is missing peer certificate.");
		}

		auto &fs = FileSystem::GetFileSystem(context);
		if (!fs.FileExists(result->tlsPrivateKeyCAFiles.first)) {
			throw InvalidInputException("Could not find private key file " + result->tlsPrivateKeyCAFiles.first);
		}
		if (!fs.FileExists(result->tlsPrivateKeyCAFiles.second)) {
			throw InvalidInputException("Could not find certificate file " + result->tlsPrivateKeyCAFiles.second);
		}
		if (!fs.FileExists(result->peerCAFile)) {
			throw InvalidInputException("Could not find peer certificate file " + result->peerCAFile);
		}
	}

	names.push_back("dicom_response");
	return_types.push_back(LogicalType::JSON());

	return std::move(result);
}

unique_ptr<GlobalTableFunctionState> QueryDicomGlobalInit(ClientContext &context, TableFunctionInitInput &input) {
	auto state = make_uniq<QueryDicomGlobalState>();
	return state;
}

void QueryDicomFunc(ClientContext &context, TableFunctionInput &data, DataChunk &output) {
	auto &bind_data = data.bind_data->Cast<QueryDicomBindData>();
	auto &global_state = data.global_state->Cast<QueryDicomGlobalState>();

	if (global_state.is_processed) {
		output.SetCardinality(0);
		return;
	}

	auto &response_vector = output.data[0];
	auto find_scu = DcmFindSCU();
	auto findscu_callback = DuckDBFindSCUCallback(response_vector);

	string dicom_logtype = "dicom";

	OFCondition cond = find_scu.initializeNetwork(bind_data.acseTimeout);
	if (cond.bad()) {
		throw IOException("Could not initialize DICOM network.");
		output.SetCardinality(0);
		global_state.is_processed = true;
		return;
	}

	DuckDBTlsOptions tlsOptions = DuckDBTlsOptions(NET_REQUESTOR, bind_data.tlsPrivateKeyCAFiles.first,
	                                               bind_data.tlsPrivateKeyCAFiles.second, bind_data.peerCAFile);
	if (bind_data.useTls) {
		cond = tlsOptions.createTransportLayer();
		if (cond.bad()) {
			throw IOException("Could not create secure transport layer.");
			output.SetCardinality(0);
			global_state.is_processed = true;
			return;
		}

		cond = find_scu.setTransportLayer(tlsOptions.getTransportLayer());
		if (cond.bad()) {
			throw IOException("Could not set secure transport layer.");
			output.SetCardinality(0);
			global_state.is_processed = true;
			return;
		}
	}
	cond = find_scu.performQuery(bind_data.host.c_str(), bind_data.port, bind_data.callingAETitle.c_str(),
	                             bind_data.calledAETitle.c_str(), bind_data.abstractSyntax.c_str(),
	                             bind_data.networkTransferSyntax, bind_data.blockMode, bind_data.dimseTimeout,
	                             bind_data.maxReceivePDULength, bind_data.useTls,
	                             // the following only work with the default callback, set to dummy values
	                             false,    // abort association,
	                             1,        // repeat count,
	                             FEM_none, // extract responses,
	                             false,    // cancel after N responses,
	                             const_cast<OFList<OFString> *>(&(bind_data.query)), &findscu_callback, NULL, NULL,
	                             NULL, bind_data.protocolVersion);
	if (cond.bad()) {
		throw IOException("Error performing C-FIND command.");
		output.SetCardinality(findscu_callback.GetNumResponses());
		global_state.is_processed = true;
		return;
	}

	cond = find_scu.dropNetwork();
	auto &logger = Logger::Get(context);
	if (cond.bad()) {
		logger.WriteLog(dicom_logtype.c_str(), LogLevel::LOG_ERROR, "Error closing DICOM network.");
	}

	OFStandard::shutdownNetwork();

	cond = tlsOptions.writeRandomSeed();
	if (cond.bad()) {
		logger.WriteLog(dicom_logtype.c_str(), LogLevel::LOG_WARNING, "Could not write back the TLS random seed.");
	}

	output.SetCardinality(findscu_callback.GetNumResponses());
	global_state.is_processed = true;
}

void RegisterDicomQueryFunctions(ExtensionLoader &loader) {
	// query_dicom table function
	TableFunction query_dicom_func("query_dicom", {}, QueryDicomFunc, QueryDicomFuncBind, QueryDicomGlobalInit);
	query_dicom_func.named_parameters["secret"] = LogicalType::VARCHAR;
	query_dicom_func.named_parameters["host"] = LogicalType::VARCHAR;
	query_dicom_func.named_parameters["port"] = LogicalType::UINTEGER;
	query_dicom_func.named_parameters["aetitle"] = LogicalType::VARCHAR;
	query_dicom_func.named_parameters["calling_aetitle"] = LogicalType::VARCHAR;
	query_dicom_func.named_parameters["qr_level"] = LogicalType::VARCHAR;
	query_dicom_func.named_parameters["acse_timeout"] = LogicalType::UINTEGER;
	query_dicom_func.named_parameters["dimse_timeout"] = LogicalType::UINTEGER;
	query_dicom_func.named_parameters["max_receive_pdu_length"] = LogicalType::UINTEGER;
	query_dicom_func.named_parameters["tls_key_file"] = LogicalType::VARCHAR;
	query_dicom_func.named_parameters["tls_ca_file"] = LogicalType::VARCHAR;
	query_dicom_func.named_parameters["peer_ca_file"] = LogicalType::VARCHAR;
	query_dicom_func.named_parameters["match_keys"] = LogicalType::MAP(LogicalType::VARCHAR, LogicalType::VARCHAR);
	query_dicom_func.named_parameters["retrieve_keys"] = LogicalType::LIST(LogicalType::VARCHAR);

	CreateTableFunctionInfo query_dicom_info(query_dicom_func);
	FunctionDescription query_dicom_desc;
	query_dicom_desc.parameter_types = {LogicalType::VARCHAR,
	                                    LogicalType::VARCHAR,
	                                    LogicalType::UINTEGER,
	                                    LogicalType::VARCHAR,
	                                    LogicalType::VARCHAR,
	                                    LogicalType::VARCHAR,
	                                    LogicalType::UINTEGER,
	                                    LogicalType::UINTEGER,
	                                    LogicalType::UINTEGER,
	                                    LogicalType::VARCHAR,
	                                    LogicalType::VARCHAR,
	                                    LogicalType::VARCHAR,
	                                    LogicalType::MAP(LogicalType::VARCHAR, LogicalType::VARCHAR),
	                                    LogicalType::LIST(LogicalType::VARCHAR)};
	query_dicom_desc.parameter_names = {"secret",
	                                    "host",
	                                    "port",
	                                    "aetitle",
	                                    "calling_aetitle",
	                                    "qr_level",
	                                    "acse_timeout",
	                                    "dimse_output",
	                                    "max_receive_pdu_length",
	                                    "tls_key_file",
	                                    "tls_ca_file",
	                                    "peer_ca_file",
	                                    "match_keys",
	                                    "retrieve_keys"};
	query_dicom_desc.description = "Query remote DICOM nodes using C-FIND commands";
	query_dicom_desc.examples = {
	    "FROM query_dicom(host='localhost', port=4242, match_keys={'Modality': 'MR'}, "
	    "retrieve_keys=['StudyInstanceUID']);",
	    "FROM query_dicom(secret='my_dicom_conn_secret', qr_level='series', match_keys={'SeriesDate': '20070101'});"};
	query_dicom_desc.categories = {"medical"};
	query_dicom_info.descriptions.push_back(query_dicom_desc);

	loader.RegisterFunction(query_dicom_func);
}

} // namespace duckdb
