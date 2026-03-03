#include "Marker.hpp"

LHTransactions::Marker::Marker(
    std::string&& marker_path,
    StorageConnector::MinIOConnector* connector_ptr) {
  this->marker_path = marker_path;
  this->connector_ptr = connector_ptr;
  this->valid = true;
}

LHTransactions::Marker::~Marker() {
  if (this->valid) {
    std::string log_header = "";
    this->createHeaderCommit(log_header);

    this->connector_ptr->write(this->marker_path, log_header.c_str(),
                               log_header.size());
  }
}

void LHTransactions::Marker::setInvalid() { this->valid = false; }

void LHTransactions::Marker::createHeaderCommit(std::string& out_string) {
  rapidjson::Document d;
  d.SetObject();

  rapidjson::Value operation_para;
  rapidjson::Value operation_metrics;

  operation_para.SetObject();
  operation_metrics.SetObject();

  operation_para.AddMember("mode", "MultiQuery", d.GetAllocator());
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
  commit_info.AddMember("timestamp", 0, d.GetAllocator());

  d.AddMember("commitInfo", commit_info, d.GetAllocator());

  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  d.Accept(writer);

  out_string = buffer.GetString();
}