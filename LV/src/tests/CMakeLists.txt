find_package(GTest REQUIRED)
include(GoogleTest)

# unit and protocol tests: only need lakevilla-core (in-memory storage)
add_executable(lakevilla-tests
    test_util.cpp
    test_memory_connector.cpp
    test_store_clock.cpp
    test_settings.cpp
    test_deltalog.cpp
    test_helpers.cpp
    test_transaction.cpp
    test_tables.cpp
    test_schema.cpp
)
target_link_libraries(lakevilla-tests PRIVATE lakevilla-core GTest::gtest_main)
gtest_discover_tests(lakevilla-tests
    PROPERTIES TIMEOUT 120 LABELS unit
    DISCOVERY_TIMEOUT 60)

# integration tests against a real S3 endpoint (MinIO); skipped unless
# LV_TEST_S3_ENDPOINT is set, see tests/README.md
add_executable(lakevilla-s3-tests
    test_util.cpp
    test_s3_integration.cpp
)
target_link_libraries(lakevilla-s3-tests PRIVATE lakevilla-s3 GTest::gtest)
gtest_discover_tests(lakevilla-s3-tests
    PROPERTIES TIMEOUT 300 LABELS integration
    DISCOVERY_TIMEOUT 60)
