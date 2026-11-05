// conform_to_schema: tables of data files -> the logical schema of the Delta
// table (see DeltaSchema.hpp)
#include <arrow/compute/cast.h>

#include <algorithm>
#include <cctype>
#include <limits>

#include "Helpers/DeltaSchema.hpp"

namespace {

const char* kPhysicalName = "delta.columnMapping.physicalName";

std::string lower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return text;
}

// the column name of a schema field in the data files
std::string file_name(const arrow::Field& field, bool physical_names) {
  if (physical_names && field.metadata()) {
    auto physical = field.metadata()->Get(kPhysicalName);
    if (physical.ok()) {
      return *physical;
    }
  }
  return field.name();
}

// index of the field with this name (exact match first, then
// case-insensitive as Delta column names are); -1 if there is none
int find_field(const arrow::FieldVector& fields, const std::string& name) {
  for (size_t i = 0; i < fields.size(); i++) {
    if (fields[i]->name() == name) {
      return static_cast<int>(i);
    }
  }
  auto wanted = lower(name);
  for (size_t i = 0; i < fields.size(); i++) {
    if (lower(fields[i]->name()) == wanted) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

int64_t per_second(arrow::TimeUnit::type unit) {
  switch (unit) {
    case arrow::TimeUnit::SECOND:
      return 1;
    case arrow::TimeUnit::MILLI:
      return 1000;
    case arrow::TimeUnit::MICRO:
      return 1000000;
    case arrow::TimeUnit::NANO:
      return 1000000000;
  }
  return 1;
}

int64_t floor_div(int64_t value, int64_t divisor) {
  int64_t quotient = value / divisor;
  if (value % divisor != 0 && value < 0) {
    quotient--;
  }
  return quotient;
}

// timestamps in another unit or time zone; finer units are truncated
// (floor), as Spark does for INT96 timestamps
arrow::Result<std::shared_ptr<arrow::Array>> convert_timestamps(
    const std::shared_ptr<arrow::Array>& source,
    const std::shared_ptr<arrow::DataType>& target,
    const std::string& context) {
  auto from = static_cast<const arrow::TimestampType&>(*source->type()).unit();
  auto to = static_cast<const arrow::TimestampType&>(*target).unit();
  if (from == to) {
    // only the time zone differs: values are UTC in both (Delta timestamp)
    // or wall-clock times in both (timestamp_ntz)
    return source->View(target);
  }
  auto& values = static_cast<const arrow::TimestampArray&>(*source);
  arrow::TimestampBuilder builder(target, arrow::default_memory_pool());
  ARROW_RETURN_NOT_OK(builder.Reserve(values.length()));
  int64_t from_rate = per_second(from), to_rate = per_second(to);
  for (int64_t i = 0; i < values.length(); i++) {
    if (values.IsNull(i)) {
      builder.UnsafeAppendNull();
      continue;
    }
    int64_t value = values.Value(i);
    if (to_rate >= from_rate) {
      int64_t factor = to_rate / from_rate;
      if (value > std::numeric_limits<int64_t>::max() / factor ||
          value < std::numeric_limits<int64_t>::min() / factor) {
        return arrow::Status::Invalid(context, ": timestamp out of range");
      }
      builder.UnsafeAppend(value * factor);
    } else {
      builder.UnsafeAppend(floor_div(value, from_rate / to_rate));
    }
  }
  return builder.Finish();
}

arrow::Result<std::shared_ptr<arrow::Array>> conform_array(
    const std::shared_ptr<arrow::Array>& source,
    const std::shared_ptr<arrow::Field>& target, bool physical_names,
    const std::string& context);

// struct (also the key/value entries of a map: by_position)
arrow::Result<std::shared_ptr<arrow::Array>> conform_struct(
    const std::shared_ptr<arrow::Array>& source,
    const std::shared_ptr<arrow::DataType>& target, bool physical_names,
    bool by_position, const std::string& context) {
  auto& data = *source->data();
  auto& source_fields = source->type()->fields();
  // children of the struct are not sliced by its offset
  int64_t length = data.child_data.empty() ? data.offset + data.length
                                           : data.child_data[0]->length;
  std::vector<std::shared_ptr<arrow::ArrayData>> children;
  for (int i = 0; i < target->num_fields(); i++) {
    auto& field = target->field(i);
    int index = by_position
                    ? (i < static_cast<int>(source_fields.size()) ? i : -1)
                    : find_field(source_fields, file_name(*field,
                                                          physical_names));
    std::shared_ptr<arrow::Array> child;
    if (index < 0) {
      ARROW_ASSIGN_OR_RAISE(child,
                            arrow::MakeArrayOfNull(field->type(), length));
    } else {
      ARROW_ASSIGN_OR_RAISE(
          child, conform_array(arrow::MakeArray(data.child_data[index]), field,
                               physical_names, context + "." + field->name()));
    }
    children.push_back(child->data());
  }
  return arrow::MakeArray(arrow::ArrayData::Make(target, data.length,
                                                 {data.buffers[0]}, children,
                                                 data.null_count, data.offset));
}

// list and map (a list of key/value entries)
arrow::Result<std::shared_ptr<arrow::Array>> conform_list(
    const std::shared_ptr<arrow::Array>& source,
    const std::shared_ptr<arrow::DataType>& target, bool physical_names,
    const std::string& context) {
  auto& data = *source->data();
  auto value_field = static_cast<const arrow::ListType&>(*target).value_field();
  std::shared_ptr<arrow::Array> values;
  if (target->id() == arrow::Type::MAP) {
    ARROW_ASSIGN_OR_RAISE(
        values, conform_struct(arrow::MakeArray(data.child_data[0]),
                               value_field->type(), physical_names, true,
                               context));
  } else {
    ARROW_ASSIGN_OR_RAISE(
        values, conform_array(arrow::MakeArray(data.child_data[0]),
                              value_field, physical_names, context + ".element"));
  }
  return arrow::MakeArray(arrow::ArrayData::Make(
      target, data.length, data.buffers, {values->data()}, data.null_count,
      data.offset));
}

arrow::Result<std::shared_ptr<arrow::Array>> conform_array(
    const std::shared_ptr<arrow::Array>& source,
    const std::shared_ptr<arrow::Field>& target, bool physical_names,
    const std::string& context) {
  auto& type = target->type();
  if (source->type()->Equals(*type, /*check_metadata=*/false)) {
    return source;
  }
  auto id = source->type_id();
  if (id == arrow::Type::NA) {
    return arrow::MakeArrayOfNull(type, source->length());
  }
  if (type->id() == arrow::Type::TIMESTAMP && id == arrow::Type::TIMESTAMP) {
    return convert_timestamps(source, type, context);
  }
  if (type->id() == arrow::Type::STRUCT && id == arrow::Type::STRUCT) {
    return conform_struct(source, type, physical_names, false, context);
  }
  if ((type->id() == arrow::Type::LIST && id == arrow::Type::LIST) ||
      (type->id() == arrow::Type::MAP && id == arrow::Type::MAP)) {
    return conform_list(source, type, physical_names, context);
  }
  if (type->id() == arrow::Type::LIST && id == arrow::Type::LARGE_LIST) {
    // 64-bit offsets -> 32-bit offsets, then the elements
    auto element = static_cast<const arrow::LargeListType&>(*source->type())
                       .value_field();
    auto narrowed = arrow::compute::Cast(*source, arrow::list(element));
    if (!narrowed.ok()) {
      return narrowed.status().WithMessage(context, ": ",
                                           narrowed.status().message());
    }
    return conform_list(*narrowed, type, physical_names, context);
  }
  // other conversions (wider integers and floats, decimals, large and
  // dictionary-encoded strings, ...) must not lose information
  auto cast = arrow::compute::Cast(*source, type,
                                   arrow::compute::CastOptions::Safe());
  if (!cast.ok()) {
    return arrow::Status::TypeError(context, ": cannot convert ",
                                    source->type()->ToString(), " to ",
                                    type->ToString(), " (",
                                    cast.status().message(), ")");
  }
  return *cast;
}

}  // namespace

arrow::Result<std::shared_ptr<arrow::Table>> LHHelpers::conform_to_schema(
    const std::shared_ptr<arrow::Table>& table, const arrow::Schema& schema,
    const ConformOptions& options) {
  if (!table) {
    return arrow::Status::Invalid("no table");
  }
  int64_t rows = table->num_rows();
  auto& file_fields = table->schema()->fields();
  std::vector<bool> used(file_fields.size(), false);
  std::vector<std::shared_ptr<arrow::ChunkedArray>> columns;

  for (auto& field : schema.fields()) {
    std::shared_ptr<arrow::ChunkedArray> column;

    const std::optional<std::string>* partition_value = nullptr;
    if (options.partition_values) {
      for (auto& entry : *options.partition_values) {
        if (lower(entry.first) == lower(field->name())) {
          partition_value = &entry.second;
        }
      }
    }

    if (partition_value) {
      // partition columns are not stored in the data files
      std::shared_ptr<arrow::Array> array;
      if (!*partition_value) {
        ARROW_ASSIGN_OR_RAISE(array,
                              arrow::MakeArrayOfNull(field->type(), rows));
      } else {
        auto scalar = arrow::Scalar::Parse(field->type(), **partition_value);
        if (!scalar.ok()) {
          return arrow::Status::Invalid(
              "partition column ", field->name(), ": cannot parse '",
              **partition_value, "' as ", field->type()->ToString());
        }
        ARROW_ASSIGN_OR_RAISE(array, arrow::MakeArrayFromScalar(**scalar,
                                                                rows));
      }
      column = std::make_shared<arrow::ChunkedArray>(array);
    } else {
      int index = find_field(file_fields,
                             file_name(*field, options.physical_names));
      if (index < 0) {
        if (!field->nullable()) {
          return arrow::Status::Invalid("column ", field->name(),
                                        " is NOT NULL but missing in the "
                                        "file");
        }
        ARROW_ASSIGN_OR_RAISE(auto array,
                              arrow::MakeArrayOfNull(field->type(), rows));
        column = std::make_shared<arrow::ChunkedArray>(array);
      } else {
        used[index] = true;
        auto source = table->column(index);
        arrow::ArrayVector chunks;
        for (auto& chunk : source->chunks()) {
          ARROW_ASSIGN_OR_RAISE(
              auto converted, conform_array(chunk, field, options.physical_names,
                                            "column " + field->name()));
          chunks.push_back(converted);
        }
        ARROW_ASSIGN_OR_RAISE(column, arrow::ChunkedArray::Make(
                                          std::move(chunks), field->type()));
      }
    }

    if (!field->nullable() && column->null_count() > 0) {
      return arrow::Status::Invalid("column ", field->name(),
                                    " is NOT NULL but contains nulls");
    }
    columns.push_back(column);
  }

  if (options.reject_extra_columns) {
    for (size_t i = 0; i < used.size(); i++) {
      if (!used[i]) {
        return arrow::Status::Invalid("column ", file_fields[i]->name(),
                                      " is not part of the table schema");
      }
    }
  }

  return arrow::Table::Make(std::make_shared<arrow::Schema>(schema), columns,
                            rows);
}
