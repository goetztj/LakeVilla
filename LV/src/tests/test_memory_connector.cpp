#include <gtest/gtest.h>

#include "Connectors/MemoryConnector.hpp"

using StorageConnector::MemoryConnector;
using StorageConnector::WriteStatus;

TEST(MemoryConnector, WriteReadCheckDelete) {
  MemoryConnector conn;
  std::string data = "hello";

  EXPECT_FALSE(conn.check("a/b"));
  EXPECT_TRUE(conn.write("a/b", data.data(), data.size()));
  EXPECT_TRUE(conn.check("a/b"));

  auto content = conn.read2("a/b");
  ASSERT_NE(content->data, nullptr);
  EXPECT_EQ(std::string(content->data, content->length), "hello");

  EXPECT_TRUE(conn.delete_file("a/b"));
  EXPECT_FALSE(conn.check("a/b"));
  EXPECT_EQ(conn.read2("a/b")->data, nullptr);
}

TEST(MemoryConnector, EmptyObjectIsReadable) {
  MemoryConnector conn;
  conn.put("empty", "");
  auto content = conn.read2("empty");
  EXPECT_NE(content->data, nullptr);
  EXPECT_EQ(content->length, 0u);
}

TEST(MemoryConnector, WriteAtomicIsPutIfAbsent) {
  MemoryConnector conn;
  std::string first = "first";
  std::string second = "second";

  EXPECT_EQ(conn.write_atomic("k", first.data(), first.size()), WriteStatus::Ok);
  EXPECT_EQ(conn.write_atomic("k", second.data(), second.size()),
            WriteStatus::AlreadyExists);
  EXPECT_EQ(*conn.get("k"), "first");
}

TEST(MemoryConnector, ListIsSortedAndPrefixed) {
  MemoryConnector conn;
  conn.put("t/_delta_log/00000000000000000001.json", "b");
  conn.put("t/_delta_log/00000000000000000000.json", "a");
  conn.put("t/data.parquet", "x");
  conn.put("u/_delta_log/00000000000000000000.json", "c");

  auto objects = conn.list_objects("t/_delta_log");
  ASSERT_TRUE(objects.has_value());
  ASSERT_EQ(objects->size(), 2u);
  EXPECT_EQ((*objects)[0].key, "t/_delta_log/00000000000000000000.json");
  EXPECT_EQ((*objects)[1].key, "t/_delta_log/00000000000000000001.json");
  EXPECT_EQ((*objects)[1].size, 1u);

  auto empty = conn.list_objects("nothing/");
  ASSERT_TRUE(empty.has_value());
  EXPECT_TRUE(empty->empty());
}

TEST(MemoryConnector, FaultInjection) {
  MemoryConnector conn;
  std::string data = "x";
  conn.fail_hook = [](MemoryConnector::Op op, const std::string& path) {
    return op == MemoryConnector::Op::Write && path == "bad";
  };

  EXPECT_FALSE(conn.write("bad", data.data(), data.size()));
  EXPECT_TRUE(conn.write("good", data.data(), data.size()));

  conn.fail_hook = [](MemoryConnector::Op op, const std::string&) {
    return op == MemoryConnector::Op::WriteAtomic;
  };
  EXPECT_EQ(conn.write_atomic("new", data.data(), data.size()),
            WriteStatus::Error);
  EXPECT_FALSE(conn.check("new"));
}

TEST(MemoryConnector, CountsBytes) {
  MemoryConnector conn;
  std::string data = "12345";
  conn.write("k", data.data(), data.size());
  conn.read2("k");
  EXPECT_EQ(conn.getWritesBytes(), 5u);
  EXPECT_EQ(conn.getReadsBytes(), 5u);
}

TEST(MemoryConnector, WriteIfMatch) {
  MemoryConnector conn;
  std::string marker = "marker", commit = "commit", takeover = "takeover";
  ASSERT_EQ(conn.write_atomic("v1", marker.data(), marker.size()),
            WriteStatus::Ok);

  StorageConnector::ObjectVersion version;
  auto content = conn.read_versioned("v1", version);
  auto etag = version.etag;
  ASSERT_NE(content->data, nullptr);
  ASSERT_FALSE(etag.empty());

  EXPECT_EQ(conn.write_if_match("v1", commit.data(), commit.size(), "\"x\""),
            WriteStatus::PreconditionFailed);
  EXPECT_EQ(conn.write_if_match("v1", commit.data(), commit.size(), etag),
            WriteStatus::Ok);
  // the old ETag is no longer valid: a late takeover loses
  EXPECT_EQ(conn.write_if_match("v1", takeover.data(), takeover.size(), etag),
            WriteStatus::PreconditionFailed);
  EXPECT_EQ(*conn.get("v1"), "commit");
  // missing objects never match
  EXPECT_EQ(conn.write_if_match("v2", commit.data(), commit.size(), etag),
            WriteStatus::PreconditionFailed);
}

TEST(MemoryConnector, EtagChangesOnEveryWrite) {
  MemoryConnector conn;
  std::string data = "same";
  conn.write("k", data.data(), data.size());
  StorageConnector::ObjectVersion first, second;
  conn.read_versioned("k", first);
  conn.write("k", data.data(), data.size());
  conn.read_versioned("k", second);
  EXPECT_NE(first.etag, second.etag);
}
