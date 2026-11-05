#pragma once
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "Helpers/Helper.hpp"

namespace LHExecutor::LHDL {

enum DLOperation { NONE, ADD, DELETE, METADATA, MARKER };

struct DeltaLogEntry {
  DLOperation operation;
  // min/max statistics in the column order of the stats JSON
  std::vector<std::string> min;
  std::vector<std::string> max;
  // the column names of min and max (same positions)
  std::vector<std::string> min_columns;
  std::vector<std::string> max_columns;
  // numRecords of the statistics (-1 if unknown)
  int64_t num_records = -1;
  // raw stats JSON of the add action ("" if unknown)
  std::string stats;
  // file size in bytes (-1 if unknown)
  int64_t size = -1;
  // deletionTimestamp of a remove action (tombstone; 0 if unknown)
  int64_t deletion_timestamp = 0;
  // modificationTime of an add action (-1 if unknown)
  int64_t modification_time = -1;
  // partitionValues and tags of the action as JSON objects ("" if absent);
  // kept for checkpoints
  std::string partition_values;
  std::string tags;
  // log version of the action (checkpoint: its version; -1 if unknown)
  int64_t version = -1;

  DeltaLogEntry(DLOperation operation);

  // stats may be nullptr or empty (statistics are optional in Delta)
  DeltaLogEntry(DLOperation operation, const char* stats, int64_t size = -1);

  DeltaLogEntry(DLOperation operation, std::vector<std::string>& min,
                std::vector<std::string>& max);

  // statistics of a file written by LakeVilla (columns with min/max)
  DeltaLogEntry(DLOperation operation, std::vector<std::string> columns,
                std::vector<std::string> min, std::vector<std::string> max,
                int64_t num_records);

  // min/max statistics of a column (exact name, else case-insensitive);
  // nullptr if the statistics do not contain the column
  const std::string* min_of(const std::string& column) const;
  const std::string* max_of(const std::string& column) const;

  // entry of the value of an add (ADD) or remove (DELETE) action of the
  // given log version
  static DeltaLogEntry from_action(DLOperation operation,
                                   const rapidjson::Value& action,
                                   int64_t version);

  void update(DLOperation operation, bool overwrite = false);

 private:
  void NONEUpdate(DLOperation operation);
  void ADDUpdate(DLOperation operation);
  void DELETEUpdate(DLOperation operation);
  void METADATAUpdate(DLOperation operation);
};

};  // namespace LHExecutor::LHDL
