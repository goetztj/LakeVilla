cmake_minimum_required(VERSION 3.13)

if(DEFINED ENV{CONDA_PREFIX})
    set(CMAKE_IGNORE_PATH $ENV{CONDA_PREFIX}/bin $ENV{CONDA_PREFIX}/lib $ENV{CONDA_PREFIX}/include)
endif()

set(SERVICE_COMPONENTS s3)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++20 -mtune=generic -O2")
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++20 -g")

project("lakevilla")

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

list(PREPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/CMake")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")

include_directories(.)

message(STATUS "AWSSDK_ROOT_DIR = ${AWSSDK_ROOT_DIR}")
message(STATUS "AWSSDK_PLATFORM_PREFIX = '${AWSSDK_PLATFORM_PREFIX}'")

list(PREPEND CMAKE_PREFIX_PATH "${AWSSDK_ROOT_DIR}")

set(SERVICE_COMPONENTS s3)
find_package(AWSSDK REQUIRED COMPONENTS ${SERVICE_COMPONENTS})

find_package(Parquet REQUIRED)

find_package(JNI REQUIRED)

add_library(dependencies INTERFACE)
target_include_directories(dependencies INTERFACE ${AWSSDK_LINK_LIBRARIES} Parquet::parquet_shared)
target_link_libraries(dependencies INTERFACE ${AWSSDK_LINK_LIBRARIES} Parquet::parquet_shared)

add_library(lakevilla-core OBJECT)
target_link_libraries(lakevilla-core dependencies)

add_executable(ui-lv main.cpp)
add_executable(ycsbc-lv ycsbc.cpp)
add_executable(tpcc-lv tpcc.cpp)
add_executable(cab-lv cab.cpp)
add_executable(hermitage-lv hermitage.cpp)
add_executable(tpcds-lv tpcds.cpp)
add_executable(lstbench-lv lstbench.cpp)

target_include_directories(ycsbc-lv PRIVATE YCSB-C)

add_subdirectory(Helpers)
add_subdirectory(LakeVilla)
add_subdirectory(Connectors)
#add_subdirectory(Generators)
#add_subdirectory(Tables)
#add_subdirectory(Benchmarks)
add_subdirectory(YCSBDB)
add_subdirectory(Timer)
add_subdirectory(TPC-C)
add_subdirectory(TPC-DS)
add_subdirectory(CAB)
add_subdirectory(Settings)
add_subdirectory(LSTBench)

target_link_libraries(ui-lv lakevilla-core)

target_link_libraries(ycsbc-lv lakevilla-core)

target_link_libraries(tpcc-lv lakevilla-core)

target_link_libraries(cab-lv lakevilla-core)

target_link_libraries(hermitage-lv lakevilla-core)

target_link_libraries(tpcds-lv lakevilla-core)

target_link_libraries(lstbench-lv lakevilla-core)

#target_link_libraries(ycsbc-lv duckdb)

add_library(lakevilla SHARED
    $<TARGET_OBJECTS:lakevilla-core>
)

set_target_properties(lakevilla PROPERTIES
    OUTPUT_NAME "lakevilla"
)

target_include_directories(lakevilla-core PRIVATE
    ${JNI_INCLUDE_DIRS}
)

target_include_directories(lakevilla PRIVATE
    ${JNI_INCLUDE_DIRS}
)

target_link_libraries(lakevilla PRIVATE
    dependencies
    ${JNI_LIBRARIES}
)
