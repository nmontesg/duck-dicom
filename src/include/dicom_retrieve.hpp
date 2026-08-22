#pragma once

#include "duckdb.hpp" // IWYU pragma: keep
#include "dicom_read.hpp"
#include "dcmtk/dcmdata/dcuid.h"
#include "dcmtk/dcmdata/dcxfer.h"
#include "dcmtk/dcmnet/dimse.h"

namespace duckdb {

struct RetrieveDicomBindData : public TableFunctionData {
	string host;
	unsigned int port;
	unsigned int incoming_port;
	string called_ae_title = "ANY-SCP";
	string calling_ae_title = "DUCKDB";
	string abstract_syntax_find = UID_FINDStudyRootQueryRetrieveInformationModel;
	string abstract_syntax_move = UID_MOVEStudyRootQueryRetrieveInformationModel;
	string query_retrieve_level = "STUDY";
	vector<string> uids;
	E_TransferSyntax network_transfer_syntax = EXS_Unknown;
	T_DIMSE_BlockingMode block_mode = DIMSE_BLOCKING;
	unsigned int acse_timeout = 30;
	unsigned int dimse_timeout = 0;
	unsigned int max_receive_pdu_length = ASC_DEFAULTMAXPDU;
	T_ASC_ProtocolFamily protocol_version = ASC_AF_Default;

	bool use_tls = false;
	pair<string, string> tls_private_key_ca_files;
	string peer_ca_file;

	ReadDicomOptions read_options = {false};
};

unique_ptr<FunctionData> RetrieveDicomFuncSingleBind(ClientContext &, TableFunctionBindInput &, vector<LogicalType> &,
                                                     vector<string> &);
unique_ptr<FunctionData> RetrieveDicomFuncListBind(ClientContext &, TableFunctionBindInput &, vector<LogicalType> &,
                                                   vector<string> &);
void RetrieveDicomFuncBind(ClientContext &, TableFunctionBindInput &, vector<LogicalType> &, vector<string> &,
                           RetrieveDicomBindData &);
unique_ptr<GlobalTableFunctionState> RetrieveDicomGlobalInit(ClientContext &, TableFunctionInitInput &);
void RetrieveDicomFunc(ClientContext &, TableFunctionInput &, DataChunk &);

struct RetrieveDicomMoveCallbackData {
	Logger &logger;

	explicit RetrieveDicomMoveCallbackData(Logger &_logger) : logger(_logger) {};
};

struct RetrieveSubOpCallbackData {
	Logger &logger;
	bool use_tls;
	unsigned int max_receive_pdu_length;
	T_DIMSE_BlockingMode block_mode;
	unsigned int dimse_timeout;
	unsigned int num_recv_datasets;
	bool load_pixel_data;
	Vector &response_vector;
	unsigned int offset;

	RetrieveSubOpCallbackData(Logger &_logger, bool _use_tls, unsigned int _max_recv_pdu_len,
	                          T_DIMSE_BlockingMode _block_mode, unsigned int _dimse_timeout, bool _lpd,
	                          Vector &_resp_vec, unsigned int _off)
	    : logger(_logger), use_tls(_use_tls), max_receive_pdu_length(_max_recv_pdu_len), block_mode(_block_mode),
	      dimse_timeout(_dimse_timeout), num_recv_datasets(0), load_pixel_data(_lpd), response_vector(_resp_vec),
	      offset(_off) {};
};

void moveCallback(void *, T_DIMSE_C_MoveRQ *, int, T_DIMSE_C_MoveRSP *);
void subOpCallback(void *, T_ASC_Network *, T_ASC_Association **);
OFCondition acceptSubAssoc(T_ASC_Network *, T_ASC_Association **, unsigned int, bool, Logger &);
OFCondition subOpSCP(T_ASC_Association **, T_DIMSE_BlockingMode, unsigned int, Logger &, unsigned int &, bool, Vector &,
                     unsigned int);
OFCondition storeSCP(T_ASC_Association *, T_DIMSE_Message *, T_ASC_PresentationContextID, Logger &,
                     T_DIMSE_BlockingMode, unsigned int, unsigned int &, bool, Vector &, unsigned int);

void RegisterDicomRetrieve(ExtensionLoader &);

} // namespace duckdb
