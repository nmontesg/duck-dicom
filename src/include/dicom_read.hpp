#pragma once

#include "duckdb.hpp" // IWYU pragma: keep
#include <thread>

#define READ_DICOM_BATCH_SIZE_SETTING "read_dicom_batch_size"
#define DEFAULT_BATCH_SIZE 64

#define READ_DICOM_INTERNAL_BUFFER_SIZE_SETTING "read_dicom_internal_buffer_size"
#define DEFAULT_BUFFER_SIZE 128

namespace duckdb {

struct ReadDicomOptions {
	bool load_pixel_data = false;
};

struct ReadDicomBindData : public TableFunctionData {
	vector<OpenFileInfo> files;
	ReadDicomOptions options;
};

unique_ptr<FunctionData> ReadDicomFuncBind(ClientContext &, TableFunctionBindInput &, vector<LogicalType> &,
                                           vector<Identifier> &);

struct ReadDicomGlobalState : public GlobalTableFunctionState {
	std::mutex mutex;
	idx_t num_files_left_to_read;
	const idx_t total_files;
	const size_t max_batch_size;

	explicit ReadDicomGlobalState(int64_t total_files, size_t batch_size)
	    : GlobalTableFunctionState(), num_files_left_to_read(total_files), total_files(total_files),
	      max_batch_size(batch_size) {
	}

	bool GetWorkItem(idx_t &start_idx, idx_t &end_idx) {
		std::lock_guard<std::mutex> lock(mutex);
		if (num_files_left_to_read == 0) {
			return false;
		}
		start_idx = total_files - num_files_left_to_read;
		idx_t work_size = MinValue<idx_t>(num_files_left_to_read, max_batch_size);
		end_idx = start_idx + work_size;
		num_files_left_to_read -= work_size;
		return true;
	}

	idx_t MaxThreads() const override {
		return GlobalTableFunctionState::MAX_THREADS;
	}
};

struct ReadDicomLocalState : public LocalTableFunctionState {
	vector<unsigned char> buffer;

#ifdef INSPECT_READ_PERFORMANCE
	uint64_t thread_id;
#endif

	explicit ReadDicomLocalState(size_t buf_size) : LocalTableFunctionState() {
		buffer.resize(buf_size);

#ifdef INSPECT_READ_PERFORMANCE
		thread_id = std::hash<std::thread::id> {}(std::this_thread::get_id());
#endif
	}
};

unique_ptr<GlobalTableFunctionState> ReadDicomGlobalInit(ClientContext &, TableFunctionInitInput &);
unique_ptr<LocalTableFunctionState> ReadDicomLocalInit(ExecutionContext &, TableFunctionInitInput &,
                                                       GlobalTableFunctionState *);
void ReadDicomFunc(ClientContext &, TableFunctionInput &, DataChunk &);
unique_ptr<NodeStatistics> ReadDicomCardinality(ClientContext &, const FunctionData *);
double ReadDicomProgress(ClientContext &, const FunctionData *, const GlobalTableFunctionState *);
void RegisterDicomRead(ExtensionLoader &);

} // namespace duckdb
