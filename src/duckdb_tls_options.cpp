#include "dcmtk/dcmtls/tlscond.h"
#include "duckdb_tls_options.hpp"

namespace duckdb {

OFCondition DuckDBTlsOptions::createTransportLayer() {
	delete tLayer;
	tLayer = new DcmTLSTransportLayer(opt_networkRole, opt_readSeedFile, OFFalse);
	OFCondition cond = tLayer->addTrustedCertificateFile(opt_peerCAFile, opt_keyFileFormat);
	if (cond.bad()) {
		DCMTLS_ERROR("Unable to load certificate file " << opt_peerCAFile);
		return EC_InvalidFilename;
	}

	cond = tLayer->setPrivateKeyFile(opt_privateKeyFile, opt_keyFileFormat);
	if (cond.bad()) {
		DCMTLS_ERROR("Unable to load private key file " << opt_privateKeyFile);
		return DCMTLS_EC_FailedToLoadPrivateKey(opt_privateKeyFile);
	}

	cond = tLayer->setCertificateFile(opt_certificateFile, opt_keyFileFormat, opt_tlsProfile);
	if (cond.bad()) {
		DCMTLS_ERROR("Unable to load certificate file " << opt_certificateFile);
		return DCMTLS_EC_FailedToLoadCertificate(opt_certificateFile);
	}

	opt_secureConnection = OFTrue;
	return EC_Normal;
}

} // namespace duckdb
