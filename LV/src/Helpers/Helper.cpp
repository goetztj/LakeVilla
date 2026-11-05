#include "Helper.hpp"

#include <arrow/status.h>

#include <charconv>
#include <chrono>
#include <cmath>
#include <limits>

namespace {
// data files: the Arrow schema is stored; timestamps are written in
// microseconds (Spark 3.5 cannot read nanosecond Parquet timestamps; finer
// values are truncated as Delta timestamps have microsecond precision)
std::shared_ptr<parquet::ArrowWriterProperties> data_file_arrow_properties() {
  return parquet::ArrowWriterProperties::Builder()
      .store_schema()
      ->coerce_timestamps(arrow::TimeUnit::MICRO)
      ->allow_truncated_timestamps()
      ->build();
}

// Spark 3 reads dates before 1582 and timestamps before 1900 only from files
// whose metadata names a Spark version >= 3.0 (other files may use the
// hybrid Julian calendar of older writers and are refused by default). Data
// files of LakeVilla use the proleptic Gregorian calendar, as Spark 3 does.
std::shared_ptr<arrow::Table> with_calendar_metadata(
    const std::shared_ptr<arrow::Table>& table) {
  auto metadata = table->schema()->metadata()
                      ? table->schema()->metadata()->Copy()
                      : std::make_shared<arrow::KeyValueMetadata>();
  if (!metadata->Contains("org.apache.spark.version")) {
    metadata->Append("org.apache.spark.version", "3.0.0");
  }
  return table->ReplaceSchemaMetadata(metadata);
}
}  // namespace

std::string LHHelpers::readParquet(const char* data, uint64_t length) {
  auto table = readParquetAsTable(data, length);
  if (!table) {
    return {};
  }

  // one chunk per column
  auto combined = table->CombineChunks();
  if (!combined.ok()) {
    std::cerr << "ARROW: " << combined.status().ToString() << std::endl;
    return {};
  }
  table = *combined;

  auto schema = table->schema();

  std::stringstream stream;

  for (size_t i = 0; i < table->num_rows(); i++) {
    for (size_t j = 0; j < table->num_columns(); j++) {
      switch (schema->field(j)->type()->id()) {
        case arrow::Type::INT8: {
          auto col = std::static_pointer_cast<arrow::Int8Array>(
              table->column(j)->chunk(0));
          stream << col->Value(i) << "\t";
        } break;
        case arrow::Type::INT16: {
          auto col = std::static_pointer_cast<arrow::Int16Array>(
              table->column(j)->chunk(0));
          stream << col->Value(i) << "\t";
        } break;
        case arrow::Type::INT32: {
          auto col = std::static_pointer_cast<arrow::Int32Array>(
              table->column(j)->chunk(0));
          stream << col->Value(i) << "\t";
        } break;
        case arrow::Type::INT64: {
          auto col = std::static_pointer_cast<arrow::Int64Array>(
              table->column(j)->chunk(0));
          stream << col->Value(i) << "\t";
        } break;
        case arrow::Type::BOOL: {
          auto col = std::static_pointer_cast<arrow::BooleanArray>(
              table->column(j)->chunk(0));
          stream << col->Value(i) << "\t";
        } break;
        case arrow::Type::STRING: {
          auto col = std::static_pointer_cast<arrow::StringArray>(
              table->column(j)->chunk(0));
          stream << col->Value(i) << "\t";
        } break;
        case arrow::Type::DOUBLE: {
          auto col = std::static_pointer_cast<arrow::DoubleArray>(
              table->column(j)->chunk(0));
          stream << col->Value(i) << "\t";
        } break;
        case arrow::Type::FLOAT: {
          auto col = std::static_pointer_cast<arrow::FloatArray>(
              table->column(j)->chunk(0));
          stream << col->Value(i) << "\t";
        } break;
        case arrow::Type::UINT8: {
          auto col = std::static_pointer_cast<arrow::UInt8Array>(
              table->column(j)->chunk(0));
          stream << col->Value(i) << "\t";
        } break;
        case arrow::Type::UINT16: {
          auto col = std::static_pointer_cast<arrow::UInt16Array>(
              table->column(j)->chunk(0));
          stream << col->Value(i) << "\t";
        } break;
        case arrow::Type::UINT32: {
          auto col = std::static_pointer_cast<arrow::UInt32Array>(
              table->column(j)->chunk(0));
          stream << col->Value(i) << "\t";
        } break;
        case arrow::Type::UINT64: {
          auto col = std::static_pointer_cast<arrow::UInt64Array>(
              table->column(j)->chunk(0));
          stream << col->Value(i) << "\t";
        } break;
        case arrow::Type::TIME32: {
          auto col = std::static_pointer_cast<arrow::Time32Array>(
              table->column(j)->chunk(0));
          stream << col->Value(i) << "\t";
        } break;
        case arrow::Type::TIME64: {
          auto col = std::static_pointer_cast<arrow::Time64Array>(
              table->column(j)->chunk(0));
          stream << col->Value(i) << "\t";
        } break;
        case arrow::Type::DATE32: {
          auto col = std::static_pointer_cast<arrow::Date32Array>(
              table->column(j)->chunk(0));
          stream << col->Value(i) << "\t";
        } break;
        case arrow::Type::DATE64: {
          auto col = std::static_pointer_cast<arrow::Date64Array>(
              table->column(j)->chunk(0));
          stream << col->Value(i) << "\t";
        } break;
        case arrow::Type::TIMESTAMP: {
          auto col = std::static_pointer_cast<arrow::TimestampArray>(
              table->column(j)->chunk(0));
          stream << col->Value(i) << "\t";
        } break;
        case arrow::Type::BINARY: {
          auto col = std::static_pointer_cast<arrow::BinaryArray>(
              table->column(j)->chunk(0));
          stream << col->Value(i) << "\t";
        } break;
        case arrow::Type::DECIMAL128: {
          auto col = std::static_pointer_cast<arrow::Decimal128Array>(
              table->column(j)->chunk(0));
          stream << col->Value(i) << "\t";
        } break;
        default: {
          auto scalar = table->column(j)->GetScalar(i);
          stream << (scalar.ok() ? (*scalar)->ToString() : "?") << "\t";
        } break;
      }
    }
    stream << std::endl;
  }

  stream << std::endl;

  return stream.str();
}

std::shared_ptr<arrow::Table> LHHelpers::readParquetAsTable(const char* data,
                                                            uint64_t length) {
  if (!data || length == 0) {
    std::cerr << "ARROW: no data to read" << std::endl;
    return {};
  }

  auto buffer = std::make_shared<arrow::Buffer>(
      reinterpret_cast<const uint8_t*>(data), length);
  auto reader = std::make_shared<arrow::io::BufferReader>(buffer);

  // INT96 timestamps (Spark's default) as microseconds, the precision of
  // Delta timestamps; nanoseconds would only cover the years 1677 to 2262
  parquet::ArrowReaderProperties properties;
  properties.set_coerce_int96_timestamp_unit(arrow::TimeUnit::MICRO);
  parquet::arrow::FileReaderBuilder builder;
  auto opened = builder.Open(reader);
  if (!opened.ok()) {
    std::cerr << "ARROW: Error opening Parquet file: " << opened.ToString()
              << std::endl;
    return {};
  }
  auto arrow_reader = builder.memory_pool(arrow::default_memory_pool())
                          ->properties(properties)
                          ->Build();
  if (!arrow_reader.ok()) {
    std::cerr << "ARROW: Error opening Parquet file: "
              << arrow_reader.status().ToString() << std::endl;
    return {};
  }

  auto table = (*arrow_reader)->ReadTable();
  if (!table.ok()) {
    std::cerr << "ARROW: Error reading parquet: "
              << table.status().ToString() << std::endl;
    return {};
  }

  return *table;
}

namespace {

// one element of an Arrow array as JSON (Delta checkpoints <-> log actions)
void arrow_to_json(const arrow::Array& array, int64_t i, rapidjson::Value& out,
                   rapidjson::Document::AllocatorType& allocator) {
  if (array.IsNull(i)) {
    out.SetNull();
    return;
  }
  switch (array.type_id()) {
    case arrow::Type::STRING: {
      auto view = static_cast<const arrow::StringArray&>(array).GetView(i);
      out.SetString(view.data(), view.size(), allocator);
    } break;
    case arrow::Type::LARGE_STRING: {
      auto view = static_cast<const arrow::LargeStringArray&>(array).GetView(i);
      out.SetString(view.data(), view.size(), allocator);
    } break;
    case arrow::Type::BOOL:
      out.SetBool(static_cast<const arrow::BooleanArray&>(array).Value(i));
      break;
    case arrow::Type::INT8:
      out.SetInt64(static_cast<const arrow::Int8Array&>(array).Value(i));
      break;
    case arrow::Type::INT16:
      out.SetInt64(static_cast<const arrow::Int16Array&>(array).Value(i));
      break;
    case arrow::Type::INT32:
      out.SetInt64(static_cast<const arrow::Int32Array&>(array).Value(i));
      break;
    case arrow::Type::INT64:
      out.SetInt64(static_cast<const arrow::Int64Array&>(array).Value(i));
      break;
    case arrow::Type::DOUBLE:
      out.SetDouble(static_cast<const arrow::DoubleArray&>(array).Value(i));
      break;
    case arrow::Type::STRUCT: {
      auto& structs = static_cast<const arrow::StructArray&>(array);
      out.SetObject();
      for (int f = 0; f < structs.num_fields(); f++) {
        auto child = structs.field(f);
        if (child->IsNull(i)) {
          continue;
        }
        rapidjson::Value name(structs.struct_type()->field(f)->name().c_str(),
                              allocator);
        rapidjson::Value value;
        arrow_to_json(*child, i, value, allocator);
        out.AddMember(name, value, allocator);
      }
    } break;
    case arrow::Type::LIST: {
      auto& lists = static_cast<const arrow::ListArray&>(array);
      out.SetArray();
      auto values = lists.values();
      for (int64_t j = lists.value_offset(i);
           j < lists.value_offset(i) + lists.value_length(i); j++) {
        rapidjson::Value value;
        arrow_to_json(*values, j, value, allocator);
        out.PushBack(value, allocator);
      }
    } break;
    case arrow::Type::MAP: {
      auto& maps = static_cast<const arrow::MapArray&>(array);
      out.SetObject();
      auto keys = maps.keys();
      auto items = maps.items();
      for (int64_t j = maps.value_offset(i);
           j < maps.value_offset(i) + maps.value_length(i); j++) {
        rapidjson::Value key;
        arrow_to_json(*keys, j, key, allocator);
        if (!key.IsString()) {
          continue;
        }
        rapidjson::Value value;
        arrow_to_json(*items, j, value, allocator);
        out.AddMember(key, value, allocator);
      }
    } break;
    default:
      out.SetNull();
      break;
  }
}

std::string to_json_string(const rapidjson::Value& value) {
  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  value.Accept(writer);
  return std::string(buffer.GetString(), buffer.GetSize());
}

// JSON of the first non-null row of a struct column ("" if none)
std::string first_action_json(const std::shared_ptr<arrow::Table>& table,
                              const std::string& column_name) {
  auto column = table->GetColumnByName(column_name);
  if (!column) {
    return "";
  }
  for (auto& chunk : column->chunks()) {
    for (int64_t i = 0; i < chunk->length(); i++) {
      if (!chunk->IsNull(i)) {
        rapidjson::Document doc;
        arrow_to_json(*chunk, i, doc, doc.GetAllocator());
        return to_json_string(doc);
      }
    }
  }
  return "";
}

// child of a struct array of the given type (nullptr if missing or different)
template <typename ArrayType>
std::shared_ptr<ArrayType> struct_field(const arrow::StructArray& array,
                                        const char* name,
                                        arrow::Type::type type) {
  auto field = array.GetFieldByName(name);
  if (!field || field->type_id() != type) {
    return nullptr;
  }
  return std::static_pointer_cast<ArrayType>(field);
}

// JSON of element i of a map or struct array ("" if missing or null)
std::string element_json(const std::shared_ptr<arrow::Array>& array,
                         int64_t i) {
  if (!array || array->IsNull(i)) {
    return "";
  }
  rapidjson::Document doc;
  arrow_to_json(*array, i, doc, doc.GetAllocator());
  return to_json_string(doc);
}

}  // namespace

std::optional<LHHelpers::CheckpointContent> LHHelpers::readDLCheckpointContent(
    const char* data, uint64_t length) {
  auto table = readParquetAsTable(data, length);
  if (!table) {
    return std::nullopt;
  }

  CheckpointContent content;
  content.protocol = first_action_json(table, "protocol");
  content.metadata = first_action_json(table, "metaData");

  auto add = table->GetColumnByName("add");
  if (add && add->type()->id() == arrow::Type::STRUCT) {
    for (auto& chunk : add->chunks()) {
      auto& add_array = static_cast<const arrow::StructArray&>(*chunk);
      auto paths = struct_field<arrow::StringArray>(add_array, "path",
                                                    arrow::Type::STRING);
      if (!paths) {
        std::cerr << "checkpoint: add.path missing or not a string"
                  << std::endl;
        return std::nullopt;
      }
      auto stats = struct_field<arrow::StringArray>(add_array, "stats",
                                                    arrow::Type::STRING);
      auto sizes = struct_field<arrow::Int64Array>(add_array, "size",
                                                   arrow::Type::INT64);
      auto times = struct_field<arrow::Int64Array>(
          add_array, "modificationTime", arrow::Type::INT64);
      auto partition_values = add_array.GetFieldByName("partitionValues");
      auto tags = add_array.GetFieldByName("tags");
      auto dv = add_array.GetFieldByName("deletionVector");

      for (int64_t i = 0; i < add_array.length(); i++) {
        if (add_array.IsNull(i) || paths->IsNull(i)) {
          continue;
        }
        CheckpointEntry entry;
        entry.path = paths->GetString(i);
        if (stats && !stats->IsNull(i)) {
          entry.stats = stats->GetString(i);
        }
        if (sizes && !sizes->IsNull(i)) {
          entry.size = sizes->Value(i);
        }
        if (times && !times->IsNull(i)) {
          entry.modification_time = times->Value(i);
        }
        entry.partition_values = element_json(partition_values, i);
        entry.tags = element_json(tags, i);
        if (dv && !dv->IsNull(i)) {
          content.unsupported = "deletion vectors";
        }
        content.adds.push_back(std::move(entry));
      }
    }
  }

  auto remove = table->GetColumnByName("remove");
  if (remove && remove->type()->id() == arrow::Type::STRUCT) {
    for (auto& chunk : remove->chunks()) {
      auto& remove_array = static_cast<const arrow::StructArray&>(*chunk);
      auto paths = struct_field<arrow::StringArray>(remove_array, "path",
                                                    arrow::Type::STRING);
      if (!paths) {
        continue;
      }
      auto timestamps = struct_field<arrow::Int64Array>(
          remove_array, "deletionTimestamp", arrow::Type::INT64);
      auto sizes = struct_field<arrow::Int64Array>(remove_array, "size",
                                                   arrow::Type::INT64);
      auto partition_values = remove_array.GetFieldByName("partitionValues");
      for (int64_t i = 0; i < remove_array.length(); i++) {
        if (remove_array.IsNull(i) || paths->IsNull(i)) {
          continue;
        }
        CheckpointRemove tombstone;
        tombstone.path = paths->GetString(i);
        if (timestamps && !timestamps->IsNull(i)) {
          tombstone.deletion_timestamp = timestamps->Value(i);
        }
        if (sizes && !sizes->IsNull(i)) {
          tombstone.size = sizes->Value(i);
        }
        tombstone.partition_values = element_json(partition_values, i);
        content.removes.push_back(std::move(tombstone));
      }
    }
  }

  auto txn = table->GetColumnByName("txn");
  if (txn && txn->type()->id() == arrow::Type::STRUCT) {
    for (auto& chunk : txn->chunks()) {
      auto& txn_array = static_cast<const arrow::StructArray&>(*chunk);
      auto app_ids = struct_field<arrow::StringArray>(txn_array, "appId",
                                                      arrow::Type::STRING);
      auto versions = struct_field<arrow::Int64Array>(txn_array, "version",
                                                      arrow::Type::INT64);
      auto updated = struct_field<arrow::Int64Array>(
          txn_array, "lastUpdated", arrow::Type::INT64);
      for (int64_t i = 0; i < txn_array.length(); i++) {
        if (txn_array.IsNull(i)) {
          continue;
        }
        if (!app_ids || !versions || app_ids->IsNull(i) ||
            versions->IsNull(i)) {
          content.unsupported = "invalid txn action";
          continue;
        }
        CheckpointTxn entry;
        entry.app_id = app_ids->GetString(i);
        entry.version = versions->Value(i);
        if (updated && !updated->IsNull(i)) {
          entry.last_updated = updated->Value(i);
        }
        content.txns.push_back(std::move(entry));
      }
    }
  }

  auto domains = table->GetColumnByName("domainMetadata");
  if (domains && domains->type()->id() == arrow::Type::STRUCT) {
    for (auto& chunk : domains->chunks()) {
      auto& domain_array = static_cast<const arrow::StructArray&>(*chunk);
      auto names = struct_field<arrow::StringArray>(domain_array, "domain",
                                                    arrow::Type::STRING);
      auto configurations = struct_field<arrow::StringArray>(
          domain_array, "configuration", arrow::Type::STRING);
      auto removed = struct_field<arrow::BooleanArray>(
          domain_array, "removed", arrow::Type::BOOL);
      for (int64_t i = 0; i < domain_array.length(); i++) {
        if (domain_array.IsNull(i)) {
          continue;
        }
        if (!names || names->IsNull(i)) {
          content.unsupported = "invalid domainMetadata action";
          continue;
        }
        // a checkpoint contains no removed domains (they are tombstones of
        // the log only)
        if (removed && !removed->IsNull(i) && removed->Value(i)) {
          continue;
        }
        CheckpointDomain entry;
        entry.domain = names->GetString(i);
        if (configurations && !configurations->IsNull(i)) {
          entry.configuration = configurations->GetString(i);
        }
        content.domains.push_back(std::move(entry));
      }
    }
  }

  return content;
}

std::optional<std::vector<LHHelpers::CheckpointEntry>>
LHHelpers::readDLCheckpointEntries(const char* data, uint64_t length) {
  auto content = readDLCheckpointContent(data, length);
  if (!content) {
    return std::nullopt;
  }
  return std::move(content->adds);
}

std::vector<std::string> LHHelpers::readDLCheckpoint(const char* data,
                                                     uint64_t length) {
  std::vector<std::string> result_vec;
  auto entries = readDLCheckpointEntries(data, length);
  if (entries) {
    for (auto& entry : *entries) {
      result_vec.push_back(std::move(entry.path));
    }
  }
  return result_vec;
}

std::unique_ptr<LHHelpers::ParquetPair> LHHelpers::encodeAsParquet(
    std::vector<std::string> names, std::vector<int32_t> values) {
  std::shared_ptr<arrow::ResizableBuffer> buffer =
      std::move(*(arrow::AllocateResizableBuffer(4096)));

  auto writer = std::make_shared<arrow::io::BufferOutputStream>(buffer);

  arrow::FieldVector attributes;

  for (auto& ref : names) {
    attributes.push_back(arrow::field(ref, arrow::int32()));
  }

  auto schema = arrow::schema(attributes);

  arrow::ArrayVector value_vec;

  uint32_t index = 0;

  for (size_t i = 0; i < names.size(); i++) {
    arrow::Int32Builder builder;

    for (uint32_t j = 0; j < values.size() / names.size(); j++) {
      PARQUET_THROW_NOT_OK(builder.Append(values[index]));
      index++;
    }

    std::shared_ptr<arrow::Array> arrow_array;

    PARQUET_THROW_NOT_OK(builder.Finish(&arrow_array));

    value_vec.push_back(arrow_array);
  }

  auto table =
      arrow::Table::Make(schema, value_vec, values.size() / names.size());

  std::shared_ptr<parquet::WriterProperties> props =
      parquet::WriterProperties::Builder()
          .compression(arrow::Compression::SNAPPY)
          ->build();

  std::shared_ptr<parquet::ArrowWriterProperties> arrow_props =
      data_file_arrow_properties();

  PARQUET_THROW_NOT_OK(parquet::arrow::WriteTable(
      *with_calendar_metadata(table), arrow::default_memory_pool(), writer, 3, props, arrow_props));

  PARQUET_THROW_NOT_OK(writer->Close());

  uint8_t* result_array = (uint8_t*)std::malloc(buffer->size());

  std::memcpy(result_array, buffer->data(), buffer->size());

  return std::make_unique<LHHelpers::ParquetPair>((char*)result_array,
                                                  buffer->size());
}

std::unique_ptr<LHHelpers::ParquetPair> LHHelpers::encodeAsParquet(
    std::shared_ptr<arrow::Table> table) {
  std::shared_ptr<arrow::ResizableBuffer> buffer =
      std::move(*(arrow::AllocateResizableBuffer(4096)));

  auto writer = std::make_shared<arrow::io::BufferOutputStream>(buffer);

  std::shared_ptr<parquet::WriterProperties> props =
      parquet::WriterProperties::Builder()
          .compression(arrow::Compression::SNAPPY)
          ->build();

  std::shared_ptr<parquet::ArrowWriterProperties> arrow_props =
      data_file_arrow_properties();

  PARQUET_THROW_NOT_OK(parquet::arrow::WriteTable(
      *with_calendar_metadata(table), arrow::default_memory_pool(), writer, 3, props, arrow_props));

  PARQUET_THROW_NOT_OK(writer->Close());

  uint8_t* result_array = (uint8_t*)std::malloc(buffer->size());

  std::memcpy(result_array, buffer->data(), buffer->size());

  return std::make_unique<LHHelpers::ParquetPair>((char*)result_array,
                                                  buffer->size());
}

std::string LHHelpers::encodeAsParquet_str(
    std::shared_ptr<arrow::Table> table, int64_t chunk_size) {
  std::shared_ptr<arrow::ResizableBuffer> buffer =
      std::move(*(arrow::AllocateResizableBuffer(4096)));

  auto writer = std::make_shared<arrow::io::BufferOutputStream>(buffer);

  std::shared_ptr<parquet::WriterProperties> props =
      parquet::WriterProperties::Builder()
          .compression(arrow::Compression::SNAPPY)
          ->build();

  std::shared_ptr<parquet::ArrowWriterProperties> arrow_props =
      data_file_arrow_properties();

  PARQUET_THROW_NOT_OK(parquet::arrow::WriteTable(
      *with_calendar_metadata(table), arrow::default_memory_pool(), writer, chunk_size, props, arrow_props));

  PARQUET_THROW_NOT_OK(writer->Close());

  std::string parquet_content(reinterpret_cast<const char*>(buffer->data()),
                              buffer->size());

  return parquet_content;
}

void LHHelpers::writeParquetTest() {
  std::shared_ptr<arrow::ResizableBuffer> buffer =
      std::move(*(arrow::AllocateResizableBuffer(4096)));

  auto writer = std::make_shared<arrow::io::BufferOutputStream>(buffer);

  parquet::schema::NodeVector fields;

  fields.push_back(parquet::schema::PrimitiveNode::Make(
      "x", parquet::Repetition::REQUIRED, parquet::Type::INT32,
      parquet::ConvertedType::INT_32));
  fields.push_back(parquet::schema::PrimitiveNode::Make(
      "y", parquet::Repetition::REQUIRED, parquet::Type::INT32,
      parquet::ConvertedType::INT_32));

  auto schema = std::static_pointer_cast<parquet::schema::GroupNode>(
      parquet::schema::GroupNode::Make("", parquet::Repetition::REQUIRED,
                                       fields));

  parquet::WriterProperties::Builder builder;

  builder.compression(arrow::Compression::SNAPPY);

  auto properties = builder.build();

  parquet::StreamWriter os{
      parquet::ParquetFileWriter::Open(writer, schema, properties)};

  for (int32_t i = 0; i < 2; i++) {
    os << i << i + 1 << parquet::EndRow;
  }

  ARROW_WARN_NOT_OK(writer->Flush(), "not flushable");
  ARROW_WARN_NOT_OK(writer->Close(), "not closed!");

  uint8_t* result_array = (uint8_t*)std::malloc(buffer->size());

  std::memcpy(result_array, buffer->data(), buffer->size());

  for (size_t i = 0; i < buffer->size(); i++) {
    std::cout << result_array[i];
  }

  std::cout << readParquet((char*)(result_array), buffer->size());

  std::free(result_array);
}

std::unique_ptr<rapidjson::Document> LHHelpers::readJSON_rapid(
    const char* data) {
  auto doc = std::make_unique<rapidjson::Document>();

  doc->Parse(data);

  return doc;
}

rapidjson::StringBuffer LHHelpers::encodeAsJSON_rapid(
    std::unique_ptr<rapidjson::Document>& content) {
  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  content->Accept(writer);

  return buffer;
}

std::shared_ptr<arrow::Table> LHHelpers::arrow_table_builder() {
  std::cout << "please describe the schema" << std::endl;

  bool cont = true;
  arrow::FieldVector attributes;

  arrow::ArrayVector value_vec;

  while (cont) {
    std::cout << "What is the name of the next attribute?\n";
    std::string name;
    std::cin >> name;

    std::cout << "And its data type?\n[0]int32\n[1]float32\n[other]continue\n";
    std::string type;
    std::cin >> type;

    switch (std::stoi(type)) {
      case 0:
        attributes.push_back(arrow::field(name, arrow::int32()));
        break;
      case 1:
        attributes.push_back(arrow::field(name, arrow::float32()));
        break;
      default:
        cont = false;
        break;
    }
  }

  auto schema = arrow::schema(attributes);

  std::vector<std::vector<std::string>> raw_entries;

  while (true) {
    std::cout << "Please add the next row in CSV format (separator ;). Typing "
                 "'-' continues the oepration"
              << std::endl;
    std::string row;
    std::cin >> row;

    if (row.compare("-") == 0) {
      break;
    }

    std::vector<std::string> raw_row;

    std::string value;

    std::stringstream row_stream(row);

    while (std::getline(row_stream, value, ';')) {
      std::cout << "got: " << value << std::endl;
      raw_row.push_back(value);
    }

    raw_entries.push_back(raw_row);
  }

  uint32_t col_counter = 0;
  for (auto& attr : attributes) {
    switch (attr->type()->id()) {
      case arrow::Type::INT32: {
        arrow::Int32Builder builder;
        for (auto& ref : raw_entries) {
          int32_t parsed_val = std::stoi(ref[col_counter]);
          PARQUET_THROW_NOT_OK(builder.Append(parsed_val));
        }
        std::shared_ptr<arrow::Array> arrow_array;

        PARQUET_THROW_NOT_OK(builder.Finish(&arrow_array));

        value_vec.push_back(arrow_array);
      } break;
      case arrow::Type::FLOAT: {
        arrow::FloatBuilder builder;
        for (auto& ref : raw_entries) {
          float parsed_val = std::stof(ref[col_counter]);
          PARQUET_THROW_NOT_OK(builder.Append(parsed_val));
        }
        std::shared_ptr<arrow::Array> arrow_array;

        PARQUET_THROW_NOT_OK(builder.Finish(&arrow_array));

        value_vec.push_back(arrow_array);
      } break;

      default:
        break;
    }
    col_counter++;
  }

  return arrow::Table::Make(schema, value_vec, raw_entries.size());
}

namespace {

// appends a JSON value to a builder of the given (checkpoint) type
arrow::Status append_json(arrow::ArrayBuilder* builder,
                          const rapidjson::Value* value) {
  if (!value || value->IsNull()) {
    return builder->AppendNull();
  }
  switch (builder->type()->id()) {
    case arrow::Type::STRING: {
      auto string_builder = static_cast<arrow::StringBuilder*>(builder);
      if (value->IsString()) {
        return string_builder->Append(value->GetString(),
                                      value->GetStringLength());
      }
      return string_builder->Append(to_json_string(*value));
    }
    case arrow::Type::INT32:
      if (!value->IsInt()) return builder->AppendNull();
      return static_cast<arrow::Int32Builder*>(builder)->Append(value->GetInt());
    case arrow::Type::INT64:
      if (!value->IsInt64()) return builder->AppendNull();
      return static_cast<arrow::Int64Builder*>(builder)->Append(
          value->GetInt64());
    case arrow::Type::BOOL:
      if (!value->IsBool()) return builder->AppendNull();
      return static_cast<arrow::BooleanBuilder*>(builder)->Append(
          value->GetBool());
    case arrow::Type::STRUCT: {
      if (!value->IsObject()) return builder->AppendNull();
      auto struct_builder = static_cast<arrow::StructBuilder*>(builder);
      ARROW_RETURN_NOT_OK(struct_builder->Append());
      // type() builds a new DataType: keep it alive while iterating
      auto type_ptr = builder->type();
      auto& type = static_cast<const arrow::StructType&>(*type_ptr);
      for (int f = 0; f < type.num_fields(); f++) {
        auto member = value->FindMember(type.field(f)->name().c_str());
        ARROW_RETURN_NOT_OK(append_json(
            struct_builder->field_builder(f),
            member != value->MemberEnd() ? &member->value : nullptr));
      }
      return arrow::Status::OK();
    }
    case arrow::Type::LIST: {
      if (!value->IsArray()) return builder->AppendNull();
      auto list_builder = static_cast<arrow::ListBuilder*>(builder);
      ARROW_RETURN_NOT_OK(list_builder->Append());
      for (auto& element : value->GetArray()) {
        ARROW_RETURN_NOT_OK(append_json(list_builder->value_builder(), &element));
      }
      return arrow::Status::OK();
    }
    case arrow::Type::MAP: {
      if (!value->IsObject()) return builder->AppendNull();
      auto map_builder = static_cast<arrow::MapBuilder*>(builder);
      ARROW_RETURN_NOT_OK(map_builder->Append());
      for (auto& member : value->GetObject()) {
        ARROW_RETURN_NOT_OK(static_cast<arrow::StringBuilder*>(
                                map_builder->key_builder())
                                ->Append(member.name.GetString(),
                                         member.name.GetStringLength()));
        ARROW_RETURN_NOT_OK(
            append_json(map_builder->item_builder(), &member.value));
      }
      return arrow::Status::OK();
    }
    default:
      return builder->AppendNull();
  }
}

std::shared_ptr<arrow::Schema> checkpoint_schema() {
  auto string_map = arrow::map(arrow::utf8(), arrow::utf8());
  auto string_list = arrow::list(arrow::utf8());

  auto protocol = arrow::struct_({
      arrow::field("minReaderVersion", arrow::int32()),
      arrow::field("minWriterVersion", arrow::int32()),
      arrow::field("readerFeatures", string_list),
      arrow::field("writerFeatures", string_list),
  });
  auto metadata = arrow::struct_({
      arrow::field("id", arrow::utf8()),
      arrow::field("name", arrow::utf8()),
      arrow::field("description", arrow::utf8()),
      arrow::field("format",
                   arrow::struct_({arrow::field("provider", arrow::utf8()),
                                   arrow::field("options", string_map)})),
      arrow::field("schemaString", arrow::utf8()),
      arrow::field("partitionColumns", string_list),
      arrow::field("configuration", string_map),
      arrow::field("createdTime", arrow::int64()),
  });
  auto add = arrow::struct_({
      arrow::field("path", arrow::utf8()),
      arrow::field("partitionValues", string_map),
      arrow::field("size", arrow::int64()),
      arrow::field("modificationTime", arrow::int64()),
      arrow::field("dataChange", arrow::boolean()),
      arrow::field("stats", arrow::utf8()),
      arrow::field("tags", string_map),
  });
  auto remove = arrow::struct_({
      arrow::field("path", arrow::utf8()),
      arrow::field("deletionTimestamp", arrow::int64()),
      arrow::field("dataChange", arrow::boolean()),
      arrow::field("extendedFileMetadata", arrow::boolean()),
      arrow::field("partitionValues", string_map),
      arrow::field("size", arrow::int64()),
  });
  auto txn = arrow::struct_({
      arrow::field("appId", arrow::utf8()),
      arrow::field("version", arrow::int64()),
      arrow::field("lastUpdated", arrow::int64()),
  });
  auto domain_metadata = arrow::struct_({
      arrow::field("domain", arrow::utf8()),
      arrow::field("configuration", arrow::utf8()),
      arrow::field("removed", arrow::boolean()),
  });

  // column order of the checkpoints of Spark (Delta 3.0)
  return arrow::schema({arrow::field("txn", txn),
                        arrow::field("add", add),
                        arrow::field("remove", remove),
                        arrow::field("metaData", metadata),
                        arrow::field("protocol", protocol),
                        arrow::field("domainMetadata", domain_metadata)});
}

}  // namespace

std::unique_ptr<LHHelpers::ParquetPair> LHHelpers::generateCheckpoint(
    const CheckpointContent& content) {
  auto schema = checkpoint_schema();

  // every row of a checkpoint holds exactly one action
  std::vector<rapidjson::Document> rows;

  auto add_action_row = [&rows](const char* action, const std::string& json) {
    rapidjson::Document value;
    value.Parse(json.c_str());
    if (value.HasParseError() || !value.IsObject()) {
      throw std::invalid_argument(std::string("invalid ") + action + " JSON");
    }
    rapidjson::Document row;
    row.SetObject();
    rapidjson::Value copy(value, row.GetAllocator());
    row.AddMember(rapidjson::StringRef(action), copy, row.GetAllocator());
    rows.push_back(std::move(row));
  };

  // a JSON object of an action ("": empty object)
  auto json_object = [](const std::string& json,
                        rapidjson::Document::AllocatorType& alloc) {
    rapidjson::Value result;
    result.SetObject();
    if (json.empty()) {
      return result;
    }
    rapidjson::Document parsed;
    parsed.Parse(json.c_str());
    if (parsed.HasParseError() || !parsed.IsObject()) {
      throw std::invalid_argument("invalid JSON object " + json);
    }
    result.CopyFrom(parsed, alloc);
    return result;
  };

  add_action_row("protocol", content.protocol);
  add_action_row("metaData", content.metadata);

  for (auto& txn : content.txns) {
    rapidjson::Document row;
    row.SetObject();
    auto& alloc = row.GetAllocator();
    rapidjson::Value action;
    action.SetObject();
    action.AddMember("appId", rapidjson::Value(txn.app_id.c_str(), alloc),
                     alloc);
    action.AddMember("version", txn.version, alloc);
    if (txn.last_updated >= 0) {
      action.AddMember("lastUpdated", txn.last_updated, alloc);
    }
    row.AddMember("txn", action, alloc);
    rows.push_back(std::move(row));
  }

  for (auto& domain : content.domains) {
    rapidjson::Document row;
    row.SetObject();
    auto& alloc = row.GetAllocator();
    rapidjson::Value action;
    action.SetObject();
    action.AddMember("domain", rapidjson::Value(domain.domain.c_str(), alloc),
                     alloc);
    action.AddMember("configuration",
                     rapidjson::Value(domain.configuration.c_str(), alloc),
                     alloc);
    action.AddMember("removed", false, alloc);
    row.AddMember("domainMetadata", action, alloc);
    rows.push_back(std::move(row));
  }

  for (auto& remove : content.removes) {
    rapidjson::Document row;
    row.SetObject();
    auto& alloc = row.GetAllocator();
    rapidjson::Value action;
    action.SetObject();
    action.AddMember("path", rapidjson::Value(remove.path.c_str(), alloc),
                     alloc);
    action.AddMember("deletionTimestamp", remove.deletion_timestamp, alloc);
    action.AddMember("dataChange", false, alloc);
    // extendedFileMetadata: partitionValues and size are present
    action.AddMember("extendedFileMetadata", remove.size >= 0, alloc);
    if (remove.size >= 0) {
      action.AddMember("partitionValues",
                       json_object(remove.partition_values, alloc), alloc);
      action.AddMember("size", remove.size, alloc);
    }
    row.AddMember("remove", action, alloc);
    rows.push_back(std::move(row));
  }

  auto now = now_ms();
  for (auto& add : content.adds) {
    rapidjson::Document row;
    row.SetObject();
    auto& alloc = row.GetAllocator();
    rapidjson::Value action;
    action.SetObject();
    action.AddMember("path", rapidjson::Value(add.path.c_str(), alloc), alloc);
    action.AddMember("partitionValues",
                     json_object(add.partition_values, alloc), alloc);
    action.AddMember("size", add.size < 0 ? 0 : add.size, alloc);
    action.AddMember("modificationTime",
                     add.modification_time < 0 ? now : add.modification_time,
                     alloc);
    action.AddMember("dataChange", false, alloc);
    if (!add.tags.empty()) {
      action.AddMember("tags", json_object(add.tags, alloc), alloc);
    }
    if (!add.stats.empty()) {
      action.AddMember("stats", rapidjson::Value(add.stats.c_str(), alloc),
                       alloc);
    }
    row.AddMember("add", action, alloc);
    rows.push_back(std::move(row));
  }

  arrow::ArrayVector columns;
  for (auto& field : schema->fields()) {
    std::unique_ptr<arrow::ArrayBuilder> builder;
    PARQUET_THROW_NOT_OK(
        arrow::MakeBuilder(arrow::default_memory_pool(), field->type(), &builder));
    for (auto& row : rows) {
      auto member = row.FindMember(field->name().c_str());
      PARQUET_THROW_NOT_OK(append_json(
          builder.get(), member != row.MemberEnd() ? &member->value : nullptr));
    }
    std::shared_ptr<arrow::Array> array;
    PARQUET_THROW_NOT_OK(builder->Finish(&array));
    columns.push_back(array);
  }

  auto table = arrow::Table::Make(schema, columns, rows.size());
  // one row group for the whole checkpoint
  auto bytes = encodeAsParquet_str(table, std::max<int64_t>(1, rows.size()));

  char* result_array = (char*)std::malloc(bytes.size());
  std::memcpy(result_array, bytes.data(), bytes.size());
  return std::make_unique<LHHelpers::ParquetPair>(result_array, bytes.size());
}

namespace {

template <typename ArrayType>
void integer_min_max(const arrow::ChunkedArray& column,
                     LHHelpers::ColumnStatistics& stats) {
  bool first = true;
  int64_t min = 0, max = 0;
  for (auto& chunk : column.chunks()) {
    auto array = std::static_pointer_cast<ArrayType>(chunk);
    for (int64_t i = 0; i < array->length(); i++) {
      if (array->IsNull(i)) {
        continue;
      }
      int64_t value = static_cast<int64_t>(array->Value(i));
      if (first || value < min) min = value;
      if (first || value > max) max = value;
      first = false;
    }
  }
  if (!first) {
    stats.has_min_max = true;
    stats.numeric = true;
    stats.min = std::to_string(min);
    stats.max = std::to_string(max);
  }
}

std::string shortest_double(double value) {
  char buffer[64];
  auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
  return std::string(buffer, result.ptr);
}

template <typename ArrayType>
void floating_min_max(const arrow::ChunkedArray& column,
                      LHHelpers::ColumnStatistics& stats) {
  bool first = true;
  double min = 0, max = 0;
  for (auto& chunk : column.chunks()) {
    auto array = std::static_pointer_cast<ArrayType>(chunk);
    for (int64_t i = 0; i < array->length(); i++) {
      if (array->IsNull(i)) {
        continue;
      }
      double value = static_cast<double>(array->Value(i));
      if (std::isnan(value) || std::isinf(value)) {
        // no reliable min/max representation in JSON
        return;
      }
      if (first || value < min) min = value;
      if (first || value > max) max = value;
      first = false;
    }
  }
  if (!first) {
    stats.has_min_max = true;
    stats.numeric = true;
    stats.min = shortest_double(min);
    stats.max = shortest_double(max);
  }
}

template <typename ArrayType>
void string_min_max(const arrow::ChunkedArray& column,
                    LHHelpers::ColumnStatistics& stats) {
  bool first = true;
  std::string min, max;
  for (auto& chunk : column.chunks()) {
    auto array = std::static_pointer_cast<ArrayType>(chunk);
    for (int64_t i = 0; i < array->length(); i++) {
      if (array->IsNull(i)) {
        continue;
      }
      auto value = array->GetView(i);
      if (first || value < min) min = std::string(value);
      if (first || value > max) max = std::string(value);
      first = false;
    }
  }
  if (!first) {
    stats.has_min_max = true;
    stats.min = std::move(min);
    stats.max = std::move(max);
  }
}

}  // namespace

LHHelpers::TableStatistics LHHelpers::computeStatistics(
    const arrow::Table& table) {
  TableStatistics result;
  result.num_records = table.num_rows();

  for (int i = 0; i < table.num_columns(); i++) {
    auto& column = *table.column(i);
    ColumnStatistics stats;
    stats.name = table.schema()->field(i)->name();
    stats.null_count = column.null_count();

    switch (column.type()->id()) {
      case arrow::Type::STRING:
        string_min_max<arrow::StringArray>(column, stats);
        break;
      case arrow::Type::LARGE_STRING:
        string_min_max<arrow::LargeStringArray>(column, stats);
        break;
      case arrow::Type::INT8:
        integer_min_max<arrow::Int8Array>(column, stats);
        break;
      case arrow::Type::INT16:
        integer_min_max<arrow::Int16Array>(column, stats);
        break;
      case arrow::Type::INT32:
        integer_min_max<arrow::Int32Array>(column, stats);
        break;
      case arrow::Type::INT64:
        integer_min_max<arrow::Int64Array>(column, stats);
        break;
      case arrow::Type::UINT8:
        integer_min_max<arrow::UInt8Array>(column, stats);
        break;
      case arrow::Type::UINT16:
        integer_min_max<arrow::UInt16Array>(column, stats);
        break;
      case arrow::Type::UINT32:
        integer_min_max<arrow::UInt32Array>(column, stats);
        break;
      case arrow::Type::FLOAT:
        floating_min_max<arrow::FloatArray>(column, stats);
        break;
      case arrow::Type::DOUBLE:
        floating_min_max<arrow::DoubleArray>(column, stats);
        break;
      default:
        // dates, timestamps, decimals, nested types: no min/max (engines
        // then do not skip the file, which is always correct)
        break;
    }
    result.columns.push_back(std::move(stats));
  }
  return result;
}

int64_t LHHelpers::parquetNumRows(const char* data, uint64_t length) {
  if (!data || length == 0) {
    return -1;
  }
  try {
    auto buffer = std::make_shared<arrow::Buffer>(
        reinterpret_cast<const uint8_t*>(data), length);
    auto reader = parquet::ParquetFileReader::Open(
        std::make_shared<arrow::io::BufferReader>(buffer));
    return reader->metadata()->num_rows();
  } catch (const std::exception& e) {
    std::cerr << "unable to read Parquet footer: " << e.what() << std::endl;
    return -1;
  }
}

int64_t LHHelpers::now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}
