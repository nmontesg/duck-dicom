#include "duckdb.hpp" // IWYU pragma: keep
#include "dcmtk2duckdb_logger.hpp"
#include "dicom_query.hpp"
#include "dicom_utils.hpp"
#include "duckdb_findscu_callback.hpp"
#include "duckdb_tls_options.hpp"
#include "duckdb/common/identifier.hpp"
#include "duckdb/parser/parsed_data/create_table_function_info.hpp"

namespace duckdb {

unique_ptr<FunctionData> QueryDicomFuncBind(ClientContext &context, TableFunctionBindInput &input,
                                            vector<LogicalType> &return_types, vector<Identifier> &names) {
	RedirectDCMTKLogsToDuckDB(context);

	auto result = make_uniq<QueryDicomBindData>();
	bool use_secret = false;
	string secret_name;

	for (const auto &kv : input.named_parameters) {
		if (kv.first == "secret") {
			if (!kv.second.IsNull()) {
				use_secret = true;
				secret_name = StringValue::Get(kv.second);
			}
		} else if (kv.first == "host") {
			if (!kv.second.IsNull()) {
				result->host = StringValue::Get(kv.second);
			}
		} else if (kv.first == "port") {
			if (!kv.second.IsNull()) {
				result->port = UIntegerValue::Get(kv.second);
			}
		} else if (kv.first == "aetitle") {
			result->called_ae_title = StringValue::Get(kv.second);
		} else if (kv.first == "calling_aetitle") {
			result->calling_ae_title = StringValue::Get(kv.second);
		} else if (kv.first == "qr_level") {
			auto input_qr_level = StringValue::Get(kv.second);
			auto it = find(DuckDBDicomUtils::QUERY_RETRIEVE_LEVELS.begin(),
			               DuckDBDicomUtils::QUERY_RETRIEVE_LEVELS.end(), StringUtil::Upper(input_qr_level));
			if (it == DuckDBDicomUtils::QUERY_RETRIEVE_LEVELS.end()) {
				throw InvalidInputException("Unknown Query/Retrieve level " + input_qr_level);
			}
			result->query_retrieve_level = StringUtil::Upper(input_qr_level);
		} else if (kv.first == "acse_timeout") {
			result->acse_timeout = UIntegerValue::Get(kv.second);
		} else if (kv.first == "dimse_timeout") {
			result->dimse_timeout = UIntegerValue::Get(kv.second);
		} else if (kv.first == "max_receive_pdu_length") {
			result->max_receive_pdu_length = UIntegerValue::Get(kv.second);
		} else if (kv.first == "tls_key_file") {
			if (!kv.second.IsNull()) {
				result->tls_private_key_ca_files.first = StringValue::Get(kv.second);
				result->use_tls = true;
			}
		} else if (kv.first == "tls_ca_file") {
			if (!kv.second.IsNull()) {
				result->tls_private_key_ca_files.second = StringValue::Get(kv.second);
				result->use_tls = true;
			}
		} else if (kv.first == "peer_ca_file") {
			if (!kv.second.IsNull()) {
				result->peer_ca_file = StringValue::Get(kv.second);
				result->use_tls = true;
			}
		} else if (kv.first == "match_keys") {
			DuckDBDicomUtils::ParseQueryMatchKeys(kv.second, *result);
		} else if (kv.first == "retrieve_keys") {
			DuckDBDicomUtils::ParseQueryRetrieveKeys(kv.second, *result);
		} else {
			throw InvalidInputException("Unknown query_dicom argument " + kv.first);
		}
	}

	string qr_key = "QueryRetrieveLevel=" + StringUtil::Upper(result->query_retrieve_level);
	result->query.push_back(qr_key.c_str());

	if (use_secret) {
		DuckDBDicomUtils::GetSecretParams<QueryDicomBindData>(context, secret_name, *result);
	}

	if (result->host.empty() || !result->port) {
		throw InvalidInputException("Could not find DICOM peer host and/or port. Please pass a named DICOM secret or "
		                            "specify host and port explicitly.");
	}

	if (result->query.empty()) {
		throw InvalidInputException("Could not process DICOM query. Please provide match or retrieve keys.");
	}

	if (result->use_tls) {
		DuckDBDicomUtils::CheckTlsParams<QueryDicomBindData>(context, *result);
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
		output.SetChildCardinality(0);
		return;
	}

	auto &response_vector = output.data[0];
	auto find_scu = DcmFindSCU();
	auto findscu_callback = DuckDBFindSCUCallback(response_vector);

	string dicom_logtype = "dicom";

	OFCondition cond = find_scu.initializeNetwork(static_cast<int>(bind_data.acse_timeout));
	if (cond.bad()) {
		throw IOException("Could not initialize DICOM network.");
	}

	DuckDBTlsOptions tlsOptions = DuckDBTlsOptions(NET_REQUESTOR, bind_data.tls_private_key_ca_files.first,
	                                               bind_data.tls_private_key_ca_files.second, bind_data.peer_ca_file);
	if (bind_data.use_tls) {
		cond = tlsOptions.createTransportLayer();
		if (cond.bad()) {
			throw IOException("Could not create secure transport layer.");
		}

		cond = find_scu.setTransportLayer(tlsOptions.getTransportLayer());
		if (cond.bad()) {
			throw IOException("Could not set secure transport layer.");
		}
	}
	cond = find_scu.performQuery(
	    bind_data.host.c_str(), bind_data.port, bind_data.calling_ae_title.c_str(), bind_data.called_ae_title.c_str(),
	    bind_data.abstract_syntax.c_str(), bind_data.network_transfer_syntax, bind_data.block_mode,
	    static_cast<int>(bind_data.dimse_timeout), bind_data.max_receive_pdu_length, bind_data.use_tls,
	    // the following only work with the default callback, set to dummy values
	    false,                                              // abort association
	    1,                                                  // repeat count
	    FEM_none,                                           // extract responses,
	    false,                                              // cancel after N responses,
	    const_cast<OFList<OFString> *>(&(bind_data.query)), // NOLINT(cppcoreguidelines-pro-type-const-cast)
	    &findscu_callback, nullptr, nullptr, nullptr, bind_data.protocol_version);
	if (cond.bad()) {
		throw IOException("Error performing C-FIND command.");
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

	output.SetChildCardinality(findscu_callback.GetNumResponses());
	global_state.is_processed = true;
}

void RegisterDicomQuery(ExtensionLoader &loader) {
	FunctionSignature query_dicom_signature;
	// query_dicom_signature.AddPositionalOnly(Identifier("_uids"), LogicalType::VARCHAR);
	query_dicom_signature.AddKeywordOnly(Identifier("secret"), LogicalType::VARCHAR, Value(LogicalType::SQLNULL));
	query_dicom_signature.AddKeywordOnly(Identifier("host"), LogicalType::VARCHAR, Value(LogicalType::SQLNULL));
	query_dicom_signature.AddKeywordOnly(Identifier("port"), LogicalType::UINTEGER, Value(LogicalType::SQLNULL));
	query_dicom_signature.AddKeywordOnly(Identifier("aetitle"), LogicalType::VARCHAR, Value("ANY-SCP"));
	query_dicom_signature.AddKeywordOnly(Identifier("calling_aetitle"), LogicalType::VARCHAR, Value("DUCKDB"));
	query_dicom_signature.AddKeywordOnly(Identifier("qr_level"), LogicalType(LogicalTypeId::VARCHAR), Value("study"));
	query_dicom_signature.AddKeywordOnly(Identifier("acse_timeout"), LogicalType::UINTEGER, Value(30));
	query_dicom_signature.AddKeywordOnly(Identifier("dimse_timeout"), LogicalType::UINTEGER, Value(0));
	query_dicom_signature.AddKeywordOnly(Identifier("max_receive_pdu_length"), LogicalType::UINTEGER,
	                                     Value(ASC_DEFAULTMAXPDU));
	query_dicom_signature.AddKeywordOnly(Identifier("tls_key_file"), LogicalType::VARCHAR, Value(LogicalType::SQLNULL));
	query_dicom_signature.AddKeywordOnly(Identifier("tls_ca_file"), LogicalType::VARCHAR, Value(LogicalType::SQLNULL));
	query_dicom_signature.AddKeywordOnly(Identifier("peer_ca_file"), LogicalType::VARCHAR, Value(LogicalType::SQLNULL));
	query_dicom_signature.AddKeywordOnly(Identifier("match_keys"),
	                                     LogicalType::MAP(LogicalType::VARCHAR, LogicalType::VARCHAR),
	                                     Value::MAP(LogicalType::VARCHAR, LogicalType::VARCHAR, {}, {}));
	query_dicom_signature.AddKeywordOnly(Identifier("retrieve_keys"), LogicalType::LIST(LogicalType::VARCHAR),
	                                     Value::LIST(LogicalType::VARCHAR, {}));

	// query_dicom table function
	TableFunction query_dicom_func("query_dicom", query_dicom_signature, QueryDicomFunc, QueryDicomFuncBind,
	                               QueryDicomGlobalInit);

	CreateTableFunctionInfo query_dicom_info(query_dicom_func);
	FunctionDescription query_dicom_desc;
	query_dicom_desc.description = "Query remote DICOM nodes using C-FIND commands";
	query_dicom_desc.examples = {"FROM query_dicom(host='localhost', port=4242, match_keys=MAP {'Modality': 'MR'}, "
	                             "retrieve_keys=['StudyInstanceUID']);",
	                             "FROM query_dicom(secret='my_dicom_conn_secret', qr_level='series', match_keys=MAP "
	                             "{'SeriesDate': '20070101'});"};
	query_dicom_desc.categories = {"medical"};
	query_dicom_info.descriptions.push_back(query_dicom_desc);

	loader.RegisterFunction(query_dicom_info);
}

} // namespace duckdb
