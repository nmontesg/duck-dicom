#pragma once

#include "duckdb.hpp" // IWYU pragma: keep
#include "dicom_query.hpp"
#include "duckdb/catalog/catalog_transaction.hpp"
#include "duckdb/main/secret/secret_manager.hpp"
#include "dcmtk/dcmdata/dcdatset.h"

namespace duckdb {

class DuckDBDicomUtils {
public:
	static const vector<string> GetQueryRetrieveLevels() {
		return {"PATIENT", "STUDY", "SERIES", "IMAGE"};
	};

	static const map<string, string> GetCharacterSetMapping() {
		map<string, string> CHARACTER_SET_MAPPING;

		// Table C.12-3
		CHARACTER_SET_MAPPING["ISO_IR 100"] = "ISO-IR-100";
		CHARACTER_SET_MAPPING["ISO_IR 101"] = "ISO-IR-101";
		CHARACTER_SET_MAPPING["ISO_IR 109"] = "ISO-IR-109";
		CHARACTER_SET_MAPPING["ISO_IR 110"] = "ISO-IR-110";
		CHARACTER_SET_MAPPING["ISO_IR 144"] = "ISO-IR-144";
		CHARACTER_SET_MAPPING["ISO_IR 127"] = "ISO-IR-127";
		CHARACTER_SET_MAPPING["ISO_IR 126"] = "ISO-IR-126";
		CHARACTER_SET_MAPPING["ISO_IR 138"] = "ISO-IR-138";
		CHARACTER_SET_MAPPING["ISO_IR 148"] = "ISO-IR-148";
		CHARACTER_SET_MAPPING["ISO_IR 203"] = "ISO-IR-203";
		CHARACTER_SET_MAPPING["ISO_IR 13"] = "ISO-IR-13";
		CHARACTER_SET_MAPPING["ISO_IR 166"] = "ISO-IR-166";

		// Table C.12-3
		CHARACTER_SET_MAPPING["ISO 2022 IR 6"] = "ISO-646";
		CHARACTER_SET_MAPPING["ISO 2022 IR 100"] = "ISO-IR-100";
		CHARACTER_SET_MAPPING["ISO 2022 IR 101"] = "ISO-IR-101";
		CHARACTER_SET_MAPPING["ISO 2022 IR 109"] = "ISO-IR-109";
		CHARACTER_SET_MAPPING["ISO 2022 IR 110"] = "ISO-IR-110";
		CHARACTER_SET_MAPPING["ISO 2022 IR 144"] = "ISO-IR-144";
		CHARACTER_SET_MAPPING["ISO 2022 IR 127"] = "ISO-IR-127";
		CHARACTER_SET_MAPPING["ISO 2022 IR 126"] = "ISO-IR-126";
		CHARACTER_SET_MAPPING["ISO 2022 IR 138"] = "ISO-IR-138";
		CHARACTER_SET_MAPPING["ISO 2022 IR 148"] = "ISO-IR-148";
		CHARACTER_SET_MAPPING["ISO 2022 IR 203"] = "ISO-IR-203";
		CHARACTER_SET_MAPPING["ISO 2022 IR 13"] = "ISO-IR-13";
		CHARACTER_SET_MAPPING["ISO 2022 IR 166"] = "ISO-IR-166";

		// Table C.12-4
		CHARACTER_SET_MAPPING["ISO 2022 IR 87"] = "ISO-IR-87";
		CHARACTER_SET_MAPPING["ISO 2022 IR 159"] = "ISO-IR-159";
		CHARACTER_SET_MAPPING["ISO 2022 IR 149"] = "ISO-IR-149";
		CHARACTER_SET_MAPPING["ISO 2022 IR 58"] = "ISO-IR-58";

		// Table C.12-5
		CHARACTER_SET_MAPPING["ISO_IR 192"] = "ISO-IR-192";
		CHARACTER_SET_MAPPING["GB18030"] = "GB18030";
		CHARACTER_SET_MAPPING["GBK"] = "GBK";

		return CHARACTER_SET_MAPPING;
	};

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

	static vector<string> GetCharacterSets(DcmDataset *);
};

} // namespace duckdb
