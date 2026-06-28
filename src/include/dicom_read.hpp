#include "duckdb.hpp"

namespace duckdb {

struct ReadDicomOptions {
	bool load_pixel_data = false;
};

struct ReadDicomBindData : public TableFunctionData {
	vector<OpenFileInfo> files;
	ReadDicomOptions options;
};

unique_ptr<FunctionData> ReadDicomFuncBind(ClientContext &, TableFunctionBindInput &, vector<LogicalType> &,
                                           vector<string> &);

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

unique_ptr<GlobalTableFunctionState> ReadDicomGlobalInit(ClientContext &, TableFunctionInitInput &);
unique_ptr<LocalTableFunctionState> ReadDicomLocalInit(ExecutionContext &, TableFunctionInitInput &,
                                                       GlobalTableFunctionState *);
void ReadDicomFunc(ClientContext &, TableFunctionInput &, DataChunk &);
unique_ptr<NodeStatistics> ReadDicomCardinality(ClientContext &, const FunctionData *);
double ReadDicomProgress(ClientContext &, const FunctionData *, const GlobalTableFunctionState *);
void RegisterDicomRead(ExtensionLoader &);

} // namespace duckdb
