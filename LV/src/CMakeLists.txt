cmake_minimum_required(VERSION 3.16)

if(DEFINED ENV{CONDA_PREFIX})
    set(CMAKE_IGNORE_PATH $ENV{CONDA_PREFIX}/bin $ENV{CONDA_PREFIX}/lib $ENV{CONDA_PREFIX}/include)
endif()

project(lakevilla CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
# keep the optimisation level used for the paper experiments
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG" CACHE STRING "" FORCE)

option(LV_BUILD_BENCHMARKS "Build the benchmark drivers (ycsbc-lv, cab-lv, ...)" ON)
option(LV_BUILD_TESTS "Build the unit tests" OFF)
option(LV_BUILD_JNI "Build the JNI library liblakevilla.so (requires a JDK)" ON)
option(LV_BUILD_COMPAT "Build compat-lv (Spark/Delta compatibility harness, compat/)" ON)
option(LV_WARNINGS "Enable compiler warnings" ON)
set(LV_SANITIZERS "" CACHE STRING "Semicolon-separated sanitizers, e.g. address;undefined or thread")

if(LV_SANITIZERS)
    list(JOIN LV_SANITIZERS "," _lv_san)
    add_compile_options(-fsanitize=${_lv_san} -fno-omit-frame-pointer)
    add_link_options(-fsanitize=${_lv_san})
endif()

message(STATUS "AWSSDK_ROOT_DIR = ${AWSSDK_ROOT_DIR}")
if(AWSSDK_ROOT_DIR)
    list(PREPEND CMAKE_PREFIX_PATH "${AWSSDK_ROOT_DIR}")
endif()

find_package(AWSSDK REQUIRED COMPONENTS s3)
find_package(Arrow REQUIRED)
find_package(Parquet REQUIRED)

# ---------------------------------------------------------------------------
# core library
# ---------------------------------------------------------------------------
# lakevilla-core: transaction protocol, Delta log handling, helpers; storage
# is accessed through the abstract StorageConnector::Connector (no AWS SDK)
add_library(lakevilla-core STATIC)
target_include_directories(lakevilla-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(lakevilla-core PUBLIC Parquet::parquet_shared Arrow::arrow_shared)

# lakevilla-s3: S3/MinIO connector based on the AWS SDK
add_library(lakevilla-s3 STATIC)
target_link_libraries(lakevilla-s3 PUBLIC lakevilla-core ${AWSSDK_LINK_LIBRARIES})

if(LV_WARNINGS)
    foreach(lib lakevilla-core lakevilla-s3)
        target_compile_options(${lib} PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare)
    endforeach()
endif()

add_subdirectory(Helpers)
add_subdirectory(LakeVilla)
add_subdirectory(Connectors)
add_subdirectory(Timer)
add_subdirectory(Settings)

# ---------------------------------------------------------------------------
# JNI library (used by the YCSB-LH / Trino bindings)
# ---------------------------------------------------------------------------
if(LV_BUILD_JNI)
    find_package(JNI)
    if(JNI_FOUND)
        add_library(lakevilla SHARED LakeVilla/TransactionManager/jni_transaction.cpp)
        target_include_directories(lakevilla PRIVATE ${JNI_INCLUDE_DIRS})
        target_link_libraries(lakevilla PRIVATE lakevilla-s3)
    else()
        message(WARNING "No JDK found - skipping liblakevilla.so (set -DLV_BUILD_JNI=OFF to silence)")
    endif()
endif()

# ---------------------------------------------------------------------------
# benchmark drivers
# ---------------------------------------------------------------------------
if(LV_BUILD_BENCHMARKS)
    add_executable(ui-lv main.cpp)
    add_executable(ycsbc-lv ycsbc.cpp)
    add_executable(tpcc-lv tpcc.cpp)
    add_executable(cab-lv cab.cpp)
    add_executable(hermitage-lv hermitage.cpp)
    add_executable(tpcds-lv tpcds.cpp)
    add_executable(lstbench-lv lstbench.cpp)

    target_include_directories(ycsbc-lv PRIVATE YCSB-C)

    add_subdirectory(YCSBDB)
    add_subdirectory(TPC-C)
    add_subdirectory(TPC-DS)
    add_subdirectory(CAB)
    add_subdirectory(LSTBench)

    foreach(exe ui-lv ycsbc-lv tpcc-lv cab-lv hermitage-lv tpcds-lv lstbench-lv)
        target_link_libraries(${exe} PRIVATE lakevilla-s3)
    endforeach()
endif()

# ---------------------------------------------------------------------------
# compatibility harness driver (compat/run_compat.py)
# ---------------------------------------------------------------------------
if(LV_BUILD_COMPAT)
    add_executable(compat-lv compat.cpp)
    target_link_libraries(compat-lv PRIVATE lakevilla-s3)
endif()

# ---------------------------------------------------------------------------
# tests
# ---------------------------------------------------------------------------
if(LV_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
