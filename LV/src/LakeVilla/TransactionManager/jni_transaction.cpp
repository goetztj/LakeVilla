#include <arrow/api.h>
#include <arrow/io/api.h>
#include <arrow/io/memory.h>
#include <arrow/ipc/api.h>
#include <arrow/ipc/writer.h>
#include <jni.h>

#include <Settings/LvSettings.hpp>
#include <memory>
#include <mutex>

#include "Connectors/MinIOConnector.hpp"
#include "ManagerDeltaLake.hpp"

using namespace LHTransactions;

namespace {

// The AWS SDK must be initialised once per process and shut down only after
// the last client is gone: reference count the managers.
std::mutex sdk_mutex;
uint32_t sdk_users = 0;
Aws::SDKOptions options;

void acquire_sdk() {
  std::lock_guard<std::mutex> guard(sdk_mutex);
  if (sdk_users++ == 0) {
    Aws::InitAPI(options);
  }
}

void release_sdk() {
  std::lock_guard<std::mutex> guard(sdk_mutex);
  if (sdk_users > 0 && --sdk_users == 0) {
    Aws::ShutdownAPI(options);
  }
}

// C++ exceptions must not cross the JNI boundary
void throw_java(JNIEnv* env, const char* message) {
  jclass exception = env->FindClass("java/lang/RuntimeException");
  if (exception) {
    env->ThrowNew(exception, message);
  }
}

std::string to_string(JNIEnv* env, jstring value) {
  const char* chars = env->GetStringUTFChars(value, nullptr);
  std::string result(chars);
  env->ReleaseStringUTFChars(value, chars);
  return result;
}

TransactionManagerDeltaLake* manager(jlong handle) {
  return reinterpret_cast<TransactionManagerDeltaLake*>(handle);
}

}  // namespace

extern "C" JNIEXPORT jlong JNICALL Java_site_ycsb_db_LakeVillaTransactionManager_createNative(
    JNIEnv* env, jobject, jbooleanArray jLevels, jstring jPath,
    jstring jConfig_path, jint transactionId) {
  try {
    std::string pathToConfig = to_string(env, jConfig_path);
    auto settings = std::make_unique<LHConfig::LvSettings>(pathToConfig);
    if (!settings->parse()) {
      throw_java(env, ("unable to read LakeVilla config " + pathToConfig).c_str());
      return 0;
    }

    jsize len = env->GetArrayLength(jLevels);
    std::vector<bool> levels(len);
    jboolean* elements = env->GetBooleanArrayElements(jLevels, nullptr);
    for (jsize i = 0; i < len; ++i) {
      levels[i] = elements[i];
    }
    env->ReleaseBooleanArrayElements(jLevels, elements, JNI_ABORT);

    std::string pathToTable = to_string(env, jPath);

    StorageConnector::MinIOConfig config2;
    config2.endpointURI = settings->host;
    config2.user = settings->user;
    config2.passwd = settings->passwd;
    config2.region = settings->awsregion;
    config2.connectTimeout = 60000;
    config2.requestTimeout = 10000;
    config2.default_bucket = settings->bucket;

    acquire_sdk();
    auto tm = std::make_unique<TransactionManagerDeltaLake>(
        levels, pathToTable, config2, transactionId);
    if (!settings->pathToGlobalLog.empty() &&
        !settings->pathToGlobalTmpLog.empty()) {
      tm->set_global_log_paths(settings->pathToGlobalLog,
                               settings->pathToGlobalTmpLog);
    }

    return reinterpret_cast<jlong>(tm.release());
  } catch (const std::exception& e) {
    throw_java(env, e.what());
    return 0;
  }
}

extern "C" JNIEXPORT void JNICALL
Java_site_ycsb_db_LakeVillaTransactionManager_destroyNative(JNIEnv* env, jobject, jlong handle) {
  if (!handle) {
    return;
  }
  try {
    delete manager(handle);
  } catch (const std::exception& e) {
    throw_java(env, e.what());
  }
  release_sdk();
}

extern "C" JNIEXPORT void JNICALL
Java_site_ycsb_db_LakeVillaTransactionManager_openTable(JNIEnv* env, jobject, jlong handle, jstring jPath) {
  try {
    std::string pathToTable = to_string(env, jPath);
    if (!manager(handle)->open_new_table(pathToTable)) {
      throw_java(env, ("unable to open table " + pathToTable).c_str());
    }
  } catch (const std::exception& e) {
    throw_java(env, e.what());
  }
}

extern "C" JNIEXPORT jboolean JNICALL
Java_site_ycsb_db_LakeVillaTransactionManager_beginTransaction(JNIEnv* env, jobject, jlong handle) {
  try {
    return manager(handle)->begin_transaction_ycsb();
  } catch (const std::exception& e) {
    throw_java(env, e.what());
    return false;
  }
}

extern "C" JNIEXPORT jbyteArray JNICALL
Java_site_ycsb_db_LakeVillaTransactionManager_readTableAsBytes(JNIEnv* env, jobject,
                                               jlong handle, jint tableId) {
  try {
    auto table = manager(handle)->read_table(static_cast<uint32_t>(tableId));
    if (!table) return nullptr;

    auto b =
        arrow::io::BufferOutputStream::Create(0, arrow::default_memory_pool());
    if (!b.ok()) return nullptr;

    auto buffer_stream = *b;

    arrow::TableBatchReader reader(*table);

    auto w = arrow::ipc::MakeStreamWriter(
        std::static_pointer_cast<arrow::io::OutputStream>(buffer_stream),
        table->schema());

    if (!w.ok()) return nullptr;

    auto writer = *w;

    std::shared_ptr<arrow::RecordBatch> batch;
    while (true) {
      auto status = reader.ReadNext(&batch);
      if (!status.ok()) return nullptr;
      if (!batch) break;
      status = writer->WriteRecordBatch(*batch);
      if (!status.ok()) return nullptr;
    }
    if (!writer->Close().ok()) return nullptr;

    auto buffer = buffer_stream->Finish();

    if (!buffer.ok()) return nullptr;

    jbyteArray result = env->NewByteArray(static_cast<jsize>((*buffer)->size()));
    env->SetByteArrayRegion(result, 0, static_cast<jsize>((*buffer)->size()),
                            reinterpret_cast<const jbyte*>((*buffer)->data()));

    return result;
  } catch (const std::exception& e) {
    throw_java(env, e.what());
    return nullptr;
  }
}

extern "C" JNIEXPORT jboolean JNICALL
Java_site_ycsb_db_LakeVillaTransactionManager_writeToTableNative(JNIEnv* env, jobject, jlong handle,
                                           jint tableId,
                                           jbyteArray arrowTableBytes) {
  if (arrowTableBytes == nullptr) {
    return false;
  }

  try {
    // copy the bytes: nothing has to be released on early returns
    jsize len = env->GetArrayLength(arrowTableBytes);
    auto bytes = std::make_shared<std::vector<uint8_t>>(len);
    env->GetByteArrayRegion(arrowTableBytes, 0, len,
                            reinterpret_cast<jbyte*>(bytes->data()));

    auto buffer = std::make_shared<arrow::Buffer>(bytes->data(),
                                                  static_cast<int64_t>(len));

    auto input = std::make_shared<arrow::io::BufferReader>(buffer);

    auto openResult = arrow::ipc::RecordBatchStreamReader::Open(input);
    if (!openResult.ok()) {
      return false;
    }
    std::shared_ptr<arrow::ipc::RecordBatchReader> batchReader = *openResult;

    std::vector<std::shared_ptr<arrow::RecordBatch>> batches;
    while (true) {
      std::shared_ptr<arrow::RecordBatch> batch;
      auto status = batchReader->ReadNext(&batch);
      if (!status.ok()) {
        return false;
      }
      if (!batch) break;
      batches.push_back(batch);
    }

    auto tableResult = arrow::Table::FromRecordBatches(batches);
    if (!tableResult.ok()) {
      return false;
    }
    std::shared_ptr<arrow::Table> table = *tableResult;

    if (table->num_columns() <= 1 || table->column(1)->num_chunks() == 0 ||
        table->column(1)->type()->id() != arrow::Type::STRING) {
      return false;
    }

    auto string_array =
        std::static_pointer_cast<arrow::StringArray>(table->column(1)->chunk(0));

    if (string_array->length() == 0) {
      return false;
    }

    std::string update_val = "";
    if (!string_array->IsNull(0)) {
      update_val = string_array->GetString(0);
    }

    std::vector<std::pair<std::string, std::string>> update_vals;
    update_vals.push_back({"value", update_val});
    auto prev_id = manager(handle)->register_update_pairs(
        update_vals, LHTransactions::UpdateOperations::REP, update_vals);

    return manager(handle)->add_file(static_cast<uint32_t>(tableId), table,
                                     "value", prev_id);
  } catch (const std::exception& e) {
    throw_java(env, e.what());
    return false;
  }
}

extern "C" JNIEXPORT void JNICALL Java_site_ycsb_db_LakeVillaTransactionManager_commitNative(
    JNIEnv* env, jobject, jlong handle, jboolean readOnly) {
  try {
    manager(handle)->commit(readOnly);
  } catch (const std::exception& e) {
    throw_java(env, e.what());
  }
}
