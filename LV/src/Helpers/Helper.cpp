#include "Helper.hpp"

#include <arrow/status.h>

std::string LHHelpers::readParquet(const char* data, uint64_t length) {
  auto buffer = std::make_shared<arrow::Buffer>(
      reinterpret_cast<const uint8_t*>(data), length);
  auto reader = std::make_shared<arrow::io::BufferReader>(buffer);

  arrow::MemoryPool* pool = arrow::default_memory_pool();

  std::unique_ptr<parquet::arrow::FileReader> arrow_reader;
  auto status = parquet::arrow::FileReader::Make(
      pool, parquet::ParquetFileReader::Open(reader), &arrow_reader);

  if (!status.ok()) {
    std::cerr << "ARROW: Error opening Parquet file: " << status.ToString()
              << std::endl;
    return {};
  }

  std::shared_ptr<arrow::Table> table;
  auto status2 = arrow_reader->ReadTable(&table);
  if (!status2.ok()) {
    std::cerr << "ARROW: Error reading parquet" << std::endl;
    return {};
  }

  auto schema = table->schema();

  std::vector<std::shared_ptr<arrow::Array>> fieldArrays;
  std::vector<std::string> names;
  fieldArrays.reserve(schema->num_fields());
  names.reserve(schema->num_fields());

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
          std::cerr << "unknown type " << schema->field(j)->type()->ToString()
                    << "; default: string" << std::endl;
          auto col = std::static_pointer_cast<arrow::StringArray>(
              table->column(j)->chunk(0));
          stream << col->Value(i) << "\t";
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
  auto buffer = std::make_shared<arrow::Buffer>(
      reinterpret_cast<const uint8_t*>(data), length);
  auto reader = std::make_shared<arrow::io::BufferReader>(buffer);

  arrow::MemoryPool* pool = arrow::default_memory_pool();

  std::unique_ptr<parquet::arrow::FileReader> arrow_reader;
  auto status = parquet::arrow::FileReader::Make(
      pool, parquet::ParquetFileReader::Open(reader), &arrow_reader);

  if (!status.ok()) {
    std::cerr << "ARROW: Error opening Parquet file: " << status.ToString()
              << std::endl;
    return {};
  }

  std::shared_ptr<arrow::Table> table;
  auto status2 = arrow_reader->ReadTable(&table);
  if (!status2.ok()) {
    std::cerr << "ARROW: Error reading parquet" << std::endl;
    return {};
  }

  return table;
}

std::vector<std::string> LHHelpers::readDLCheckpoint(const char* data,
                                                     uint64_t length) {
  auto buffer = std::make_shared<arrow::Buffer>(
      reinterpret_cast<const uint8_t*>(data), length);
  auto reader = std::make_shared<arrow::io::BufferReader>(buffer);

  arrow::MemoryPool* pool = arrow::default_memory_pool();

  std::unique_ptr<parquet::arrow::FileReader> arrow_reader;
  auto status = parquet::arrow::FileReader::Make(
      pool, parquet::ParquetFileReader::Open(reader), &arrow_reader);

  if (!status.ok()) {
    std::cerr << "ARROW: Error opening Parquet file: " << status.ToString()
              << std::endl;
    return {};
  }

  std::shared_ptr<arrow::Table> table;
  auto status2 = arrow_reader->ReadTable(&table);
  if (!status2.ok()) {
    std::cerr << "ARROW: Error reading parquet" << std::endl;
    return {};
  }

  auto add =
      std::static_pointer_cast<arrow::StructArray>(table->column(1)->chunk(0))
          ->GetFieldByName("path");

  auto string_val = std::static_pointer_cast<arrow::StringArray>(add);

  std::vector<std::string> result_vec;
  for (int i = 0; i < table->num_rows(); i++) {
    if (!string_val->IsNull(i)) {
      std::string tmp;
      tmp = string_val->Value(i);
      result_vec.push_back(std::move(tmp));
    }
  }

  return std::move(result_vec);
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
      parquet::ArrowWriterProperties::Builder().store_schema()->build();

  PARQUET_THROW_NOT_OK(parquet::arrow::WriteTable(
      *table, arrow::default_memory_pool(), writer, 3, props, arrow_props));

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
      parquet::ArrowWriterProperties::Builder().store_schema()->build();

  PARQUET_THROW_NOT_OK(parquet::arrow::WriteTable(
      *table, arrow::default_memory_pool(), writer, 3, props, arrow_props));

  PARQUET_THROW_NOT_OK(writer->Close());

  uint8_t* result_array = (uint8_t*)std::malloc(buffer->size());

  std::memcpy(result_array, buffer->data(), buffer->size());

  return std::make_unique<LHHelpers::ParquetPair>((char*)result_array,
                                                  buffer->size());
}

std::string LHHelpers::encodeAsParquet_str(
    std::shared_ptr<arrow::Table> table) {
  std::shared_ptr<arrow::ResizableBuffer> buffer =
      std::move(*(arrow::AllocateResizableBuffer(4096)));

  auto writer = std::make_shared<arrow::io::BufferOutputStream>(buffer);

  std::shared_ptr<parquet::WriterProperties> props =
      parquet::WriterProperties::Builder()
          .compression(arrow::Compression::SNAPPY)
          ->build();

  std::shared_ptr<parquet::ArrowWriterProperties> arrow_props =
      parquet::ArrowWriterProperties::Builder().store_schema()->build();

  PARQUET_THROW_NOT_OK(parquet::arrow::WriteTable(
      *table, arrow::default_memory_pool(), writer, 3, props, arrow_props));

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

std::unique_ptr<LHHelpers::ParquetPair> LHHelpers::generateCheckpoint(
    std::vector<std::string> files) {
  std::shared_ptr<arrow::ResizableBuffer> buffer =
      std::move(*(arrow::AllocateResizableBuffer(4096)));

  auto writer = std::make_shared<arrow::io::BufferOutputStream>(buffer);

  arrow::FieldVector attributes;

  attributes.push_back(arrow::field("txn", arrow::utf8()));

  auto path_field = arrow::field("path", arrow::list(arrow::utf8()), true);
  auto partition_values_field =
      arrow::field("partitionValues", arrow::list(arrow::utf8()), true);
  auto size_field = arrow::field("size", arrow::int64(), true);
  auto modification_time_field =
      arrow::field("modificationTime", arrow::int64(), true);
  auto data_change_field = arrow::field("dataChange", arrow::boolean(), true);
  auto tags_field = arrow::field("tags", arrow::list(arrow::utf8()), true);
  auto deletion_vector_field =
      arrow::field("deletionVector", arrow::utf8(), true);
  auto base_row_id_field = arrow::field("baseRowId", arrow::int64(), true);
  auto default_row_commit_version_field =
      arrow::field("defaultRowCommitVersion", arrow::int64(), true);
  auto stats_field = arrow::field("stats", arrow::utf8(), true);

  attributes.push_back(arrow::field(
      "add",
      arrow::struct_({path_field, partition_values_field, size_field,
                      modification_time_field, data_change_field, tags_field,
                      deletion_vector_field, base_row_id_field,
                      default_row_commit_version_field, stats_field})));

  attributes.push_back(arrow::field("remove", arrow::utf8(), true));
  attributes.push_back(arrow::field("metaData", arrow::utf8(), true));
  attributes.push_back(arrow::field("protocol", arrow::utf8(), true));
  attributes.push_back(arrow::field("domainMetadata", arrow::utf8(), true));

  auto schema = arrow::schema(attributes);

  arrow::ArrayVector value_vec;

  uint32_t index = 0;

  std::shared_ptr<arrow::Array> txn_array =
      *arrow::MakeArrayOfNull(arrow::utf8(), files.size());

  auto string_builder = std::make_shared<arrow::StringBuilder>();

  arrow::ListBuilder list_builder(arrow::default_memory_pool(), string_builder);

  for (const auto& f : files) {
    list_builder.Append();
    string_builder->Append(f);
  }

  std::shared_ptr<arrow::Array> path_array;
  list_builder.Finish(&path_array);

  std::shared_ptr<arrow::Array> partition_values_array =
      *arrow::MakeArrayOfNull(arrow::list(arrow::utf8()), files.size());

  std::shared_ptr<arrow::Array> size_array =
      *arrow::MakeArrayOfNull(arrow::int64(), files.size());

  std::shared_ptr<arrow::Array> modification_array =
      *arrow::MakeArrayOfNull(arrow::int64(), files.size());

  arrow::BooleanBuilder b_builder;
  for (auto& f : files) {
    b_builder.Append(false);
  }
  std::shared_ptr<arrow::Array> data_change_array = *b_builder.Finish();

  auto string_builder_tags = std::make_shared<arrow::StringBuilder>();

  arrow::ListBuilder list_builder_tags(arrow::default_memory_pool(),
                                       string_builder_tags);

  for (const auto& f : files) {
    list_builder_tags.Append();
    string_builder_tags->Append("");
  }

  std::shared_ptr<arrow::Array> tags_array;
  list_builder_tags.Finish(&tags_array);

  std::shared_ptr<arrow::Array> deletionVec_array =
      *arrow::MakeArrayOfNull(arrow::utf8(), files.size());

  std::shared_ptr<arrow::Array> baseRow_array =
      *arrow::MakeArrayOfNull(arrow::int64(), files.size());

  std::shared_ptr<arrow::Array> defaultRow_array =
      *arrow::MakeArrayOfNull(arrow::int64(), files.size());

  std::shared_ptr<arrow::Array> stats_array =
      *arrow::MakeArrayOfNull(arrow::utf8(), files.size());

  std::vector<std::shared_ptr<arrow::Array>> add_fields = {
      path_array,         partition_values_array, size_array,
      modification_array, data_change_array,      tags_array,
      deletionVec_array,  baseRow_array,          defaultRow_array,
      stats_array};
  auto add_array = std::make_shared<arrow::StructArray>(
      arrow::struct_({path_field, partition_values_field, size_field,
                      modification_time_field, data_change_field, tags_field,
                      deletion_vector_field, base_row_id_field,
                      default_row_commit_version_field, stats_field}),
      files.size(), add_fields);

  std::shared_ptr<arrow::Array> null_struct_array =
      *arrow::MakeArrayOfNull(arrow::utf8(), files.size());

  auto table = arrow::Table::Make(
      schema, {txn_array, add_array, null_struct_array, null_struct_array,
               null_struct_array, null_struct_array});

  std::shared_ptr<parquet::WriterProperties> props =
      parquet::WriterProperties::Builder()
          .compression(arrow::Compression::SNAPPY)
          ->build();

  std::shared_ptr<parquet::ArrowWriterProperties> arrow_props =
      parquet::ArrowWriterProperties::Builder().store_schema()->build();

  PARQUET_THROW_NOT_OK(parquet::arrow::WriteTable(
      *table, arrow::default_memory_pool(), writer, 3, props, arrow_props));

  PARQUET_THROW_NOT_OK(writer->Close());

  uint8_t* result_array = (uint8_t*)std::malloc(buffer->size());

  std::memcpy(result_array, buffer->data(), buffer->size());

  return std::make_unique<LHHelpers::ParquetPair>((char*)result_array,
                                                  buffer->size());
}
