#include "MinIOConnector.hpp"

#include <aws/core/Aws.h>
#include <aws/core/auth/AWSCredentialsProviderChain.h>
#include <aws/core/http/HttpResponse.h>
#include <aws/s3/S3Client.h>
#include <aws/s3/model/DeleteObjectRequest.h>
#include <aws/s3/model/DeleteObjectsRequest.h>
#include <aws/s3/model/GetObjectRequest.h>
#include <aws/s3/model/HeadBucketRequest.h>
#include <aws/s3/model/HeadObjectRequest.h>
#include <aws/s3/model/ListObjectsV2Request.h>
#include <aws/s3/model/Object.h>
#include <aws/s3/model/PutObjectRequest.h>

#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>

// S3 allows at most 1000 keys per DeleteObjects request
static constexpr uint32_t MAX_DELETE_KEYS = 1000;
static constexpr uint32_t MAX_DELETE_RETRIES = 10;

std::mutex StorageConnector::MinIOConnector::creating_client;

StorageConnector::MinIOConnector::MinIOConnector(MinIOConfig* config)
    : Connector(StorageConnector::ConnectorType::MinIO) {
  if (config) {
    this->config = *config;
  } else {
    std::cerr << "no config provided; using defaults" << std::endl;
  }
  this->default_bucket = this->config.default_bucket;

  // region and endpoint are configured explicitly: do not probe the EC2
  // instance metadata service (costs ~2 s per client outside of EC2)
  Aws::Client::ClientConfigurationInitValues init_values;
  init_values.shouldDisableIMDS = true;
  Aws::Client::ClientConfiguration c_config(init_values);
  c_config.region = this->config.region;
  c_config.verifySSL = this->config.verifySSL;
  c_config.connectTimeoutMs = this->config.connectTimeout;
  c_config.requestTimeoutMs = this->config.requestTimeout;
  c_config.endpointOverride = this->config.endpointURI;
  c_config.scheme = this->config.use_https ? Aws::Http::Scheme::HTTPS
                                           : Aws::Http::Scheme::HTTP;

  Aws::Auth::AWSCredentials cred(this->config.user, this->config.passwd);

  std::lock_guard<std::mutex> guard(creating_client);
  this->minio_client = std::make_unique<Aws::S3::S3Client>(
      cred, c_config, Aws::Client::AWSAuthV4Signer::PayloadSigningPolicy::Never,
      false);
}

bool StorageConnector::MinIOConnector::write(std::string path,
                                             std::string file) {
  std::shared_ptr<Aws::IOStream> input = Aws::MakeShared<Aws::FStream>(
      "insert", file.c_str(), std::ios_base::in | std::ios_base::binary);

  if (!input || !input->good()) {
    std::cerr << "unable to open local file " << file << std::endl;
    return false;
  }

  input->seekg(0, std::ios_base::end);
  auto size = static_cast<uint64_t>(input->tellg());
  input->seekg(0, std::ios_base::beg);

  Aws::S3::Model::PutObjectRequest request;
  request.SetBucket(this->default_bucket);
  request.SetKey(path);
  request.SetBody(input);
  request.SetContentLength(size);

  auto outcome = minio_client->PutObject(request);

  if (!outcome.IsSuccess()) {
    std::cerr << "PutObject error for " << path << ": "
              << outcome.GetError().GetMessage() << std::endl;
    return false;
  }

  this->bytes_write += size;
  return true;
}

bool StorageConnector::MinIOConnector::write(std::string path, const char* data,
                                             uint64_t size) {
  Aws::S3::Model::PutObjectRequest request;

  request.SetBucket(this->default_bucket);
  request.SetKey(path);

  const std::shared_ptr<Aws::IOStream> input =
      Aws::MakeShared<Aws::StringStream>("");
  input->write(data, static_cast<std::streamsize>(size));

  request.SetBody(input);
  request.SetContentLength(size);

  auto outcome = minio_client->PutObject(request);

  if (!outcome.IsSuccess()) {
    std::cerr << "PutObject error for " << path << ": "
              << outcome.GetError().GetMessage() << std::endl;
    return false;
  }

  this->bytes_write += size;
  return true;
}

StorageConnector::WriteStatus StorageConnector::MinIOConnector::write_atomic(
    std::string path, const char* data, uint64_t size, std::string* etag) {
  Aws::S3::Model::PutObjectRequest request;

  request.SetBucket(this->default_bucket);
  request.SetKey(path);

  request.SetIfNoneMatch("*");

  const std::shared_ptr<Aws::IOStream> input =
      Aws::MakeShared<Aws::StringStream>("");
  input->write(data, static_cast<std::streamsize>(size));

  request.SetBody(input);
  request.SetContentLength(size);

  auto outcome = minio_client->PutObject(request);

  if (outcome.IsSuccess()) {
    this->bytes_write += size;
    if (etag) {
      *etag = outcome.GetResult().GetETag();
    }
    return WriteStatus::Ok;
  }

  // 412: the object exists already; 409 (ConditionalRequestConflict) means a
  // concurrent conditional write is in flight and the request may be retried
  if (outcome.GetError().GetResponseCode() ==
      Aws::Http::HttpResponseCode::PRECONDITION_FAILED) {
    return WriteStatus::AlreadyExists;
  }

  std::cerr << "conditional PutObject error for " << path << ": "
            << outcome.GetError().GetMessage() << std::endl;
  return WriteStatus::Error;
}

std::pair<char*, uint64_t> StorageConnector::MinIOConnector::read(
    std::string path) {
  auto container = this->read2(path);
  if (!container->data) {
    return {nullptr, 0};
  }

  std::pair<char*, uint64_t> result = {container->data, container->length};
  // ownership moves to the caller
  container->data = nullptr;
  container->length = 0;
  return result;
}

std::unique_ptr<StorageConnector::Container>
StorageConnector::MinIOConnector::read2(const std::string& path) {
  ObjectVersion version;
  return this->read_versioned(path, version);
}

StorageConnector::WriteStatus StorageConnector::MinIOConnector::write_if_match(
    const std::string& path, const char* data, uint64_t size,
    const std::string& etag) {
  Aws::S3::Model::PutObjectRequest request;

  request.SetBucket(this->default_bucket);
  request.SetKey(path);
  request.SetIfMatch(etag);

  const std::shared_ptr<Aws::IOStream> input =
      Aws::MakeShared<Aws::StringStream>("");
  input->write(data, static_cast<std::streamsize>(size));

  request.SetBody(input);
  request.SetContentLength(size);

  auto outcome = minio_client->PutObject(request);

  if (outcome.IsSuccess()) {
    this->bytes_write += size;
    return WriteStatus::Ok;
  }

  auto code = outcome.GetError().GetResponseCode();
  // 412: the ETag changed; 404: the object is gone
  if (code == Aws::Http::HttpResponseCode::PRECONDITION_FAILED ||
      code == Aws::Http::HttpResponseCode::NOT_FOUND) {
    return WriteStatus::PreconditionFailed;
  }

  std::cerr << "conditional PutObject (If-Match) error for " << path << ": "
            << outcome.GetError().GetMessage() << std::endl;
  return WriteStatus::Error;
}

std::unique_ptr<StorageConnector::Container>
StorageConnector::MinIOConnector::read_versioned(const std::string& path,
                                                 ObjectVersion& version) {
  auto result_ptr = std::make_unique<StorageConnector::Container>();
  version = {};

  Aws::S3::Model::GetObjectRequest request;
  request.SetBucket(this->default_bucket);
  request.SetKey(path);

  auto outcome = minio_client->GetObject(request);

  if (outcome.IsSuccess()) {
    auto& result = outcome.GetResult().GetBody();
    auto result_size = outcome.GetResult().GetContentLength();

    // allocate at least one byte so that empty objects are distinguishable
    // from failed reads (data != nullptr)
    result_ptr->data =
        (char*)std::malloc(result_size > 0 ? result_size : 1);
    result_ptr->length = result_size;

    result.read(result_ptr->data, result_size);
    if (result.gcount() != result_size) {
      std::cerr << "GetObject: short read for " << path << std::endl;
      std::free(result_ptr->data);
      result_ptr->data = nullptr;
      result_ptr->length = 0;
    }
    if (result_ptr->data) {
      version.etag = outcome.GetResult().GetETag();
      version.last_modified_ms = outcome.GetResult().GetLastModified().Millis();
    }
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

std::optional<std::vector<StorageConnector::ObjectInfo>>
StorageConnector::MinIOConnector::list_objects(const std::string& prefix) {
  std::vector<ObjectInfo> result;

  Aws::S3::Model::ListObjectsV2Request request;
  request.WithBucket(this->default_bucket);
  request.WithPrefix(prefix);

  while (true) {
    auto outcome = minio_client->ListObjectsV2(request);

    if (!outcome.IsSuccess()) {
      std::cerr << "error listing objects: "
                << outcome.GetError().GetExceptionName() << std::endl;
      return std::nullopt;
    }

    for (const auto& obj : outcome.GetResult().GetContents()) {
      result.push_back({obj.GetKey(), static_cast<uint64_t>(obj.GetSize())});
      this->bytes_read += obj.GetKey().size();
    }

    if (!outcome.GetResult().GetIsTruncated()) {
      break;
    }
    request.SetContinuationToken(
        outcome.GetResult().GetNextContinuationToken());
  }

  return result;
}

std::unique_ptr<StorageConnector::Container>
StorageConnector::MinIOConnector::list2(std::string& path) {
  auto result = std::make_unique<StorageConnector::Container>();

  auto objects = this->list_objects(path);
  if (!objects) {
    return result;
  }

  std::stringstream stream;
  for (auto& obj : *objects) {
    stream << obj.key << "\n";
  }

  auto result_string = stream.str();
  result->data = (char*)std::malloc(result_string.size() + 1);
  std::memcpy(result->data, result_string.c_str(), result_string.size());
  result->length = result_string.size();

  return result;
}

std::pair<char*, uint64_t> StorageConnector::MinIOConnector::list(
    std::string& path) {
  auto container = this->list2(path);
  if (!container->data) {
    return {nullptr, 0};
  }

  std::pair<char*, uint64_t> result = {container->data, container->length};
  container->data = nullptr;
  container->length = 0;
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
    std::cerr << "error deleting " << path << ": "
              << outcome.GetError().GetExceptionName() << std::endl;
    return false;
  }

  this->bytes_write += path.size();
  return true;
}

void StorageConnector::MinIOConnector::delete_sub_directory(std::string path,
                                                            uint32_t until) {
  for (uint32_t batch_start = 0; batch_start < until;
       batch_start += MAX_DELETE_KEYS) {
    Aws::S3::Model::DeleteObjectsRequest request;
    Aws::S3::Model::Delete deleteObj;

    uint32_t batch_end = std::min(until, batch_start + MAX_DELETE_KEYS);
    for (uint32_t log_id = batch_start; log_id < batch_end; log_id++) {
      std::stringstream sublog_key;
      sublog_key << path << "00" << log_id << ".sub";

      deleteObj.AddObjects(
          Aws::S3::Model::ObjectIdentifier().WithKey(sublog_key.str()));

      this->bytes_write += sublog_key.str().size();
    }

    request.SetDelete(deleteObj);
    request.SetBucket(this->default_bucket);

    bool deleted = false;
    for (uint32_t attempt = 0; attempt < MAX_DELETE_RETRIES; attempt++) {
      auto outcome = minio_client->DeleteObjects(request);

      if (outcome.IsSuccess()) {
        deleted = true;
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10 << attempt));
    }

    if (!deleted) {
      std::cerr << "unable to delete sublogs below " << path << std::endl;
    }
  }
}

void StorageConnector::MinIOConnector::sample_store_clock() {
  Aws::S3::Model::HeadBucketRequest request;
  request.SetBucket(this->default_bucket);

  std::string date;
  request.SetHeadersReceivedEventHandler(
      [&date](const Aws::Http::HttpRequest*, Aws::Http::HttpResponse* response) {
        if (response && response->HasHeader("date")) {
          date = response->GetHeader("date");
        }
      });

  auto sent = local_time_ms();
  minio_client->HeadBucket(request);
  auto received = local_time_ms();

  if (date.empty()) {
    return;
  }
  Aws::Utils::DateTime store_date(Aws::String(date),
                                  Aws::Utils::DateFormat::RFC822);
  if (store_date.WasParseSuccessful()) {
    this->clock.add_sample(store_date.Millis(), sent, received);
  }
}

StorageConnector::StoreTime StorageConnector::MinIOConnector::store_time() {
  // a few samples per second narrow the estimate (second boundaries)
  if (!this->clock.has_samples() ||
      local_time_ms() - this->clock.last_sample_local_ms() > 250) {
    this->sample_store_clock();
  }
  return this->clock.now(local_time_ms());
}

StorageConnector::MinIOConnector::~MinIOConnector() {
  this->minio_client = nullptr;
}
