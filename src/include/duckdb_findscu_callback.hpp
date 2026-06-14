#include "dcmtk/dcmnet/dfindscu.h"
#include "duckdb.hpp"

namespace duckdb {
class DuckDBFindSCUCallback : public DcmFindSCUCallback {
public:
	DuckDBFindSCUCallback(Vector &_resp_vec) : DcmFindSCUCallback(), response_vector(_resp_vec), num_responses(0) {};
	~DuckDBFindSCUCallback() override {};

	void callback(T_DIMSE_C_FindRQ *, int &responseCount, T_DIMSE_C_FindRSP *,
	              DcmDataset *responseIdentifiers) override;

	uint GetNumResponses() {
		return num_responses;
	};

private:
	Vector &response_vector;
	uint num_responses;
};

} // namespace duckdb
