#include "DeltaLogEntry.hpp"

using namespace LHExecutor::LHDL;

DeltaLogEntry::DeltaLogEntry(DLOperation operation) : operation(operation) {
  this->min.clear();
  this->max.clear();
}

DeltaLogEntry::DeltaLogEntry(DLOperation operation, const char* stats)
    : operation(operation) {
  this->min.clear();
  this->max.clear();

  auto sub_json = LHHelpers::readJSON_rapid(stats);

  if (sub_json->HasMember("minValues")) {
    uint32_t counter = 0;

    auto& member = (*sub_json)["minValues"];

    for (auto itr = member.MemberBegin(); itr != member.MemberEnd(); ++itr) {
      if (itr->value.IsString()) {
        this->min.push_back(itr->value.GetString());
      }

      if (itr->value.IsInt()) {
        this->min.push_back(std::to_string(itr->value.GetInt()));
      }
    }
  }

  if (sub_json->HasMember("maxValues")) {
    uint32_t counter = 0;

    auto& member = (*sub_json)["maxValues"];

    for (auto itr = member.MemberBegin(); itr != member.MemberEnd(); ++itr) {
      if (itr->value.IsString()) {
        this->max.push_back(itr->value.GetString());
      }
      if (itr->value.IsInt()) {
        this->max.push_back(std::to_string(itr->value.GetInt()));
      }
    }
  }
}

DeltaLogEntry::DeltaLogEntry(DLOperation operation,
                             std::vector<std::string>& min,
                             std::vector<std::string>& max)
    : operation(operation) {
  this->min = min;
  this->max = max;
}

void DeltaLogEntry::update(DLOperation operation, bool overwrite) {
  if (overwrite) {
    this->operation = operation;
    return;
  }

  switch (this->operation) {
    case DLOperation::ADD:
      this->ADDUpdate(operation);
      break;
    case DLOperation::DELETE:
      this->DELETEUpdate(operation);
      break;
    case DLOperation::METADATA:
      this->METADATAUpdate(operation);
      break;
    case DLOperation::NONE:
      this->NONEUpdate(operation);
      break;

    default:
      break;
  }
}

void DeltaLogEntry::ADDUpdate(DLOperation operation) {
  switch (operation) {
    case DLOperation::ADD:
      // added again - do nothing
      break;
    case DLOperation::DELETE:
      // entry removed - delete
      this->operation = DLOperation::DELETE;
      break;
    case DLOperation::METADATA:
      // incompatible in this version
      break;
    case DLOperation::NONE:
      break;
    default:
      break;
  }
}

void DeltaLogEntry::DELETEUpdate(DLOperation operation) {
  switch (operation) {
    case DLOperation::ADD:
      // added file - remove beats adding
      break;
    case DLOperation::DELETE:
      // entry removed again - do nothing
      break;
    case DLOperation::METADATA:
      // incompatible in this version
      break;
    case DLOperation::NONE:
      break;
    default:
      break;
  }
}

void DeltaLogEntry::METADATAUpdate(DLOperation operation) {
  switch (operation) {
    case DLOperation::ADD:
      // incompatible in this version
      break;
    case DLOperation::DELETE:
      // incompatible in this version
      break;
    case DLOperation::METADATA:
      // incompatible in this version
      break;
    case DLOperation::NONE:
      break;
    default:
      break;
  }
}

void DeltaLogEntry::NONEUpdate(DLOperation operation) {
  switch (operation) {
    case DLOperation::ADD:
      this->operation = DLOperation::ADD;
      break;
    case DLOperation::DELETE:
      this->operation = DLOperation::DELETE;
      break;
    case DLOperation::METADATA:
      this->operation = DLOperation::METADATA;
      break;
    case DLOperation::NONE:
      break;
    default:
      break;
  }
}