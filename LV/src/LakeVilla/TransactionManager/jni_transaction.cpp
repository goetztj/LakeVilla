#include <arrow/api.h>
#include <arrow/io/api.h>
#include <arrow/io/memory.h>
#include <arrow/ipc/api.h>
#include <arrow/ipc/writer.h>
#include <jni.h>

#include <Settings/LvSettings.hpp>
#include <memory>

#include "ManagerGeneric.hpp"

using namespace LHTransactions;

Aws::SDKOptions options;

extern "C" JNIEXPORT jlong JNICALL Java_site_ycsb_db_LakeVillaTransactionManager_createNative(
    JNIEnv* env, jobject, jbooleanArray jLevels, jstring jPath,
    jstring jConfig_path, jint transactionId) {

  Aws::InitAPI(options);

  const char* pathCLVConfig = env->GetStringUTFChars(jConfig_path, nullptr);
  std::string pathToConfig(pathCLVConfig);
  env->ReleaseStringUTFChars(jConfig_path, pathCLVConfig);
  auto settings = std::make_unique<LHConfig::LvSettings>(pathToConfig);

  settings->parse();

  jsize len = env->GetArrayLength(jLevels);
  std::vector<bool> levels(len);
  jboolean* elements = env->GetBooleanArrayElements(jLevels, nullptr);
  for (jsize i = 0; i < len; ++i) {
    levels[i] = elements[i];
  }
  env->ReleaseBooleanArrayElements(jLevels, elements, JNI_ABORT);

  const char* pathC = env->GetStringUTFChars(jPath, nullptr);
  std::string pathToTable(pathC);
  env->ReleaseStringUTFChars(jPath, pathC);

  StorageConnector::MinIOConfig config2;
  config2.endpointURI = settings->host;
  config2.user = settings->user;
  config2.passwd = settings->passwd;
  config2.region = "";
  config2.connectTimeout = 60000;
  config2.requestTimeout = 10000;
  config2.default_bucket = "warehouse";

  TransactionManagerGeneric* tm = new TransactionManagerGeneric(
      levels, pathToTable, config2, transactionId);

  return reinterpret_cast<jlong>(tm);
}

extern "C" JNIEXPORT void JNICALL
Java_site_ycsb_db_LakeVillaTransactionManager_destroyNative(JNIEnv*, jobject, jlong handle) {
  delete reinterpret_cast<TransactionManagerGeneric*>(handle);
  Aws::ShutdownAPI(options);
}

extern "C" JNIEXPORT void JNICALL
Java_site_ycsb_db_LakeVillaTransactionManager_openTable(JNIEnv* env, jobject, jlong handle, jstring jPath) {
  const char* pathC = env->GetStringUTFChars(jPath, nullptr);
  std::string pathToTable(pathC);
  env->ReleaseStringUTFChars(jPath, pathC);
  reinterpret_cast<TransactionManagerGeneric*>(handle)->open_new_table(pathToTable);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_site_ycsb_db_LakeVillaTransactionManager_beginTransaction(JNIEnv*, jobject, jlong handle) {
  return reinterpret_cast<TransactionManagerGeneric*>(handle)
      ->begin_transaction_ycsb();
}

/*extern "C" JNIEXPORT jlong JNICALL Java_io_trino_1plugin_1lakevilla_LakeVillaTransactionManagerreadTable(
    JNIEnv*, jobject, jlong handle, jint tableId, jint numThreads) {
  auto table = reinterpret_cast<TransactionManagerGeneric*>(handle)->read_table(
      static_cast<uint32_t>(tableId), static_cast<uint32_t>(numThreads));
  return reinterpret_cast<jlong>(new std::shared_ptr<arrow::Table>(table));
}*/

extern "C" JNIEXPORT jbyteArray JNICALL
Java_site_ycsb_db_LakeVillaTransactionManager_readTableAsBytes(JNIEnv* env, jobject,
                                               jlong handle, jint tableId) {
  auto table = reinterpret_cast<TransactionManagerGeneric*>(handle)->read_table(
      static_cast<uint32_t>(tableId));
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
  writer->Close();
  buffer_stream->Close();

  auto buffer = buffer_stream->Finish();

  if (!buffer.ok()) return nullptr;

  jbyteArray result = env->NewByteArray(static_cast<jsize>((*buffer)->size()));
  env->SetByteArrayRegion(result, 0, static_cast<jsize>((*buffer)->size()),
                          reinterpret_cast<const jbyte*>((*buffer)->data()));

  return result;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_site_ycsb_db_LakeVillaTransactionManager_writeToTableNative(JNIEnv* env, jobject, jlong handle,
                                           jint tableId,
                                           jbyteArray arrowTableBytes) {
  if (arrowTableBytes == nullptr) {
    return false;
  }

  jsize len = env->GetArrayLength(arrowTableBytes);
  jbyte* bytes = env->GetByteArrayElements(arrowTableBytes, nullptr);

  auto buffer = std::make_shared<arrow::Buffer>(
      reinterpret_cast<const uint8_t*>(bytes), static_cast<int64_t>(len));

  auto input = std::make_shared<arrow::io::BufferReader>(buffer);

  std::shared_ptr<arrow::ipc::RecordBatchReader> batchReader;
  auto openResult = arrow::ipc::RecordBatchStreamReader::Open(input);
  if (!openResult.ok()) {
    return false;
  }
  batchReader = *openResult;

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

  if (table->num_columns() <= 1 || table->column(1)->num_chunks() == 0) {
    return false;
  }

  auto column = table->column(1);
  auto chunk = column->chunk(0);

  auto string_array =
        std::static_pointer_cast<arrow::StringArray>(chunk);

  if (string_array->length() == 0) {
    return false; 
  }

  std::string update_val = "";
  if(!string_array->IsNull(0)){
    update_val = string_array->GetString(0);
  }

  std::vector<std::pair<std::string, std::string>> update_vals;
  update_vals.push_back({"value", update_val});
  auto prev_id = reinterpret_cast<TransactionManagerGeneric*>(handle)->register_update_pairs(
      update_vals, LHTransactions::UpdateOperations::REP, update_vals);

  bool success = reinterpret_cast<TransactionManagerGeneric*>(handle)->add_file(
      static_cast<uint32_t>(tableId), table, "value", prev_id);

  env->ReleaseByteArrayElements(arrowTableBytes, bytes, JNI_ABORT);
  return success;
}

extern "C" JNIEXPORT void JNICALL Java_site_ycsb_db_LakeVillaTransactionManager_commitNative(
    JNIEnv*, jobject, jlong handle, jboolean readOnly) {
  reinterpret_cast<TransactionManagerGeneric*>(handle)->commit(readOnly);
}