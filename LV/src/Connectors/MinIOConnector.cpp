#include "MinIOConnector.hpp"

#include <aws/core/Aws.h>
#include <aws/core/auth/AWSCredentialsProviderChain.h>
#include <aws/s3/S3Client.h>
#include <aws/s3/model/DeleteObjectRequest.h>
#include <aws/s3/model/DeleteObjectsRequest.h>
#include <aws/s3/model/GetObjectRequest.h>
#include <aws/s3/model/HeadObjectRequest.h>
#include <aws/s3/model/ListObjectsRequest.h>
#include <aws/s3/model/ListObjectsV2Request.h>
#include <aws/s3/model/Object.h>
#include <aws/s3/model/PutObjectRequest.h>

#include <fstream>
#include <iostream>

std::atomic<bool> StorageConnector::MinIOConnector::creating_client(false);

StorageConnector::MinIOConnector::MinIOConnector(MinIOConfig* config)
    : Connector(StorageConnector::ConnectorType::MinIO) {
  this->default_bucket = config->default_bucket;

  if (config) {
    this->config = *config;
  } else {
    std::cerr << "no config provided" << std::endl;
  }

  while (this->creating_client.exchange(true)) {
  }

  Aws::Client::ClientConfiguration c_config;
  c_config.region = config->region;
  c_config.verifySSL = config->verifySSL;
  c_config.connectTimeoutMs = config->connectTimeout;
  c_config.requestTimeoutMs = config->connectTimeout;
  c_config.endpointOverride = config->endpointURI;
  c_config.scheme = config->scheme;

  Aws::Auth::AWSCredentials cred(config->user, config->passwd);
  this->minio_client = std::make_unique<Aws::S3::S3Client>(
      cred, c_config, Aws::Client::AWSAuthV4Signer::PayloadSigningPolicy::Never,
      false);

  this->creating_client.store(false);
}

bool StorageConnector::MinIOConnector::write(std::string path,
                                             std::string file) {
  std::cout << path << std::endl;
  std::cout << file << std::endl;
  bool success = false;

  Aws::S3::Model::PutObjectRequest request;

  request.SetBucket(this->default_bucket);
  request.SetKey(path);

  std::shared_ptr<Aws::IOStream> input = Aws::MakeShared<Aws::FStream>(
      "insert", file.c_str(), std::ios_base::in | std::ios_base::binary);

  if (!input) {
    return false;
  }

  request.SetBody(input);

  auto outcome = minio_client->PutObject(request);

  success = outcome.IsSuccess();

  if (success) {
    this->bytes_write += file.size();
  }

  return success;
}

bool StorageConnector::MinIOConnector::write(std::string path, const char* data,
                                             uint64_t size) {
  bool success = false;
  Aws::S3::Model::PutObjectRequest request;

  request.SetBucket(this->default_bucket);
  request.SetKey(path);

  const std::shared_ptr<Aws::IOStream> input =
      Aws::MakeShared<Aws::StringStream>("");

  for (size_t i = 0; i < size; i++) {
    *input << data[i];
  }

  request.SetBody(input);
  request.SetContentLength(size);

  auto outcome = minio_client->PutObject(request);

  if (!outcome.IsSuccess()) {
    std::cout << "Error: " << outcome.GetError().GetMessage() << std::endl;
  }
  success = outcome.IsSuccess();

  if (success) {
    this->bytes_write += size;
  }

  return success;
}

bool StorageConnector::MinIOConnector::write_atomic(std::string path,
                                                    const char* data,
                                                    uint64_t size) {
  bool success = false;
  Aws::S3::Model::PutObjectRequest request;

  request.SetBucket(this->default_bucket);
  request.SetKey(path);

  request.SetIfNoneMatch("*");

  const std::shared_ptr<Aws::IOStream> input =
      Aws::MakeShared<Aws::StringStream>("");

  for (size_t i = 0; i < size; i++) {
    *input << data[i];
  }

  request.SetBody(input);
  request.SetContentLength(size);

  auto outcome = minio_client->PutObject(request);

  success = outcome.IsSuccess();

  if (success) {
    this->bytes_write += size;
  }

  return success;
}

std::pair<char*, uint64_t> StorageConnector::MinIOConnector::read(
    std::string path) {
  std::pair<char*, uint64_t> result_pair;

  Aws::S3::Model::GetObjectRequest request;
  request.SetBucket(this->default_bucket);
  request.SetKey(path);

  auto outcome = minio_client->GetObject(request);

  if (outcome.IsSuccess()) {
    auto& result = outcome.GetResult().GetBody();
    auto result_size = outcome.GetResult().GetContentLength();

    char* result_array = (char*)std::malloc(result_size);

    result.read(result_array, result_size);

    result_pair = {result_array, result_size};
  } else {
    std::cout << "GetObject error: " << outcome.GetError().GetExceptionName()
              << std::endl
              << outcome.GetError().GetMessage() << std::endl;
    result_pair = {nullptr, 0};
  }

  this->bytes_read += result_pair.second;

  return result_pair;
}

std::unique_ptr<StorageConnector::Container>
StorageConnector::MinIOConnector::read2(const std::string& path) {
  auto result_ptr = std::make_unique<StorageConnector::Container>();

  Aws::S3::Model::GetObjectRequest request;
  request.SetBucket(this->default_bucket);
  request.SetKey(path);

  auto outcome = minio_client->GetObject(request);

  if (outcome.IsSuccess()) {
    auto& result = outcome.GetResult().GetBody();
    auto result_size = outcome.GetResult().GetContentLength();

    result_ptr->data = (char*)std::malloc(result_size);
    result_ptr->length = result_size;

    result.read(result_ptr->data, result_size);
  }
  this->bytes_read += result_ptr->length;
  return result_ptr;
}

void StorageConnector::MinIOConnector::read_mock(const std::string& path) {
  Aws::S3::Model::GetObjectRequest request;
  request.SetBucket(this->default_bucket);
  request.SetKey(path);

  auto outcome = minio_client->GetObject(request);

  if (outcome.IsSuccess()) {
    this->bytes_read += outcome.GetResult().GetContentLength();
  } else {
    std::cerr << "error reading " << path << std::endl;
  }
}

std::unique_ptr<StorageConnector::Container>
StorageConnector::MinIOConnector::list2(std::string& path) {
  auto result = std::make_unique<StorageConnector::Container>();
  Aws::S3::Model::ListObjectsV2Request request;

  request.WithBucket(this->config.default_bucket);
  request.WithPrefix(path);

  auto outcome = minio_client->ListObjectsV2(request);

  if (outcome.IsSuccess()) {
    auto objects = outcome.GetResult().GetContents();

    std::stringstream stream;

    for (auto& obj : objects) {
      stream << obj.GetKey() << "\n";
    }

    auto result_string = stream.str();
    result->data = (char*)std::malloc(result_string.size() * sizeof(char));

    std::memcpy(result->data, result_string.c_str(), result_string.size());
    result->length = result_string.size();
  } else {
    std::cout << "error listing objects: "
              << outcome.GetError().GetExceptionName() << std::endl;
  }

  this->bytes_read += result->length;

  return result;
}

Aws::S3::Model::ListObjectsV2Outcome
StorageConnector::MinIOConnector::list2_vec(const std::string& path) {
  Aws::S3::Model::ListObjectsV2Outcome result;

  Aws::S3::Model::ListObjectsV2Request request;

  request.WithBucket(this->config.default_bucket);
  request.WithPrefix(path);

  result = minio_client->ListObjectsV2(request);
  if (result.IsSuccess()) {
    this->bytes_read += result.GetResult().GetContents().size() *
                        result.GetResult().GetContents().at(0).GetSize();
  }
  return result;
}

std::pair<char*, uint64_t> StorageConnector::MinIOConnector::list(
    std::string& path) {
  std::pair<char*, uint64_t> result;

  Aws::S3::Model::ListObjectsV2Request request;

  request.WithBucket(this->config.default_bucket);
  request.WithPrefix(path);

  auto outcome = minio_client->ListObjectsV2(request);

  if (outcome.IsSuccess()) {
    auto objects = outcome.GetResult().GetContents();

    std::stringstream stream;

    for (auto& obj : objects) {
      stream << obj.GetKey() << "\n";
    }

    auto result_string = stream.str();
    char* data = (char*)std::malloc(result_string.size() * sizeof(char));

    std::memcpy(data, result_string.c_str(), result_string.size());

    result = {data, result_string.size()};
  } else {
    std::cout << "error listing objects" << std::endl;
    result = {nullptr, 0};
  }

  this->bytes_read += result.second;

  return result;
}

bool StorageConnector::MinIOConnector::check(const std::string& path) {
  Aws::S3::Model::HeadObjectRequest request;
  request.SetBucket(this->default_bucket);
  request.SetKey(path);

  auto outcome = minio_client->HeadObject(request);

  if (outcome.IsSuccess()) {
    this->bytes_read += outcome.GetResult().GetContentLength();
  }

  return outcome.IsSuccess();
}

bool StorageConnector::MinIOConnector::delete_file(std::string path) {
  Aws::S3::Model::DeleteObjectRequest request;

  request.WithBucket(this->default_bucket);
  request.WithKey(path);

  auto outcome = minio_client->DeleteObject(request);

  if (!outcome.IsSuccess()) {
    std::cout << "error: " << outcome.GetError().GetExceptionName()
              << std::endl;
  }

  if (outcome.IsSuccess()) {
    this->bytes_write += path.size();
  }

  return outcome.IsSuccess();
}

void StorageConnector::MinIOConnector::delete_sub_directory(std::string path,
                                                            uint32_t until) {
  Aws::S3::Model::DeleteObjectsRequest request;
  Aws::S3::Model::Delete deleteObj;

  for (uint32_t log_id = 0; log_id < until; log_id++) {
    std::stringstream sublog_key;
    sublog_key << path << "00" << log_id << ".sub";

    deleteObj.AddObjects(
        Aws::S3::Model::ObjectIdentifier().WithKey(sublog_key.str()));

    this->bytes_write += sublog_key.str().size();
  }

  request.SetDelete(deleteObj);
  request.SetBucket(this->default_bucket);

  while (true) {
    auto outcome = minio_client->DeleteObjects(request);

    if (outcome.IsSuccess()) {
      break;
    }
  }
}

StorageConnector::MinIOConnector::~MinIOConnector() {
  this->minio_client = nullptr;
}