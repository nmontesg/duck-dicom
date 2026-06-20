#include "duckdb.hpp"
#include "dicom_types.hpp"
#include "dcmtk/dcmnet/diutil.h"

namespace duckdb {

static vector<string> QUERY_RETRIEVE_LEVELS = {"PATIENT", "STUDY", "SERIES", "IMAGE"};

struct QueryDicomBindData : public TableFunctionData {
	string host;
	unsigned int port;
	string calledAETitle = "ANY-SCP";
	string callingAETitle = "DUCKDB";
	string abstractSyntax = UID_FINDStudyRootQueryRetrieveInformationModel;
	string query_retrieve_level = "STUDY";
	E_TransferSyntax networkTransferSyntax = EXS_Unknown;
	T_DIMSE_BlockingMode blockMode = DIMSE_BLOCKING;
	unsigned int acseTimeout = 30;
	unsigned int dimseTimeout = 0;
	unsigned int maxReceivePDULength = ASC_DEFAULTMAXPDU;
	T_ASC_ProtocolFamily protocolVersion = ASC_AF_Default;

	bool useTls = false;
	pair<string, string> tlsPrivateKeyCAFiles;
	string peerCAFile;

	OFList<OFString> query = OFList<OFString>();
};

struct QueryDicomGlobalState : public GlobalTableFunctionState {
	bool is_processed = false;
	idx_t MaxThreads() const override {
		return 1;
	}
};

void ParseMatchKeys(const Value &, QueryDicomBindData &);
void ParseRetrieveKeys(const Value &, QueryDicomBindData &);
unique_ptr<FunctionData> QueryDicomFuncBind(ClientContext &, TableFunctionBindInput &, vector<LogicalType> &,
                                            vector<string> &);
unique_ptr<GlobalTableFunctionState> QueryDicomGlobalInit(ClientContext &, TableFunctionInitInput &);
void QueryDicomFunc(ClientContext &, TableFunctionInput &, DataChunk &);

void RegisterDicomQueryFunctions(ExtensionLoader &);

} // namespace duckdb
