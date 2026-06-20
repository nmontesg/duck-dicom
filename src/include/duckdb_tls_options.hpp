#include "dcmtk/dcmtls/tlsopt.h"
#include "duckdb.hpp"

namespace duckdb {

class DuckDBTlsOptions : public DcmTLSOptionsBase {
public:
	DuckDBTlsOptions(T_ASC_NetworkRole networkRole, const string &_key_file, const string &_ca_file,
	                 const string &_trusted_ca_file)
	    : DcmTLSOptionsBase(networkRole) {
		opt_privateKeyFile = _key_file.c_str();
		opt_certificateFile = _ca_file.c_str();
		opt_peerCAFile = _trusted_ca_file.c_str();
	};

	~DuckDBTlsOptions() {};

	virtual OFCondition createTransportLayer();

protected:
	const char *opt_peerCAFile;
};

} // namespace duckdb
