#pragma once

#include <arrow/api.h>

#include <memory>
#include <string>
#include <vector>

#include "Connectors/Connector.hpp"
#include "Connectors/MemoryConnector.hpp"

namespace lvtest {

// a one-row YCSB table: YCSB_KEY, field0..field9 (all strings)
std::shared_ptr<arrow::Table> ycsb_table(const std::string& key,
                                         const std::string& value = "v");

// Parquet bytes of ycsb_table(key, value)
std::string ycsb_parquet(const std::string& key,
                         const std::string& value = "v");

// zero-padded Delta log file name of a version, e.g. 00000000000000000003.json
std::string log_name(uint64_t version);

// JSON line of an add action with YCSB stats (minValues/maxValues = key)
std::string add_action(const std::string& file, uint64_t size,
                       const std::string& key);

std::string remove_action(const std::string& file);

std::string protocol_and_metadata();

// Creates a Delta table at base (ending with '/') with one data file per key
// in version 0. Returns the data file names (relative to base).
std::vector<std::string> create_ycsb_table(StorageConnector::Connector& conn,
                                           const std::string& base,
                                           const std::vector<std::string>& keys);

// writes the given action lines as Delta log version 'version'
void write_version(StorageConnector::Connector& conn, const std::string& base,
                   uint64_t version, const std::vector<std::string>& actions);

std::vector<std::string> split_lines(const std::string& content);

// all keys (YCSB_KEY column values) of a table
std::vector<std::string> keys_of(const std::shared_ptr<arrow::Table>& table);

}  // namespace lvtest
