#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>

#include "Settings/LvSettings.hpp"

namespace {
std::string write_config(const std::string& content) {
  std::string path = ::testing::TempDir() + "lvconfig_test.conf";
  std::ofstream out(path, std::ios::binary);
  out << content;
  return path;
}
}  // namespace

TEST(LvSettings, ParsesAllKeys) {
  auto path = write_config(
      "HOST=minio:9000\n"
      "USER=admin\n"
      "PASSWD=password\n"
      "AWSREGION=eu-central-1\n"
      "PathToGlobalLog=wh/_global/log.txt\n"
      "PathToGlobalTmpLog=wh/_global/log.txt.tmp\n"
      "PathToYCSBTable=wh/ycsb.db/usertable/\n"
      "LOGTHREADS=4\n"
      "FRESHRUNS=500\n"
      "CAB_PATH=wh/\n");
  LHConfig::LvSettings settings(path);
  ASSERT_TRUE(settings.parse());

  EXPECT_EQ(settings.host, "minio:9000");
  EXPECT_EQ(settings.user, "admin");
  EXPECT_EQ(settings.passwd, "password");
  EXPECT_EQ(settings.awsregion, "eu-central-1");
  EXPECT_EQ(settings.pathToGlobalLog, "wh/_global/log.txt");
  EXPECT_EQ(settings.pathToGlobalTmpLog, "wh/_global/log.txt.tmp");
  EXPECT_EQ(settings.pathToYCSBTable, "wh/ycsb.db/usertable/");
  EXPECT_EQ(settings.logthreads, 4u);
  EXPECT_EQ(settings.freshruns, 500u);
  EXPECT_EQ(settings.cab_path, "wh/");
}

TEST(LvSettings, MissingFileFails) {
  std::string path = "/nonexistent/lvconfig.conf";
  LHConfig::LvSettings settings(path);
  EXPECT_FALSE(settings.parse());
}

TEST(LvSettings, ValueMayContainEquals) {
  auto path = write_config("PASSWD=a=b\n");
  LHConfig::LvSettings settings(path);
  ASSERT_TRUE(settings.parse());
  EXPECT_EQ(settings.passwd, "a=b");
}

// Windows line endings, surrounding spaces and comments
TEST(LvSettings, TrimsWhitespaceAndComments) {
  auto path = write_config(
      "# LakeVilla config\r\n"
      " HOST = minio:9000 \r\n"
      "LOGTHREADS=2\r\n");
  LHConfig::LvSettings settings(path);
  ASSERT_TRUE(settings.parse());
  EXPECT_EQ(settings.host, "minio:9000");
  EXPECT_EQ(settings.logthreads, 2u);
  EXPECT_EQ(settings.readthreads, 1u);
}

// invalid numbers must not throw
TEST(LvSettings, InvalidNumberIsReported) {
  auto path = write_config("LOGTHREADS=abc\n");
  LHConfig::LvSettings settings(path);
  EXPECT_NO_THROW(settings.parse());
  EXPECT_EQ(settings.logthreads, 1u);
}

TEST(LvSettings, Bucket) {
  auto path = write_config("BUCKET=lakehouse\n");
  LHConfig::LvSettings settings(path);
  EXPECT_EQ(settings.bucket, "warehouse");
  ASSERT_TRUE(settings.parse());
  EXPECT_EQ(settings.bucket, "lakehouse");
}

TEST(LvSettings, MinioConfig) {
  auto path = write_config(
      "HOST=minio:9000\nUSER=u\nPASSWD=p\nAWSREGION=eu-central-1\n"
      "BUCKET=lake\n");
  LHConfig::LvSettings settings(path);
  ASSERT_TRUE(settings.parse());
  auto config = settings.minio_config();
  EXPECT_EQ(config.endpointURI, "minio:9000");
  EXPECT_EQ(config.user, "u");
  EXPECT_EQ(config.passwd, "p");
  EXPECT_EQ(config.region, "eu-central-1");
  EXPECT_EQ(config.default_bucket, "lake");
}

TEST(LvSettings, ParseLevels) {
  EXPECT_EQ(LHConfig::parse_levels("012"), (std::vector<bool>{true, true, true}));
  EXPECT_EQ(LHConfig::parse_levels("2"), (std::vector<bool>{false, false, true}));
  EXPECT_EQ(LHConfig::parse_levels(""), (std::vector<bool>{false, false, false}));
  EXPECT_EQ(LHConfig::parse_levels("00"), (std::vector<bool>{true, false, false}));
}

TEST(LvSettings, ProtocolOptionDefaults) {
  auto path = write_config("HOST=minio:9000\n");
  LHConfig::LvSettings settings(path);
  ASSERT_TRUE(settings.parse());
  EXPECT_EQ(settings.protocol.isolation,
            LHTransactions::Isolation::Serializable);
  EXPECT_EQ(settings.protocol.marker_lease_ms, 30000u);
  EXPECT_EQ(settings.protocol.marker_grace_ms, 100u);
  EXPECT_EQ(settings.protocol.max_redos, 3u);
  EXPECT_FALSE(settings.protocol.global_log_repair);
  EXPECT_EQ(settings.protocol.global_log_repair_after, 3u);
  EXPECT_FALSE(settings.protocol.global_log_check_at_begin);
  EXPECT_TRUE(settings.protocol.checkpoints);
  EXPECT_EQ(settings.protocol.checkpoint_interval, 0u);
}

TEST(LvSettings, ProtocolOptions) {
  auto path = write_config(
      "ISOLATION=snapshot\nMARKER_LEASE_MS=5000\nMARKER_GRACE_MS=250\n"
      "MAX_REDOS=7\nGLOBAL_LOG_REPAIR=1\nGLOBAL_LOG_REPAIR_AFTER=2\n"
      "GLOBAL_LOG_CHECK_AT_BEGIN=true\nCHECKPOINTS=0\nCHECKPOINT_INTERVAL=25\n");
  LHConfig::LvSettings settings(path);
  ASSERT_TRUE(settings.parse());
  EXPECT_FALSE(settings.protocol.checkpoints);
  EXPECT_EQ(settings.protocol.checkpoint_interval, 25u);
  EXPECT_EQ(settings.protocol.isolation, LHTransactions::Isolation::Snapshot);
  EXPECT_EQ(settings.protocol.marker_lease_ms, 5000u);
  EXPECT_EQ(settings.protocol.marker_grace_ms, 250u);
  EXPECT_EQ(settings.protocol.max_redos, 7u);
  EXPECT_TRUE(settings.protocol.global_log_repair);
  EXPECT_EQ(settings.protocol.global_log_repair_after, 2u);
  EXPECT_TRUE(settings.protocol.global_log_check_at_begin);

  // invalid values keep the defaults
  auto invalid = write_config("ISOLATION=chaos\nGLOBAL_LOG_REPAIR=maybe\n");
  LHConfig::LvSettings settings2(invalid);
  ASSERT_TRUE(settings2.parse());
  EXPECT_EQ(settings2.protocol.isolation,
            LHTransactions::Isolation::Serializable);
  EXPECT_FALSE(settings2.protocol.global_log_repair);
}

TEST(LvSettings, ApplyDefaults) {
  auto path = write_config("ISOLATION=snapshot\nMAX_REDOS=9\n");
  LHConfig::LvSettings settings(path);
  ASSERT_TRUE(settings.parse());
  settings.apply_defaults();
  auto options = LHTransactions::default_protocol_options();
  LHTransactions::set_default_protocol_options({});
  EXPECT_EQ(options.isolation, LHTransactions::Isolation::Snapshot);
  EXPECT_EQ(options.max_redos, 9u);
}
