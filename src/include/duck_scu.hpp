#include "duckdb.hpp"

#include "dcmtk/dcmnet/scu.h"

namespace duckdb {

class DuckSCU : public DcmSCU {
public:
	DuckSCU(bool lpd, Vector &c_vec) : DcmSCU(), load_pixel_data(lpd), content_vector(c_vec), incoming_object_idx(0) {
	}

	uint64_t getIncomingObjectIdx() {
		return incoming_object_idx;
	}

	uint64_t getNumReceivedObjects() {
		return incoming_object_idx + 1;
	}

	OFCondition handleSTORERequest(const T_ASC_PresentationContextID, DcmDataset *, OFBool &, Uint16 &) override;

	// static void prepareTransferSyntaxes(E_TransferSyntax, OFList<OFString> &);

private:
	bool load_pixel_data;
	std::mutex mutex;
	uint64_t incoming_object_idx;
	Vector &content_vector;
};

} // namespace duckdb
