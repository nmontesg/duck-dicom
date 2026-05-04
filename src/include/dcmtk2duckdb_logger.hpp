#include "duckdb.hpp"

#include "dcmtk/oflog/appender.h"
#include "dcmtk/oflog/layout.h"
#include "dcmtk/oflog/oflog.h"
#include "dcmtk/oflog/spi/logevent.h"

namespace duckdb {

class Dcmtk2DuckDBLogger : public dcmtk::log4cplus::Appender {
public:

    Dcmtk2DuckDBLogger(ClientContext *client_context)
		: dcmtk::log4cplus::Appender(), context(client_context) { }

    ~Dcmtk2DuckDBLogger() override {
        this->destructorImpl();
    }

    void close() override { }

protected:

    void append(const dcmtk::log4cplus::spi::InternalLoggingEvent &event) override {
        string msg = event.getMessage();
		dcmtk::log4cplus::LogLevel dcmtk_loglevel = event.getLogLevel();

		LogLevel duckdb_loglevel = LogLevel::LOG_TRACE;

		if (dcmtk_loglevel == dcmtk::log4cplus::FATAL_LOG_LEVEL) {
			duckdb_loglevel = LogLevel::LOG_FATAL;
		} else if (dcmtk_loglevel == dcmtk::log4cplus::ERROR_LOG_LEVEL) {
			duckdb_loglevel = LogLevel::LOG_ERROR;
		} else if (dcmtk_loglevel == dcmtk::log4cplus::WARN_LOG_LEVEL) {
			duckdb_loglevel = LogLevel::LOG_WARNING;
		} else if (dcmtk_loglevel == dcmtk::log4cplus::INFO_LOG_LEVEL) {
			duckdb_loglevel = LogLevel::LOG_INFO;
		} else if (dcmtk_loglevel == dcmtk::log4cplus::DEBUG_LOG_LEVEL) {
			duckdb_loglevel = LogLevel::LOG_DEBUG;
		}

        auto &logger = Logger::Get(*context);
		string dcmtk_logtype = "dcmtk";
		logger.WriteLog(dcmtk_logtype.c_str(), LogLevel::LOG_WARNING, msg);
    }

private:
	ClientContext *context;
};

} // namespace duckdb
