#pragma once
#include <iostream>
#include <string>
#include <vector>

#include "Helpers/Helper.hpp"

namespace LHExecutor::LHDL {

enum DLOperation { NONE, ADD, DELETE, METADATA, MARKER };

struct DeltaLogEntry {
  DLOperation operation;
  std::vector<std::string> min;
  std::vector<std::string> max;

  DeltaLogEntry(DLOperation operation);

  DeltaLogEntry(DLOperation operation, const char* stats);

  DeltaLogEntry(DLOperation operation, std::vector<std::string>& min,
                std::vector<std::string>& max);

  void update(DLOperation operation, bool overwrite = false);

 private:
  void NONEUpdate(DLOperation operation);
  void ADDUpdate(DLOperation operation);
  void DELETEUpdate(DLOperation operation);
  void METADATAUpdate(DLOperation operation);
};

};  // namespace LHExecutor::LHDL
