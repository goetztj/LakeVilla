#include "Helpers/DeltaSchema.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <random>
#include <set>

#include "rapidjson/include/rapidjson/document.h"
#include "rapidjson/include/rapidjson/stringbuffer.h"
#include "rapidjson/include/rapidjson/writer.h"

namespace {

using Writer = rapidjson::Writer<rapidjson::StringBuffer>;

struct Converter {
  std::set<std::string> features;
  std::string error;

  bool fail(const std::string& message) {
    if (this->error.empty()) {
      this->error = message;
    }
    return false;
  }

  // the "type" value of a field, an array element or a map key/value
  bool type(const std::shared_ptr<arrow::DataType>& type,
            const std::string& context, Writer& w) {
    switch (type->id()) {
      case arrow::Type::STRING:
      case arrow::Type::LARGE_STRING:
        return w.String("string");
      case arrow::Type::INT8:
        return w.String("byte");
      case arrow::Type::INT16:
        return w.String("short");
      case arrow::Type::INT32:
        return w.String("integer");
      case arrow::Type::INT64:
        return w.String("long");
      case arrow::Type::FLOAT:
        return w.String("float");
      case arrow::Type::DOUBLE:
        return w.String("double");
      case arrow::Type::BOOL:
        return w.String("boolean");
      case arrow::Type::BINARY:
      case arrow::Type::LARGE_BINARY:
      case arrow::Type::FIXED_SIZE_BINARY:
        return w.String("binary");
      case arrow::Type::DATE32:
        return w.String("date");
      case arrow::Type::TIMESTAMP: {
        auto& ts = static_cast<const arrow::TimestampType&>(*type);
        if (ts.timezone().empty()) {
          this->features.insert("timestampNtz");
          return w.String("timestamp_ntz");
        }
        return w.String("timestamp");
      }
      case arrow::Type::DECIMAL128: {
        auto& dec = static_cast<const arrow::Decimal128Type&>(*type);
        if (dec.precision() > 38) {
          return this->fail(context + ": decimal precision above 38");
        }
        return w.String(("decimal(" + std::to_string(dec.precision()) + "," +
                         std::to_string(dec.scale()) + ")")
                            .c_str());
      }
      case arrow::Type::DICTIONARY:
        return this->type(
            static_cast<const arrow::DictionaryType&>(*type).value_type(),
            context, w);
      case arrow::Type::LIST:
      case arrow::Type::LARGE_LIST: {
        auto& list = static_cast<const arrow::BaseListType&>(*type);
        w.StartObject();
        w.Key("type");
        w.String("array");
        w.Key("elementType");
        if (!this->type(list.value_type(), context + ".element", w)) {
          return false;
        }
        w.Key("containsNull");
        w.Bool(list.value_field()->nullable());
        return w.EndObject();
      }
      case arrow::Type::MAP: {
        auto& map = static_cast<const arrow::MapType&>(*type);
        w.StartObject();
        w.Key("type");
        w.String("map");
        w.Key("keyType");
        if (!this->type(map.key_type(), context + ".key", w)) {
          return false;
        }
        w.Key("valueType");
        if (!this->type(map.item_type(), context + ".value", w)) {
          return false;
        }
        w.Key("valueContainsNull");
        w.Bool(map.item_field()->nullable());
        return w.EndObject();
      }
      case arrow::Type::STRUCT:
        return this->fields(type->fields(), context + ".", w);
      default:
        return this->fail(context + ": Arrow type " + type->ToString() +
                          " has no Delta equivalent");
    }
  }

  // {"type":"struct","fields":[...]}
  bool fields(const arrow::FieldVector& fields, const std::string& prefix,
              Writer& w) {
    if (fields.empty()) {
      return this->fail(prefix.empty() ? "the schema has no columns"
                                       : prefix + ": struct without fields");
    }
    std::set<std::string> names;
    w.StartObject();
    w.Key("type");
    w.String("struct");
    w.Key("fields");
    w.StartArray();
    for (auto& field : fields) {
      const std::string& name = field->name();
      std::string context = prefix + name;
      if (name.empty()) {
        return this->fail(prefix + "<empty>: empty column name");
      }
      if (name.find_first_of(" ,;{}()\n\t=") != std::string::npos) {
        return this->fail(context +
                          ": invalid character among ' ,;{}()\\n\\t=' (needs "
                          "column mapping, which LakeVilla does not support)");
      }
      std::string lower = name;
      std::transform(lower.begin(), lower.end(), lower.begin(),
                     [](unsigned char c) { return std::tolower(c); });
      if (!names.insert(lower).second) {
        return this->fail(context + ": duplicate column name");
      }

      w.StartObject();
      w.Key("name");
      w.String(name.c_str());
      w.Key("type");
      if (!this->type(field->type(), context, w)) {
        return false;
      }
      w.Key("nullable");
      w.Bool(field->nullable());
      w.Key("metadata");
      w.StartObject();
      if (field->metadata()) {
        auto& metadata = *field->metadata();
        for (int64_t i = 0; i < metadata.size(); i++) {
          w.Key(metadata.key(i).c_str());
          w.String(metadata.value(i).c_str());
        }
      }
      w.EndObject();
      w.EndObject();
    }
    w.EndArray();
    return w.EndObject();
  }
};

}  // namespace

std::optional<LHHelpers::DeltaSchema> LHHelpers::to_delta_schema(
    const arrow::Schema& schema, std::string* error) {
  Converter converter;
  rapidjson::StringBuffer buffer;
  Writer writer(buffer);
  if (!converter.fields(schema.fields(), "", writer)) {
    if (error) {
      *error = converter.error;
    }
    return std::nullopt;
  }
  DeltaSchema result;
  result.schema_string = buffer.GetString();
  result.features.assign(converter.features.begin(), converter.features.end());
  return result;
}

std::string LHHelpers::random_uuid() {
  thread_local std::mt19937_64 generator{std::random_device{}()};
  uint64_t high = generator();
  uint64_t low = generator();
  // version 4, variant 1
  high = (high & 0xFFFFFFFFFFFF0FFFULL) | 0x0000000000004000ULL;
  low = (low & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;
  char text[37];
  std::snprintf(text, sizeof(text), "%08x-%04x-%04x-%04x-%012llx",
                static_cast<unsigned>(high >> 32),
                static_cast<unsigned>((high >> 16) & 0xFFFF),
                static_cast<unsigned>(high & 0xFFFF),
                static_cast<unsigned>(low >> 48),
                static_cast<unsigned long long>(low & 0xFFFFFFFFFFFFULL));
  return text;
}

namespace {

std::string json_text(const rapidjson::Value& value) {
  rapidjson::StringBuffer buffer;
  Writer writer(buffer);
  value.Accept(writer);
  return std::string(buffer.GetString(), buffer.GetSize());
}

// Delta schema JSON -> Arrow types
struct Parser {
  std::string error;

  std::shared_ptr<arrow::DataType> fail(const std::string& message) {
    if (this->error.empty()) {
      this->error = message;
    }
    return nullptr;
  }

  std::shared_ptr<arrow::DataType> primitive(const std::string& name,
                                             const std::string& context) {
    if (name == "string") return arrow::utf8();
    if (name == "long") return arrow::int64();
    if (name == "integer") return arrow::int32();
    if (name == "short") return arrow::int16();
    if (name == "byte") return arrow::int8();
    if (name == "float") return arrow::float32();
    if (name == "double") return arrow::float64();
    if (name == "boolean") return arrow::boolean();
    if (name == "binary") return arrow::binary();
    if (name == "date") return arrow::date32();
    if (name == "timestamp") {
      return arrow::timestamp(arrow::TimeUnit::MICRO, "UTC");
    }
    if (name == "timestamp_ntz") return arrow::timestamp(arrow::TimeUnit::MICRO);
    if (name == "void") return arrow::null();
    if (name == "decimal") return arrow::decimal128(10, 0);
    // CHAR/VARCHAR are strings (Spark keeps the length in the metadata)
    if (name.rfind("char(", 0) == 0 || name.rfind("varchar(", 0) == 0) {
      return arrow::utf8();
    }
    int precision, scale;
    if (std::sscanf(name.c_str(), "decimal(%d ,%d )", &precision, &scale) ==
        2) {
      if (precision < 1 || precision > 38 || scale < 0 || scale > precision) {
        return this->fail(context + ": invalid " + name);
      }
      return arrow::decimal128(precision, scale);
    }
    return this->fail(context + ": unsupported Delta type " + name);
  }

  std::shared_ptr<arrow::DataType> type(const rapidjson::Value& value,
                                        const std::string& context) {
    if (value.IsString()) {
      return this->primitive(value.GetString(), context);
    }
    if (!value.IsObject() || !value.HasMember("type") ||
        !value["type"].IsString()) {
      return this->fail(context + ": invalid type " + json_text(value));
    }
    std::string kind = value["type"].GetString();
    if (kind == "struct") {
      auto fields = this->fields(value, context + ".");
      return fields ? arrow::struct_(*fields) : nullptr;
    }
    if (kind == "array" && value.HasMember("elementType")) {
      auto element = this->type(value["elementType"], context + ".element");
      if (!element) {
        return nullptr;
      }
      return arrow::list(
          arrow::field("element", element, flag(value, "containsNull")));
    }
    if (kind == "map" && value.HasMember("keyType") &&
        value.HasMember("valueType")) {
      auto key = this->type(value["keyType"], context + ".key");
      auto item = this->type(value["valueType"], context + ".value");
      if (!key || !item) {
        return nullptr;
      }
      return arrow::map(
          key, arrow::field("value", item, flag(value, "valueContainsNull")));
    }
    return this->fail(context + ": unsupported Delta type " + kind);
  }

  // nullable, containsNull, valueContainsNull (default: true)
  static bool flag(const rapidjson::Value& value, const char* name) {
    return !value.HasMember(name) || !value[name].IsBool() ||
           value[name].GetBool();
  }

  std::optional<arrow::FieldVector> fields(const rapidjson::Value& value,
                                           const std::string& prefix) {
    if (!value.HasMember("fields") || !value["fields"].IsArray()) {
      this->fail(prefix + ": struct without fields");
      return std::nullopt;
    }
    arrow::FieldVector result;
    for (auto& field : value["fields"].GetArray()) {
      if (!field.IsObject() || !field.HasMember("name") ||
          !field["name"].IsString() || !field.HasMember("type")) {
        this->fail(prefix + ": invalid field " + json_text(field));
        return std::nullopt;
      }
      std::string name = field["name"].GetString();
      auto type = this->type(field["type"], prefix + name);
      if (!type) {
        return std::nullopt;
      }
      std::shared_ptr<arrow::KeyValueMetadata> metadata;
      if (field.HasMember("metadata") && field["metadata"].IsObject() &&
          field["metadata"].MemberCount() > 0) {
        metadata = std::make_shared<arrow::KeyValueMetadata>();
        for (auto& entry : field["metadata"].GetObject()) {
          metadata->Append(entry.name.GetString(),
                           entry.value.IsString() ? entry.value.GetString()
                                                  : json_text(entry.value));
        }
      }
      result.push_back(
          arrow::field(name, type, flag(field, "nullable"), metadata));
    }
    return result;
  }
};

}  // namespace

std::optional<std::shared_ptr<arrow::Schema>> LHHelpers::from_delta_schema(
    const std::string& schema_string, std::string* error) {
  rapidjson::Document document;
  document.Parse(schema_string.c_str(), schema_string.size());
  Parser parser;
  std::optional<arrow::FieldVector> fields;
  if (document.HasParseError() || !document.IsObject()) {
    parser.fail("schemaString is not a JSON object");
  } else {
    fields = parser.fields(document, "");
  }
  if (!fields) {
    if (error) {
      *error = parser.error;
    }
    return std::nullopt;
  }
  return arrow::schema(*fields);
}

std::optional<LHHelpers::TableSchema> LHHelpers::table_schema(
    const std::string& metadata_json, std::string* error) {
  rapidjson::Document metadata;
  metadata.Parse(metadata_json.c_str(), metadata_json.size());
  if (metadata.HasParseError() || !metadata.IsObject() ||
      !metadata.HasMember("schemaString") ||
      !metadata["schemaString"].IsString()) {
    if (error) {
      *error = "metaData has no schemaString";
    }
    return std::nullopt;
  }
  auto schema = from_delta_schema(metadata["schemaString"].GetString(), error);
  if (!schema) {
    return std::nullopt;
  }
  TableSchema result;
  result.schema = *schema;
  if (metadata.HasMember("partitionColumns") &&
      metadata["partitionColumns"].IsArray()) {
    for (auto& column : metadata["partitionColumns"].GetArray()) {
      if (column.IsString()) {
        result.partition_columns.push_back(column.GetString());
      }
    }
  }
  if (metadata.HasMember("configuration") &&
      metadata["configuration"].IsObject()) {
    auto& configuration = metadata["configuration"];
    auto mode = configuration.FindMember("delta.columnMapping.mode");
    if (mode != configuration.MemberEnd() && mode->value.IsString()) {
      std::string value = mode->value.GetString();
      result.column_mapping = value == "name" || value == "id";
    }
  }
  return result;
}
