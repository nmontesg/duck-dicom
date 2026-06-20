#include "dicom_extension.hpp"

namespace duckdb {

void CheckFileExists(KeyValueSecret &secret, FileSystem &fs, string paramName) {
	auto filepath = secret.secret_map[paramName].ToString();
	if (!fs.FileExists(filepath)) {
		throw InvalidInputException("Unable to locate file: " + filepath);
	}
}

unique_ptr<BaseSecret> CreateDicomSecretFunction(ClientContext &context, CreateSecretInput &input) {
	vector<string> prefix_paths;
	auto result = make_uniq<KeyValueSecret>(prefix_paths, "dicom", input.storage_type, input.name);

	bool use_tls = false;
	for (const auto &named_param : input.options) {
		if (named_param.first == "host") {
			result->secret_map["host"] = named_param.second.ToString();
		} else if (named_param.first == "port") {
			result->secret_map["port"] = named_param.second.ToString();
		} else if (named_param.first == "aetitle") {
			result->secret_map["aetitle"] = named_param.second.ToString();
		} else if (named_param.first == "tls_ca_file") {
			result->secret_map["tls_ca_file"] = named_param.second.ToString();
			use_tls = true;
		} else if (named_param.first == "tls_key_file") {
			result->secret_map["tls_key_file"] = named_param.second.ToString();
			use_tls = true;
		} else if (named_param.first == "peer_ca_file") {
			result->secret_map["peer_ca_file"] = named_param.second.ToString();
			use_tls = true;
		} else {
			throw InvalidInputException("Unknown named parameter for DICOM secret: " + named_param.first);
		}
	}

	if (result->secret_map["host"].IsNull()) {
		throw InvalidInputException("DICOM secret needs to specify DICOM peer host.");
	}
	if (result->secret_map["port"].IsNull()) {
		throw InvalidInputException("DICOM secret needs to specify DICOM peer port.");
	}

	if (use_tls) {
		if (result->secret_map["tls_ca_file"].IsNull()) {
			throw InvalidInputException("When using TLS, TLS CA file must be provided.");
		}
		if (result->secret_map["tls_key_file"].IsNull()) {
			throw InvalidInputException("When using TLS, TLS key file must be provided.");
		}
		if (result->secret_map["peer_ca_file"].IsNull()) {
			throw InvalidInputException("When using TLS, peer CA file must be provided.");
		}

		auto &fs = FileSystem::GetFileSystem(context);
		CheckFileExists(*result, fs, "tls_ca_file");
		CheckFileExists(*result, fs, "tls_key_file");
		CheckFileExists(*result, fs, "peer_ca_file");
	}

	result->redact_keys.insert("tls_ca_file");
	result->redact_keys.insert("tls_key_file");
	result->redact_keys.insert("peer_ca_file");

	return std::move(result);
}

void RegisterDicomSecret(ExtensionLoader &loader) {
	SecretType secret_type;
	secret_type.name = "dicom";
	secret_type.deserializer = KeyValueSecret::Deserialize<KeyValueSecret>;
	secret_type.default_provider = "config";
	secret_type.extension = "dicom";

	loader.RegisterSecretType(secret_type);

	CreateSecretFunction dicom_secret_function = {"dicom", "config", CreateDicomSecretFunction};
	dicom_secret_function.named_parameters["host"] = LogicalType::VARCHAR;
	dicom_secret_function.named_parameters["port"] = LogicalType::UINTEGER;
	dicom_secret_function.named_parameters["aetitle"] = LogicalType::VARCHAR;
	dicom_secret_function.named_parameters["tls_ca_file"] = LogicalType::VARCHAR;
	dicom_secret_function.named_parameters["tls_key_file"] = LogicalType::VARCHAR;
	dicom_secret_function.named_parameters["peer_ca_file"] = LogicalType::VARCHAR;

	loader.RegisterFunction(dicom_secret_function);
}

} // namespace duckdb
