#include "duckdb_findscu_callback.hpp"
#include "dcmtk/dcmdata/dcjson.h"

namespace duckdb {

void DuckDBFindSCUCallback::callback(T_DIMSE_C_FindRQ *, int &responseCount, T_DIMSE_C_FindRSP *,
                                     DcmDataset *responseIdentifiers) {
	thread_local std::ostringstream jsonStream;
	jsonStream.str("");
	jsonStream.clear();
	DcmJsonFormatCompact format;
	responseIdentifiers->writeJson(jsonStream, format);

	auto response_data = FlatVector::GetData<string_t>(response_vector);
	response_data[responseCount - 1] = StringVector::AddString(this->response_vector, "{" + jsonStream.str() + "}");
	this->num_responses = responseCount;
}

} // namespace duckdb
