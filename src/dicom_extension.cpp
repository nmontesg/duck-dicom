#define DUCKDB_EXTENSION_MAIN

#include "dicom_extension.hpp"
#include "dcmtk2duckdb_logger.hpp"
#include "dcmtk/dcmdata/dcfilefo.h"
#include "dcmtk/dcmdata/dcistrmb.h"
#include "dcmtk/dcmdata/dcjson.h"
#include "dcmtk/dcmdata/dcpath.h"
#include "dcmtk/dcmdata/dcuid.h"
#include "duckdb.hpp"
#include "duckdb/common/multi_file/multi_file_reader.hpp"
#include "duckdb/main/secret/secret_manager.hpp"
#include "duckdb/parser/parsed_data/create_table_function_info.hpp"

namespace duckdb {

struct ReadDicomOptions {
	bool load_pixel_data = false;
};

struct ReadDicomBindData : public TableFunctionData {
	vector<OpenFileInfo> files;
	ReadDicomOptions options;
};

static void RedirectDCMTKLogsToDuckDB(ClientContext &context) {
	auto appender = dcmtk::log4cplus::SharedAppenderPtr(new Dcmtk2DuckDBLogger(&context));
	dcmtk::log4cplus::Logger::getRoot().removeAllAppenders();
	dcmtk::log4cplus::Logger::getRoot().addAppender(appender);
}

unique_ptr<FunctionData> ReadDicomFuncBind(ClientContext &context, TableFunctionBindInput &input,
                                           vector<LogicalType> &return_types, vector<string> &names) {
	if (input.inputs.empty()) {
		throw InvalidInputException("read_dicom requires at least one argument");
	}

	RedirectDCMTKLogsToDuckDB(context);

	auto &path_param = input.inputs[0];
	string glob_pattern = StringValue::Get(path_param);
	auto &fs = FileSystem::GetFileSystem(context);
	auto file_list = fs.Glob(glob_pattern);
	if (file_list.empty()) {
		throw IOException("No DICOM files found matching the provided pattern.");
	}

	auto result = make_uniq<ReadDicomBindData>();
	result->files = std::move(file_list);

	// parse options
	for (const auto &kv : input.named_parameters) {
		if (StringUtil::Lower(kv.first) == "load_pixel_data") {
			result->options.load_pixel_data = BooleanValue::Get(kv.second);
		}
	}

	names.push_back("path");
	return_types.push_back(LogicalType::VARCHAR);

	names.push_back("dicom_content");
	return_types.push_back(LogicalType::JSON());

	return std::move(result);
}

struct ReadDicomGlobalState : public GlobalTableFunctionState {
	std::mutex mutex;
	int64_t num_files_left_to_read;
	const int64_t total_files;

	explicit ReadDicomGlobalState(int64_t total_files)
	    : GlobalTableFunctionState(), num_files_left_to_read(total_files), total_files(total_files) {
	}

	bool GetWorkItem(idx_t &start_idx, idx_t &end_idx) {
		std::lock_guard<std::mutex> lock(mutex);
		if (num_files_left_to_read == 0) {
			return false;
		}
		start_idx = total_files - num_files_left_to_read;
		idx_t work_size = MinValue<idx_t>(num_files_left_to_read, STANDARD_VECTOR_SIZE);
		end_idx = start_idx + work_size;
		num_files_left_to_read -= work_size;
		return true;
	}

	idx_t MaxThreads() const override {
		return GlobalTableFunctionState::MAX_THREADS;
	}
};

struct ReadDicomLocalState : public LocalTableFunctionState {
	explicit ReadDicomLocalState() : LocalTableFunctionState() {
	}
};

static unique_ptr<GlobalTableFunctionState> ReadDicomGlobalInit(ClientContext &context, TableFunctionInitInput &input) {
	auto &bind_data = input.bind_data->Cast<ReadDicomBindData>();
	return make_uniq<ReadDicomGlobalState>(bind_data.files.size());
}

static unique_ptr<LocalTableFunctionState> ReadDicomLocalInit(ExecutionContext &context, TableFunctionInitInput &input,
                                                              GlobalTableFunctionState *global_state_p) {
	return make_uniq<ReadDicomLocalState>();
}

void ReadDicomFunc(ClientContext &context, TableFunctionInput &data, DataChunk &output) {
	auto &global_state = data.global_state->Cast<ReadDicomGlobalState>();
	auto &bind_data = data.bind_data->Cast<ReadDicomBindData>();

	string dicom_logtype = "dicom";

	idx_t start_idx, end_idx;
	if (!global_state.GetWorkItem(start_idx, end_idx)) {
		output.SetCardinality(0);
		return;
	}

	auto &read_options = bind_data.options;

	idx_t count = end_idx - start_idx;
	auto &path_vector = output.data[0];
	auto &content_vector = output.data[1];

	auto path_data = FlatVector::GetData<string_t>(path_vector);
	auto content_data = FlatVector::GetData<string_t>(content_vector);

	thread_local std::ostringstream jsonStream;

	auto &fs = FileSystem::GetFileSystem(context);

	idx_t actual_count = 0;
	for (idx_t i = 0; i < count; i++) {
		idx_t file_index = start_idx + i;

		// path column
		auto file_path = bind_data.files[file_index].path;
		path_data[i] = StringVector::AddString(path_vector, file_path);

		// dicom_content column
		auto handle = fs.OpenFile(file_path, FileOpenFlags::FILE_FLAGS_READ);
		auto file_size = handle->GetFileSize();
		auto buffer = std::unique_ptr<char[]>(new char[file_size]);
		handle->Read(buffer.get(), file_size);

		DcmInputBufferStream bufferStream;
		bufferStream.setBuffer(buffer.get(), file_size);
		bufferStream.setEos();

		DcmFileFormat fileformat;
		OFCondition status = fileformat.read(bufferStream);
		handle->Close();

		if (status.good()) {
			DcmDataset *dataset = fileformat.getDataset();
			if (!read_options.load_pixel_data) {
				dataset->findAndDeleteElement(DcmTag(0x7FE0, 0x0010), OFTrue, OFTrue);
			}

			jsonStream.str("");
			jsonStream.clear();

			DcmJsonFormatCompact format;
			dataset->writeJson(jsonStream, format);

			content_data[i] = StringVector::AddString(content_vector, "{" + jsonStream.str() + "}");
		} else {
			auto &logger = Logger::Get(context);
			logger.WriteLog(dicom_logtype.c_str(), LogLevel::LOG_WARNING, "Could not read file " + file_path);
			FlatVector::SetNull(content_vector, i, true);
		}
		actual_count += 1;
	}

	output.SetCardinality(actual_count);
}

unique_ptr<NodeStatistics> ReadDicomCardinality(ClientContext &context, const FunctionData *bind_data_p) {
	auto &bind_data = bind_data_p->Cast<ReadDicomBindData>();
	return make_uniq<NodeStatistics>(static_cast<idx_t>(bind_data.files.size()));
}

double ReadDicomProgress(ClientContext &context, const FunctionData *bind_data_p,
                         const GlobalTableFunctionState *global_state_p) {
	auto &bind_data = bind_data_p->Cast<ReadDicomBindData>();
	auto &global_state = global_state_p->Cast<ReadDicomGlobalState>();

	if (bind_data.files.empty()) {
		return 100.0;
	}
	idx_t files_processed = bind_data.files.size() - global_state.num_files_left_to_read;
	return 100.0 * static_cast<double>(files_processed) / static_cast<double>(bind_data.files.size());
}

static void LoadInternal(ExtensionLoader &loader) {
	// read_dicom table function
	TableFunction read_dicom_func("read_dicom", {LogicalType::VARCHAR}, ReadDicomFunc, ReadDicomFuncBind,
	                              ReadDicomGlobalInit, ReadDicomLocalInit);
	read_dicom_func.named_parameters["load_pixel_data"] = LogicalType::BOOLEAN;

	CreateTableFunctionInfo read_dicom_info(read_dicom_func);
	FunctionDescription read_dicom_desc;
	read_dicom_desc.parameter_types = {LogicalType::VARCHAR, LogicalType::BOOLEAN};
	read_dicom_desc.parameter_names = {"path", "load_pixel_data"};
	read_dicom_desc.description =
	    "Load DICOM files into DuckDB. The contents of each file are loaded into a row in JSON format.";
	read_dicom_desc.examples = {"SELECT * FROM read_dicom('/path/to/dicom_files/*');",
	                            "SELECT * FROM read_dicom('/path/to/dicom_files/*', load_pixel_data = true);"};
	read_dicom_desc.categories = {"medical"};
	read_dicom_info.descriptions.push_back(read_dicom_desc);

	read_dicom_func.cardinality = ReadDicomCardinality;
	read_dicom_func.table_scan_progress = ReadDicomProgress;

	loader.RegisterFunction(MultiFileReader::CreateFunctionSet(read_dicom_func));
}

void DicomExtension::Load(ExtensionLoader &loader) {
	LoadInternal(loader);
}

std::string DicomExtension::Name() {
	return "dicom";
}

std::string DicomExtension::Version() const {
#ifdef EXT_VERSION_DICOM
	return EXT_VERSION_DICOM;
#else
	return "0.1.0";
#endif
}

} // namespace duckdb

extern "C" {

DUCKDB_CPP_EXTENSION_ENTRY(dicom, loader) {
	duckdb::LoadInternal(loader);
}
}
