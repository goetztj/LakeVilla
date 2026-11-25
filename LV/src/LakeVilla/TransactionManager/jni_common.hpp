#pragma once
// Helpers shared by the JNI bindings of liblakevilla.so (jni_transaction.cpp:
// YCSB-LH, jni_trino.cpp: Trino connector).
#include <arrow/api.h>
#include <jni.h>

#include <memory>
#include <string>

namespace LHJni {

// The AWS SDK must be initialised once per process and shut down only after
// the last client is gone: the bindings reference count their users.
void acquire_sdk();
void release_sdk();

// C++ exceptions must not cross the JNI boundary: the bindings catch them and
// throw a java.lang.RuntimeException (or the given class) instead
void throw_java(JNIEnv* env, const std::string& message,
                const char* exception_class = "java/lang/RuntimeException");

// UTF-8 copy of a Java string ("" for null)
std::string to_string(JNIEnv* env, jstring value);

// Java byte[] with the given content (nullptr if the array cannot be created)
jbyteArray to_byte_array(JNIEnv* env, const uint8_t* data, int64_t size);

// Arrow IPC stream (schema + record batches) of a table; nullptr and *error
// on failure
std::shared_ptr<arrow::Buffer> table_to_ipc(const arrow::Table& table,
                                            std::string* error);

// Arrow IPC stream with the schema only (no record batches)
std::shared_ptr<arrow::Buffer> schema_to_ipc(const arrow::Schema& schema,
                                             std::string* error);

// table of an Arrow IPC stream (Java byte[]); nullptr and *error on failure
std::shared_ptr<arrow::Table> ipc_to_table(JNIEnv* env, jbyteArray bytes,
                                           std::string* error);

}  // namespace LHJni
