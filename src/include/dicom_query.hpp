#pragma once

#include "duckdb.hpp" // IWYU pragma: keep
#include "dcmtk/dcmdata/dcuid.h"
#include "dcmtk/dcmnet/dimse.h"

namespace duckdb {

struct QueryDicomBindData : public TableFunctionData {
	string host;
	unsigned int port;
	string called_ae_title = "ANY-SCP";
	string calling_ae_title = "DUCKDB";
	string abstract_syntax = UID_FINDStudyRootQueryRetrieveInformationModel;
	string query_retrieve_level = "STUDY";
	E_TransferSyntax network_transfer_syntax = EXS_Unknown;
	T_DIMSE_BlockingMode block_mode = DIMSE_BLOCKING;
	unsigned int acse_timeout = 30;
	unsigned int dimse_timeout = 0;
	unsigned int max_receive_pdu_length = ASC_DEFAULTMAXPDU;
	T_ASC_ProtocolFamily protocol_version = ASC_AF_Default;

	bool use_tls = false;
	pair<string, string> tls_private_key_ca_files;
	string peer_ca_file;

	OFList<OFString> query = OFList<OFString>();
};

struct QueryDicomGlobalState : public GlobalTableFunctionState {
	bool is_processed = false;
	idx_t MaxThreads() const override {
		return 1;
	}
};

unique_ptr<FunctionData> QueryDicomFuncBind(ClientContext &, TableFunctionBindInput &, vector<LogicalType> &,
                                            vector<string> &);
unique_ptr<GlobalTableFunctionState> QueryDicomGlobalInit(ClientContext &, TableFunctionInitInput &);
void QueryDicomFunc(ClientContext &, TableFunctionInput &, DataChunk &);

void RegisterDicomQuery(ExtensionLoader &);

} // namespace duckdb
