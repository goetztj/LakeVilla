#include "jni_common.hpp"

#include <arrow/io/memory.h>
#include <arrow/ipc/api.h>
#include <aws/core/Aws.h>

#include <mutex>
#include <vector>

namespace {
std::mutex sdk_mutex;
uint32_t sdk_users = 0;
Aws::SDKOptions sdk_options;
}  // namespace

void LHJni::acquire_sdk() {
  std::lock_guard<std::mutex> guard(sdk_mutex);
  if (sdk_users++ == 0) {
    Aws::InitAPI(sdk_options);
  }
}

void LHJni::release_sdk() {
  std::lock_guard<std::mutex> guard(sdk_mutex);
  if (sdk_users > 0 && --sdk_users == 0) {
    Aws::ShutdownAPI(sdk_options);
  }
}

void LHJni::throw_java(JNIEnv* env, const std::string& message,
                       const char* exception_class) {
  if (env->ExceptionCheck()) {
    // keep the pending exception (e.g. an OutOfMemoryError of the JVM)
    return;
  }
  jclass exception = env->FindClass(exception_class);
  if (!exception) {
    exception = env->FindClass("java/lang/RuntimeException");
  }
  if (exception) {
    env->ThrowNew(exception, message.c_str());
  }
}

std::string LHJni::to_string(JNIEnv* env, jstring value) {
  if (!value) {
    return "";
  }
  const char* chars = env->GetStringUTFChars(value, nullptr);
  if (!chars) {
    return "";
  }
  std::string result(chars);
  env->ReleaseStringUTFChars(value, chars);
  return result;
}

jbyteArray LHJni::to_byte_array(JNIEnv* env, const uint8_t* data,
                                int64_t size) {
  if (size > INT32_MAX) {
    throw_java(env, "result of " + std::to_string(size) +
                        " bytes exceeds the maximum size of a Java array");
    return nullptr;
  }
  jbyteArray result = env->NewByteArray(static_cast<jsize>(size));
  if (!result) {
    return nullptr;
  }
  env->SetByteArrayRegion(result, 0, static_cast<jsize>(size),
                          reinterpret_cast<const jbyte*>(data));
  return result;
}

std::shared_ptr<arrow::Buffer> LHJni::table_to_ipc(const arrow::Table& table,
                                                   std::string* error) {
  auto fail = [error](const arrow::Status& status) {
    if (error) {
      *error = status.ToString();
    }
    return nullptr;
  };
  auto stream = arrow::io::BufferOutputStream::Create();
  if (!stream.ok()) {
    return fail(stream.status());
  }
  auto writer = arrow::ipc::MakeStreamWriter(*stream, table.schema());
  if (!writer.ok()) {
    return fail(writer.status());
  }
  arrow::TableBatchReader reader(table);
  std::shared_ptr<arrow::RecordBatch> batch;
  while (true) {
    auto status = reader.ReadNext(&batch);
    if (!status.ok()) {
      return fail(status);
    }
    if (!batch) {
      break;
    }
    status = (*writer)->WriteRecordBatch(*batch);
    if (!status.ok()) {
      return fail(status);
    }
  }
  auto status = (*writer)->Close();
  if (!status.ok()) {
    return fail(status);
  }
  auto buffer = (*stream)->Finish();
  if (!buffer.ok()) {
    return fail(buffer.status());
  }
  return *buffer;
}

std::shared_ptr<arrow::Buffer> LHJni::schema_to_ipc(const arrow::Schema& schema,
                                                    std::string* error) {
  auto empty = arrow::Table::MakeEmpty(std::make_shared<arrow::Schema>(schema));
  if (!empty.ok()) {
    if (error) {
      *error = empty.status().ToString();
    }
    return nullptr;
  }
  return table_to_ipc(**empty, error);
}

std::shared_ptr<arrow::Table> LHJni::ipc_to_table(JNIEnv* env,
                                                  jbyteArray bytes,
                                                  std::string* error) {
  auto fail = [error](const std::string& message) {
    if (error) {
      *error = message;
    }
    return nullptr;
  };
  if (!bytes) {
    return fail("no Arrow data");
  }
  // copy the bytes: the Arrow buffers must not reference the Java array
  jsize length = env->GetArrayLength(bytes);
  auto allocated = arrow::AllocateBuffer(length);
  if (!allocated.ok()) {
    return fail(allocated.status().ToString());
  }
  std::shared_ptr<arrow::Buffer> buffer = std::move(*allocated);
  env->GetByteArrayRegion(bytes, 0, length,
                          reinterpret_cast<jbyte*>(buffer->mutable_data()));

  auto input = std::make_shared<arrow::io::BufferReader>(buffer);
  auto reader = arrow::ipc::RecordBatchStreamReader::Open(input);
  if (!reader.ok()) {
    return fail(reader.status().ToString());
  }
  auto table = (*reader)->ToTable();
  if (!table.ok()) {
    return fail(table.status().ToString());
  }
  return *table;
}
