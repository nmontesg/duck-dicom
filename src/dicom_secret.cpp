#include "dicom_extension.hpp"

namespace duckdb {

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
		} else if (named_param.first == "tls") {
			result->secret_map["tls"] = named_param.second.GetValue<bool>();
			use_tls = named_param.second.GetValue<bool>();
		} else if (named_param.first == "tls_ca_file") {
			result->secret_map["tls_ca_file"] = named_param.second.ToString();
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
			throw InvalidInputException("TLS option is set to on but no CA file is provided");
		}
		auto &fs = FileSystem::GetFileSystem(context);
		auto tls_ca_filepath = result->secret_map["tls_ca_file"].ToString();
		if (!fs.FileExists(tls_ca_filepath)) {
			throw InvalidInputException("Unable to find TLS CA file: " + tls_ca_filepath);
		}
	}

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
	dicom_secret_function.named_parameters["tls"] = LogicalType::BOOLEAN;
	dicom_secret_function.named_parameters["tls_ca_file"] = LogicalType::VARCHAR;

	loader.RegisterFunction(dicom_secret_function);
}

} // namespace duckdb
