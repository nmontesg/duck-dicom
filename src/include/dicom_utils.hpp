#pragma once

#include "duckdb.hpp"
#include "dicom_query.hpp"
#include "dicom_retrieve.hpp"
#include "duckdb/catalog/catalog_transaction.hpp"
#include "duckdb/main/secret/secret_manager.hpp"

namespace duckdb {

class DuckDBDicomUtils {
public:
	inline static const vector<string> QUERY_RETRIEVE_LEVELS = {"PATIENT", "STUDY", "SERIES", "IMAGE"};

	static void ParseQueryMatchKeys(const Value &, QueryDicomBindData &);
	static void ParseQueryRetrieveKeys(const Value &, QueryDicomBindData &);

	static void ParseDicomDataset(const string &, const string &, DcmDataset *);

	template <typename BindDataT>
	static void GetSecretParams(ClientContext &context, const string &secret_name, BindDataT &bind_data) {
		auto &secret_manager = SecretManager::Get(context);
		auto transaction = CatalogTransaction::GetSystemCatalogTransaction(context);
		auto secret_entry = secret_manager.GetSecretByName(transaction, secret_name);
		if (!secret_entry) {
			throw InvalidInputException("Could not find secret " + secret_name);
		}
		const auto &kv_secret = dynamic_cast<const KeyValueSecret &>(*secret_entry->secret);

		bind_data.host = kv_secret.TryGetValue("host", true).GetValue<string>();
		bind_data.port = kv_secret.TryGetValue("port", true).GetValue<unsigned int>();
		if (!kv_secret.TryGetValue("aetitle").IsNull()) {
			bind_data.called_ae_title = kv_secret.TryGetValue("aetitle", true).GetValue<string>();
		}
		if (!kv_secret.TryGetValue("tls_key_file").IsNull()) {
			bind_data.tls_private_key_ca_files.first = kv_secret.TryGetValue("tls_key_file", true).GetValue<string>();
			bind_data.use_tls = true;
		}
		if (!kv_secret.TryGetValue("tls_ca_file").IsNull()) {
			bind_data.tls_private_key_ca_files.second = kv_secret.TryGetValue("tls_ca_file", true).GetValue<string>();
			bind_data.use_tls = true;
		}
		if (!kv_secret.TryGetValue("peer_ca_file").IsNull()) {
			bind_data.peer_ca_file = kv_secret.TryGetValue("peer_ca_file", true).GetValue<string>();
			bind_data.use_tls = true;
		}
	}

	template <typename BindDataT>
	static void CheckTlsParams(ClientContext &context, BindDataT &bind_data) {
		if (bind_data.tls_private_key_ca_files.first.empty()) {
			throw InvalidInputException("TLS setup is missing private key file.");
		}
		if (bind_data.tls_private_key_ca_files.second.empty()) {
			throw InvalidInputException("TLS setup is missing certificate file.");
		}
		if (bind_data.peer_ca_file.empty()) {
			throw InvalidInputException("TlS setup is missing peer certificate.");
		}

		auto &fs = FileSystem::GetFileSystem(context);
		if (!fs.FileExists(bind_data.tls_private_key_ca_files.first)) {
			throw InvalidInputException("Could not find private key file " + bind_data.tls_private_key_ca_files.first);
		}
		if (!fs.FileExists(bind_data.tls_private_key_ca_files.second)) {
			throw InvalidInputException("Could not find certificate file " + bind_data.tls_private_key_ca_files.second);
		}
		if (!fs.FileExists(bind_data.peer_ca_file)) {
			throw InvalidInputException("Could not find peer certificate file " + bind_data.peer_ca_file);
		}
	}
};

} // namespace duckdb
