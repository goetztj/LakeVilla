// JNI binding of the Trino connector (plugin/trino-lakevilla of the Trino
// fork, Java class io.trino.plugin.lakevilla.LakeVillaNative).
//
// Two kinds of native objects are handed to Java as jlong handles:
//   Store        a LakeVilla configuration (lvconfig.conf) and an S3
//                connection for metadata operations (listing, DDL)
//   Transaction  a TransactionManagerDeltaLake of one Trino transaction; it
//                begins with the first table the transaction accesses
// Data is exchanged as Arrow IPC streams (byte[]). Errors are reported as
// java.lang.RuntimeException (or false/-1 where documented); details are
// printed to stderr (the Trino server log) by the library.
#include <jni.h>

#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "Connectors/MinIOConnector.hpp"
#include "ManagerDeltaLake.hpp"
#include "Settings/LvSettings.hpp"
#include "jni_common.hpp"

using LHTransactions::TransactionManagerDeltaLake;
using LHJni::throw_java;
using LHJni::to_string;

namespace {

struct Store {
  std::unique_ptr<LHConfig::LvSettings> settings;
  StorageConnector::MinIOConfig config;
  std::unique_ptr<StorageConnector::MinIOConnector> connector;
};

struct Transaction {
  Store* store;
  std::unique_ptr<TransactionManagerDeltaLake> tm;
  // the Java side serializes the calls of a transaction; this guards against
  // misuse (the manager is not thread-safe)
  std::mutex mutex;
};

Store* store(jlong handle) { return reinterpret_cast<Store*>(handle); }

Transaction* transaction(jlong handle) {
  return reinterpret_cast<Transaction*>(handle);
}

// "012", "01", "2", ... ; "none" or "" for no level
LHTransactions::LakeVillaConf levels_of(const std::string& levels) {
  if (levels.empty() || levels == "none") {
    return {false, false, false};
  }
  return LHConfig::parse_levels(levels);
}

std::unique_ptr<TransactionManagerDeltaLake> new_manager(
    Store& s, const std::string& levels, std::string path,
    uint32_t transaction_id, bool own_connection) {
  std::unique_ptr<TransactionManagerDeltaLake> tm;
  if (own_connection) {
    tm = std::make_unique<TransactionManagerDeltaLake>(
        levels_of(levels), path, s.config, transaction_id);
  } else {
    tm = std::make_unique<TransactionManagerDeltaLake>(
        levels_of(levels), path, s.connector.get(), transaction_id);
  }
  if (!s.settings->pathToGlobalLog.empty() &&
      !s.settings->pathToGlobalTmpLog.empty()) {
    tm->set_global_log_paths(s.settings->pathToGlobalLog,
                             s.settings->pathToGlobalTmpLog);
  }
  tm->set_protocol_options(s.settings->protocol);
  return tm;
}

const char* table_result(TransactionManagerDeltaLake::TableResult result) {
  using R = TransactionManagerDeltaLake::TableResult;
  switch (result) {
    case R::Ok:
      return "Ok";
    case R::AlreadyExists:
      return "AlreadyExists";
    case R::NotEmpty:
      return "NotEmpty";
    case R::NotFound:
      return "NotFound";
    case R::Invalid:
      return "Invalid";
    default:
      return "Error";
  }
}

jstring table_result_string(JNIEnv* env,
                            TransactionManagerDeltaLake::TableResult result,
                            const std::string& error) {
  if (result == TransactionManagerDeltaLake::TableResult::Ok) {
    return nullptr;
  }
  std::string text = std::string(table_result(result)) + ": " + error;
  return env->NewStringUTF(text.c_str());
}

}  // namespace

#define LV_JNI(ret, name) \
  extern "C" JNIEXPORT ret JNICALL Java_io_trino_plugin_lakevilla_LakeVillaNative_##name

// ---------------------------------------------------------------------------
// store
// ---------------------------------------------------------------------------

LV_JNI(jlong, openStore)(JNIEnv* env, jclass, jstring jConfigPath) {
  try {
    std::string path = to_string(env, jConfigPath);
    auto s = std::make_unique<Store>();
    s->settings = std::make_unique<LHConfig::LvSettings>(path);
    if (!s->settings->parse()) {
      throw_java(env, "unable to read the LakeVilla config " + path);
      return 0;
    }
    if (s->settings->host.empty()) {
      throw_java(env, "the LakeVilla config " + path + " has no HOST");
      return 0;
    }
    s->config = s->settings->minio_config();
    LHJni::acquire_sdk();
    s->connector = std::make_unique<StorageConnector::MinIOConnector>(&s->config);
    return reinterpret_cast<jlong>(s.release());
  } catch (const std::exception& e) {
    throw_java(env, e.what());
    return 0;
  }
}

LV_JNI(void, closeStore)(JNIEnv* env, jclass, jlong handle) {
  if (!handle) {
    return;
  }
  try {
    delete store(handle);
  } catch (const std::exception& e) {
    throw_java(env, e.what());
  }
  LHJni::release_sdk();
}

// {bucket, isolation ("serializable" or "snapshot")}
LV_JNI(jobjectArray, storeInfo)(JNIEnv* env, jclass, jlong handle) {
  auto* s = store(handle);
  jclass string_class = env->FindClass("java/lang/String");
  jobjectArray result = env->NewObjectArray(2, string_class, nullptr);
  if (!result) {
    return nullptr;
  }
  env->SetObjectArrayElement(result, 0,
                             env->NewStringUTF(s->config.default_bucket.c_str()));
  env->SetObjectArrayElement(
      result, 1,
      env->NewStringUTF(s->settings->protocol.isolation ==
                                LHTransactions::Isolation::Serializable
                            ? "serializable"
                            : "snapshot"));
  return result;
}

// keys of all objects with the prefix
LV_JNI(jobjectArray, listObjects)(JNIEnv* env, jclass, jlong handle,
                                  jstring jPrefix) {
  try {
    std::string prefix = to_string(env, jPrefix);
    auto objects = store(handle)->connector->list_objects(prefix);
    if (!objects) {
      throw_java(env, "unable to list " + prefix);
      return nullptr;
    }
    jclass string_class = env->FindClass("java/lang/String");
    jobjectArray result = env->NewObjectArray(
        static_cast<jsize>(objects->size()), string_class, nullptr);
    if (!result) {
      return nullptr;
    }
    for (size_t i = 0; i < objects->size(); i++) {
      jstring key = env->NewStringUTF((*objects)[i].key.c_str());
      env->SetObjectArrayElement(result, static_cast<jsize>(i), key);
      env->DeleteLocalRef(key);
    }
    return result;
  } catch (const std::exception& e) {
    throw_java(env, e.what());
    return nullptr;
  }
}

LV_JNI(jboolean, writeObject)(JNIEnv* env, jclass, jlong handle, jstring jPath,
                              jbyteArray jData) {
  try {
    std::string path = to_string(env, jPath);
    std::vector<char> data;
    if (jData) {
      data.resize(env->GetArrayLength(jData));
      env->GetByteArrayRegion(jData, 0, static_cast<jsize>(data.size()),
                              reinterpret_cast<jbyte*>(data.data()));
    }
    return store(handle)->connector->write(path, data.data(), data.size());
  } catch (const std::exception& e) {
    throw_java(env, e.what());
    return false;
  }
}

LV_JNI(jboolean, deleteObject)(JNIEnv* env, jclass, jlong handle,
                               jstring jPath) {
  try {
    return store(handle)->connector->delete_file(to_string(env, jPath));
  } catch (const std::exception& e) {
    throw_java(env, e.what());
    return false;
  }
}

// Arrow IPC stream with the schema of the newest snapshot of the table (no
// rows); null if there is no Delta table at the path
LV_JNI(jbyteArray, tableSchema)(JNIEnv* env, jclass, jlong handle,
                                jstring jPath) {
  try {
    auto* s = store(handle);
    std::string path = to_string(env, jPath);
    // no Delta log: not a table (begin would retry opening it)
    auto log = s->connector->list_objects(path + "_delta_log/");
    if (!log) {
      throw_java(env, "unable to list " + path + "_delta_log/");
      return nullptr;
    }
    if (log->empty()) {
      return nullptr;
    }
    // a snapshot without levels: no markers, no global log
    auto tm = new_manager(*s, "none", path,
                          LHTransactions::unique_transaction_id(), false);
    if (!tm->begin_transaction_ycsb()) {
      return nullptr;
    }
    std::string error;
    auto schema = tm->get_table_schema(0, &error);
    tm->commit(true);
    if (!schema) {
      throw_java(env, error.empty() ? "the Delta table " + path +
                                          " has no metaData (schema)"
                                    : error);
      return nullptr;
    }
    auto ipc = LHJni::schema_to_ipc(*schema, &error);
    if (!ipc) {
      throw_java(env, "unable to serialize the schema of " + path + ": " + error);
      return nullptr;
    }
    return LHJni::to_byte_array(env, ipc->data(), ipc->size());
  } catch (const std::exception& e) {
    throw_java(env, e.what());
    return nullptr;
  }
}

// null on success, otherwise "<TableResult>: <message>"
LV_JNI(jstring, createTable)(JNIEnv* env, jclass, jlong handle,
                             jstring jLevels, jstring jPath,
                             jbyteArray jSchema, jstring jComment) {
  try {
    auto* s = store(handle);
    std::string path = to_string(env, jPath);
    std::string error;
    auto table = LHJni::ipc_to_table(env, jSchema, &error);
    if (!table) {
      throw_java(env, "invalid Arrow schema: " + error);
      return nullptr;
    }
    auto tm = new_manager(*s, to_string(env, jLevels), path,
                          LHTransactions::unique_transaction_id(), false);
    TransactionManagerDeltaLake::TableOptions options;
    options.description = to_string(env, jComment);
    auto result = tm->create_table(path, *table->schema(), options, &error);
    return table_result_string(env, result, error);
  } catch (const std::exception& e) {
    throw_java(env, e.what());
    return nullptr;
  }
}

// null on success, otherwise "<TableResult>: <message>"
LV_JNI(jstring, dropTable)(JNIEnv* env, jclass, jlong handle, jstring jLevels,
                           jstring jPath) {
  try {
    auto* s = store(handle);
    std::string path = to_string(env, jPath);
    auto tm = new_manager(*s, to_string(env, jLevels), path,
                          LHTransactions::unique_transaction_id(), false);
    std::string error;
    auto result = tm->drop_table(path, &error);
    return table_result_string(env, result, error);
  } catch (const std::exception& e) {
    throw_java(env, e.what());
    return nullptr;
  }
}

LV_JNI(jint, nextTransactionId)(JNIEnv*, jclass) {
  return static_cast<jint>(LHTransactions::unique_transaction_id());
}

// ---------------------------------------------------------------------------
// transactions
// ---------------------------------------------------------------------------

// begins a transaction whose first table is the given one (table id 0)
// redo: level 0 redoes the operations of a conflicting transaction instead of
// aborting it (ProtocolOptions::redo; overrides REDO of the config)
LV_JNI(jlong, beginTransaction)(JNIEnv* env, jclass, jlong handle,
                                jstring jLevels, jstring jPath,
                                jint transactionId, jboolean redo) {
  try {
    auto* s = store(handle);
    std::string path = to_string(env, jPath);
    auto txn = std::make_unique<Transaction>();
    txn->store = s;
    LHJni::acquire_sdk();
    txn->tm = new_manager(*s, to_string(env, jLevels), path,
                          static_cast<uint32_t>(transactionId), true);
    auto options = s->settings->protocol;
    options.redo = redo;
    txn->tm->set_protocol_options(options);
    if (!txn->tm->begin_transaction_ycsb()) {
      txn->tm.reset();
      LHJni::release_sdk();
      throw_java(env, "unable to begin a LakeVilla transaction on " + path);
      return 0;
    }
    return reinterpret_cast<jlong>(txn.release());
  } catch (const std::exception& e) {
    throw_java(env, e.what());
    return 0;
  }
}

// table id of the table in the transaction (opened if necessary); -1 if it
// cannot be opened (LakeVilla then aborts the transaction)
LV_JNI(jint, openTable)(JNIEnv* env, jclass, jlong handle, jstring jPath) {
  try {
    auto* txn = transaction(handle);
    std::lock_guard<std::mutex> guard(txn->mutex);
    std::string path = to_string(env, jPath);
    auto id = txn->tm->get_table_id(path);
    if (id != UINT32_MAX) {
      return static_cast<jint>(id);
    }
    if (!txn->tm->open_new_table(path)) {
      return -1;
    }
    return static_cast<jint>(txn->tm->tables.size() - 1);
  } catch (const std::exception& e) {
    throw_java(env, e.what());
    return -1;
  }
}

// all rows of the table in the transaction's snapshot (Arrow IPC stream)
LV_JNI(jbyteArray, readTable)(JNIEnv* env, jclass, jlong handle,
                              jint tableId) {
  try {
    auto* txn = transaction(handle);
    std::lock_guard<std::mutex> guard(txn->mutex);
    auto table = txn->tm->read_table(static_cast<uint32_t>(tableId));
    if (!table) {
      throw_java(env, "LakeVilla could not read table " +
                          std::to_string(tableId) + " (see the server log)");
      return nullptr;
    }
    std::string error;
    auto ipc = LHJni::table_to_ipc(*table, &error);
    if (!ipc) {
      throw_java(env, "unable to serialize the rows: " + error);
      return nullptr;
    }
    return LHJni::to_byte_array(env, ipc->data(), ipc->size());
  } catch (const std::exception& e) {
    throw_java(env, e.what());
    return nullptr;
  }
}

// adds the rows (Arrow IPC stream) as a new data file; false if LakeVilla
// refuses them (schema mismatch, partitioned table, storage error)
LV_JNI(jboolean, addRows)(JNIEnv* env, jclass, jlong handle, jint tableId,
                          jbyteArray jRows) {
  try {
    auto* txn = transaction(handle);
    std::string error;
    auto rows = LHJni::ipc_to_table(env, jRows, &error);
    if (!rows) {
      throw_java(env, "invalid Arrow data: " + error);
      return false;
    }
    std::lock_guard<std::mutex> guard(txn->mutex);
    return txn->tm->add_file(static_cast<uint32_t>(tableId), rows,
                             std::string(), UINT32_MAX);
  } catch (const std::exception& e) {
    throw_java(env, e.what());
    return false;
  }
}

// removes the rows whose first column equals the key; returns their number
LV_JNI(jlong, deleteKey)(JNIEnv* env, jclass, jlong handle, jint tableId,
                         jstring jKey) {
  try {
    auto* txn = transaction(handle);
    std::lock_guard<std::mutex> guard(txn->mutex);
    std::string key = to_string(env, jKey);
    auto id = static_cast<uint32_t>(tableId);
    auto rows = txn->tm->count_key_rows(id, key);
    if (!rows) {
      throw_java(env, "LakeVilla could not look up the key " + key +
                          " (see the server log)");
      return -1;
    }
    if (*rows == 0) {
      return 0;
    }
    if (!txn->tm->remove_file(id, static_cast<const std::string&>(key))) {
      throw_java(env, "LakeVilla could not remove the rows of the key " + key +
                          " (see the server log)");
      return -1;
    }
    return *rows;
  } catch (const std::exception& e) {
    throw_java(env, e.what());
    return -1;
  }
}

// removes all rows; returns their number (-1 if unknown)
LV_JNI(jlong, deleteAll)(JNIEnv* env, jclass, jlong handle, jint tableId) {
  try {
    auto* txn = transaction(handle);
    std::lock_guard<std::mutex> guard(txn->mutex);
    auto rows = txn->tm->remove_all_files(static_cast<uint32_t>(tableId));
    if (!rows) {
      throw_java(env, "LakeVilla could not remove the data files of table " +
                          std::to_string(tableId) + " (see the server log)");
      return -1;
    }
    return *rows;
  } catch (const std::exception& e) {
    throw_java(env, e.what());
    return -1;
  }
}

// ---------------------------------------------------------------------------
// row-level changes (UPDATE, MERGE, DELETE): rows are identified by their data
// file and their position in it
// ---------------------------------------------------------------------------

// paths of the data files of the table in the transaction's snapshot
LV_JNI(jobjectArray, listFiles)(JNIEnv* env, jclass, jlong handle,
                                jint tableId) {
  try {
    auto* txn = transaction(handle);
    std::lock_guard<std::mutex> guard(txn->mutex);
    auto files = txn->tm->list_data_files(static_cast<uint32_t>(tableId));
    if (!files) {
      throw_java(env, "LakeVilla could not list the data files of table " +
                          std::to_string(tableId));
      return nullptr;
    }
    jclass string_class = env->FindClass("java/lang/String");
    jobjectArray result = env->NewObjectArray(
        static_cast<jsize>(files->size()), string_class, nullptr);
    if (!result) {
      return nullptr;
    }
    for (size_t i = 0; i < files->size(); i++) {
      jstring path = env->NewStringUTF((*files)[i].path.c_str());
      env->SetObjectArrayElement(result, static_cast<jsize>(i), path);
      env->DeleteLocalRef(path);
    }
    return result;
  } catch (const std::exception& e) {
    throw_java(env, e.what());
    return nullptr;
  }
}

// the rows of a data file of the snapshot in file order (Arrow IPC stream)
LV_JNI(jbyteArray, readFile)(JNIEnv* env, jclass, jlong handle, jint tableId,
                             jstring jPath) {
  try {
    auto* txn = transaction(handle);
    std::lock_guard<std::mutex> guard(txn->mutex);
    std::string path = to_string(env, jPath);
    auto table =
        txn->tm->read_snapshot_file(static_cast<uint32_t>(tableId), path);
    if (!table) {
      throw_java(env, "LakeVilla could not read the data file " + path +
                          " (see the server log)");
      return nullptr;
    }
    std::string error;
    auto ipc = LHJni::table_to_ipc(*table, &error);
    if (!ipc) {
      throw_java(env, "unable to serialize the rows: " + error);
      return nullptr;
    }
    return LHJni::to_byte_array(env, ipc->data(), ipc->size());
  } catch (const std::exception& e) {
    throw_java(env, e.what());
    return nullptr;
  }
}

// removes the rows at the positions of a data file (copy-on-write); returns
// their number
LV_JNI(jlong, removeRows)(JNIEnv* env, jclass, jlong handle, jint tableId,
                          jstring jPath, jlongArray jPositions) {
  try {
    auto* txn = transaction(handle);
    std::string path = to_string(env, jPath);
    std::vector<int64_t> positions(env->GetArrayLength(jPositions));
    env->GetLongArrayRegion(jPositions, 0, static_cast<jsize>(positions.size()),
                            reinterpret_cast<jlong*>(positions.data()));
    std::lock_guard<std::mutex> guard(txn->mutex);
    auto rows =
        txn->tm->remove_rows(static_cast<uint32_t>(tableId), path, positions);
    if (!rows) {
      throw_java(env, "LakeVilla could not remove rows of the data file " +
                          path + " (see the server log)");
      return -1;
    }
    return *rows;
  } catch (const std::exception& e) {
    throw_java(env, e.what());
    return -1;
  }
}

// true if the transaction committed; false if LakeVilla aborted it
// (conflict). readOnly: the transaction did not write (releases markers)
LV_JNI(jboolean, commit)(JNIEnv* env, jclass, jlong handle,
                         jboolean readOnly) {
  try {
    auto* txn = transaction(handle);
    std::lock_guard<std::mutex> guard(txn->mutex);
    return txn->tm->commit(readOnly);
  } catch (const std::exception& e) {
    throw_java(env, e.what());
    return false;
  }
}

LV_JNI(void, abort)(JNIEnv* env, jclass, jlong handle) {
  try {
    auto* txn = transaction(handle);
    std::lock_guard<std::mutex> guard(txn->mutex);
    txn->tm->abort();
  } catch (const std::exception& e) {
    throw_java(env, e.what());
  }
}

LV_JNI(void, destroyTransaction)(JNIEnv* env, jclass, jlong handle) {
  if (!handle) {
    return;
  }
  try {
    delete transaction(handle);
  } catch (const std::exception& e) {
    throw_java(env, e.what());
  }
  LHJni::release_sdk();
}
