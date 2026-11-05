#include "Marker.hpp"

#include <chrono>
#include <thread>

LHTransactions::Marker::Marker(std::string&& marker_path,
                               StorageConnector::Connector* connector_ptr,
                               std::string etag, int64_t deadline_ms) {
  this->marker_path = marker_path;
  this->connector_ptr = connector_ptr;
  this->etag = std::move(etag);
  this->deadline_ms = deadline_ms;
  this->valid = true;
}

LHTransactions::Marker::~Marker() {
  if (this->valid && !this->release()) {
    std::cerr << "unable to release marker " << this->marker_path
              << std::endl;
  }
}

void LHTransactions::Marker::setInvalid() { this->valid = false; }

bool LHTransactions::Marker::release() {
  if (!this->valid) {
    return true;
  }
  auto log = empty_commit("MultiQuery");

  for (uint32_t attempt = 0; attempt < 5; attempt++) {
    if (this->etag.empty()) {
      if (this->connector_ptr->write(this->marker_path, log.c_str(),
                                     log.size())) {
        this->valid = false;
        return true;
      }
    } else {
      auto status = this->connector_ptr->write_if_match(
          this->marker_path, log.c_str(), log.size(), this->etag);
      if (status == StorageConnector::WriteStatus::Ok ||
          status == StorageConnector::WriteStatus::PreconditionFailed) {
        // released, or another transaction took the expired lease over
        this->valid = false;
        return true;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10 << attempt));
  }
  return false;
}

std::string LHTransactions::Marker::empty_commit(const std::string& mode) {
  rapidjson::Document d;
  d.SetObject();

  rapidjson::Value operation_para;
  rapidjson::Value operation_metrics;

  operation_para.SetObject();
  operation_metrics.SetObject();

  operation_para.AddMember("mode", rapidjson::Value(mode.c_str(), d.GetAllocator()),
                           d.GetAllocator());
  operation_para.AddMember("partitionBy", "[]", d.GetAllocator());

  operation_metrics.AddMember("numOutputBytes", 0, d.GetAllocator());
  operation_metrics.AddMember("numOutputRows", 0, d.GetAllocator());
  operation_metrics.AddMember("numFiles", 0, d.GetAllocator());

  rapidjson::Value commit_info;
  commit_info.SetObject();

  commit_info.AddMember("txnId", "1234", d.GetAllocator());
  commit_info.AddMember("engineInfo", "LHTransactions 0.1 - Delta Lake 3.0",
                        d.GetAllocator());
  commit_info.AddMember("operationMetrics", operation_metrics,
                        d.GetAllocator());
  commit_info.AddMember("isBlindAppend", false, d.GetAllocator());
  commit_info.AddMember("isolationLevel", "Serializable", d.GetAllocator());
  commit_info.AddMember("readVersion", 0, d.GetAllocator());
  commit_info.AddMember("operationParameters", operation_para,
                        d.GetAllocator());
  commit_info.AddMember("operation", "WRITE", d.GetAllocator());
  commit_info.AddMember("timestamp", LHHelpers::now_ms(), d.GetAllocator());

  d.AddMember("commitInfo", commit_info, d.GetAllocator());

  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  d.Accept(writer);

  return buffer.GetString();
}
