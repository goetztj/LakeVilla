#include "ManagerDeltaLake.hpp"

#include <cctype>
#include <charconv>
#include <cstdlib>
#include <map>
#include <set>

LHTransactions::TransactionManagerDeltaLake::TransactionManagerDeltaLake(
    LHTransactions::LakeVillaConf levels, std::string& path_to_table,
    StorageConnector::Connector* conn_ptr, uint32_t transaction_id)
    : TransactionManager(path_to_table, conn_ptr, transaction_id) {
  // this->global_log_exists = true;
  this->working_dir = "";
  this->tables.clear();
  this->next_step = 0;
  this->prev_markers = {};
  this->found = {"", ""};
  this->times_redo = 0;

  this->relation = std::make_unique<LHTransactions::GlobalRelation>();

  this->levels = levels;

  while (this->levels.size() < 3) {
    this->levels.push_back(false);
  }

  this->explicit_log = true;
}

LHTransactions::TransactionManagerDeltaLake::~TransactionManagerDeltaLake() {
  this->tables.clear();
  this->relation = nullptr;
}

bool LHTransactions::TransactionManagerDeltaLake::begin_transaction_ycsb() {
  if (!this->connector_ptr) {
    std::cerr << "no connector set" << std::endl;
    return false;
  }

  this->relation = std::make_unique<LHTransactions::GlobalRelation>();

  // this->global_log_exists = true;

  this->working_dir = "";
  this->tables.clear();
  this->next_step = 0;
  this->explicit_log = true;
  this->prev_markers = {};
  this->found = {"", ""};
  this->times_redo = 0;
  // ops.clear();
  // double data_read;
  // double data_write;
  this->update_vals.clear();
  this->update_ops.clear();

  this->prev_markers.clear();
  // this->commit_diff.clear();

  this->local_log.clear();

  uint32_t retry_counter = 0;
  bool success = false;
  while (retry_counter < NUM_RETRIES) {
    if (this->begin(this->base_path)) {
      success = true;
      break;
    } else {
      std::this_thread::sleep_for(std::chrono::milliseconds(WAIT_MILLISECONDS));
      retry_counter++;
    }
  }

  if (!success) {
    std::cout << "unable to execute 'begin transaction'" << std::endl;
    return false;
  }

  return true;
}

// unused: interactive transaction, not called anywhere
// bool LHTransactions::TransactionManagerDeltaLake::begin_transaction() {
//   uint32_t retry_counter = 0;
//   bool success = false;
//   while (retry_counter < NUM_RETRIES) {
//     if (this->begin(this->base_path)) {
//       success = true;
//       break;
//     } else {
//       std::this_thread::sleep_for(std::chrono::milliseconds(WAIT_MILLISECONDS));
//       retry_counter++;
//     }
//   }
//
//   if (!success) {
//     return false;
//   }
//
//   bool end = false;
//
//   while (!end) {
//     std::cout << "Done! Now describe your next step" << std::endl
//               << "[0] read a file" << std::endl
//               << "[1] add a file" << std::endl
//               << "[2] remove a file" << std::endl
//               << "[3] commit" << std::endl
//               << "[4] forced reroll" << std::endl
//               << "[5] abort" << std::endl
//               << "[6] open a table" << std::endl;
//     std::string input;
//     std::cin >> input;
//
//     uint32_t number = std::stoi(input);
//
//     bool result = false;
//
//     switch (number) {
//       case 0:
//         result = this->read_file();
//         break;
//       case 1:
//         result = this->add_file();
//         break;
//       case 2:
//         result = this->remove_file();
//         break;
//       case 3:
//         result = this->commit();
//         end = true;
//         break;
//       case 4:
//         result = this->reroll();
//         break;
//       case 5:
//         result = this->abort();
//         end = true;
//         break;
//       case 6:
//         result = this->open_new_table();
//         break;
//       default:
//         result = true;
//         break;
//     }
//
//     if (!result) {
//       if (this->reroll()) {
//         // //std::cout << "success" << std::endl;
//       } else {
//         // //std::cout << "aborting transaction" << std::endl;
//         return false;
//       }
//     }
//   }
//
//   return true;
// }

// unused: only called by the unused interactive functions
// LHTransactions::TableStats*
// LHTransactions::TransactionManagerDeltaLake::select_table() {
//   std::string in;
//   std::cin >> in;
//
//   uint32_t selection = std::stoi(in);
//
//   if (selection >= 0 && selection < this->tables.size()) {
//     return this->tables[selection].get();
//   } else {
//     return nullptr;
//   }
// }

namespace {
// Delta column names are case-insensitive
bool same_column(const std::string& a, const std::string& b) {
  return a.size() == b.size() &&
         std::equal(a.begin(), a.end(), b.begin(),
                    [](unsigned char x, unsigned char y) {
                      return std::tolower(x) == std::tolower(y);
                    });
}
}  // namespace

std::shared_ptr<const LHHelpers::TableSchema>
LHTransactions::TransactionManagerDeltaLake::table_schema(TableStats& table,
                                                          std::string* error) {
  std::lock_guard<std::mutex> guard(this->schema_mutex);
  std::pair<const void*, int64_t> source = {
      table.delta_log.get(),
      table.delta_log ? table.delta_log->get_metadata_version() : -1};
  if (source != table.schema_source) {
    table.schema = nullptr;
    table.schema_error.clear();
    table.schema_source = source;
    auto metadata = table.delta_log ? table.delta_log->get_metadata() : "";
    if (!metadata.empty()) {
      std::string message;
      auto parsed = LHHelpers::table_schema(metadata, &message);
      if (parsed) {
        table.schema =
            std::make_shared<const LHHelpers::TableSchema>(std::move(*parsed));
      } else {
        table.schema_error = "schema of " + table.base_path + ": " + message;
      }
    }
  }
  if (error) {
    *error = table.schema_error;
  }
  return table.schema;
}

std::shared_ptr<arrow::Table>
LHTransactions::TransactionManagerDeltaLake::read_data_file(
    TableStats& table, const std::string& path) {
  std::string error;
  auto schema = this->table_schema(table, &error);
  if (!error.empty()) {
    std::cerr << error << std::endl;
    return nullptr;
  }
  auto content = this->connector_ptr->read2(path);
  if (!content->data) {
    std::cerr << "unable to read " << path << std::endl;
    return nullptr;
  }
  auto file = LHHelpers::readParquetAsTable(content->data, content->length);
  if (!file || !schema) {
    // without metaData the file is used as stored
    return file;
  }
  LHHelpers::ConformOptions options;
  options.physical_names = schema->column_mapping;
  std::vector<std::pair<std::string, std::optional<std::string>>> partitions;
  if (!schema->partition_columns.empty()) {
    partitions = table.delta_log->get_partition_values(path);
    options.partition_values = &partitions;
  }
  auto conformed = LHHelpers::conform_to_schema(file, *schema->schema, options);
  if (!conformed.ok()) {
    std::cerr << "data file " << path << " does not match the table schema: "
              << conformed.status().ToString() << std::endl;
    return nullptr;
  }
  return *conformed;
}

std::shared_ptr<arrow::Table>
LHTransactions::TransactionManagerDeltaLake::conform_for_write(
    TableStats& table, const std::shared_ptr<arrow::Table>& rows) {
  std::string error;
  auto schema = this->table_schema(table, &error);
  if (!error.empty()) {
    std::cerr << error << std::endl;
    return nullptr;
  }
  if (!schema || !rows) {
    return rows;
  }
  if (!schema->partition_columns.empty() || schema->column_mapping) {
    std::cerr << "LakeVilla does not write data files of tables with "
                 "partition columns or column mapping: "
              << table.base_path << std::endl;
    return nullptr;
  }
  LHHelpers::ConformOptions options;
  options.reject_extra_columns = true;
  auto conformed = LHHelpers::conform_to_schema(rows, *schema->schema, options);
  if (!conformed.ok()) {
    std::cerr << "rows do not match the schema of " << table.base_path << ": "
              << conformed.status().ToString() << std::endl;
    return nullptr;
  }
  return *conformed;
}

namespace {

using KeyMatch = LHExecutor::LHDL::DeltaLogManager::KeyMatch;

// Delta truncates the min/max statistics of strings to this many characters
// (delta.dataSkippingStringPrefixLength)
const size_t STRING_STATS_PREFIX = 32;

size_t code_points(const std::string& text) {
  size_t count = 0;
  for (unsigned char c : text) {
    if ((c & 0xC0) != 0x80) {
      count++;
    }
  }
  return count;
}

// the first n characters (code points) of a UTF-8 string
std::string first_code_points(const std::string& text, size_t n) {
  size_t count = 0;
  for (size_t i = 0; i < text.size(); i++) {
    if ((static_cast<unsigned char>(text[i]) & 0xC0) != 0x80) {
      if (count == n) {
        return text.substr(0, i);
      }
      count++;
    }
  }
  return text;
}

std::optional<int64_t> parse_integer(const std::string& text) {
  int64_t value;
  auto end = text.data() + text.size();
  auto [ptr, ec] = std::from_chars(text.data(), end, value);
  if (ec != std::errc() || ptr != end) {
    return std::nullopt;
  }
  return value;
}

std::optional<int64_t> parse_date(const std::string& text) {
  auto scalar = arrow::Scalar::Parse(arrow::date32(), text);
  if (!scalar.ok()) {
    return std::nullopt;
  }
  return static_cast<const arrow::Date32Scalar&>(**scalar).value;
}

// a key of the table's first column: classifies data files by their
// statistics and finds the rows of the key
struct KeyMatcher {
  enum class Kind { Integer, String, Date, Other };
  Kind kind = Kind::Other;
  std::string key;
  int64_t number = 0;
  std::shared_ptr<arrow::Scalar> scalar;
  // the key is a value of the column's type (otherwise no row has it)
  bool valid = false;

  KeyMatcher(const std::shared_ptr<arrow::DataType>& type,
             const std::string& key)
      : key(key) {
    auto parsed = arrow::Scalar::Parse(type, key);
    if (!parsed.ok()) {
      return;
    }
    this->scalar = *parsed;
    this->valid = true;
    switch (type->id()) {
      case arrow::Type::INT8:
        this->number = static_cast<const arrow::Int8Scalar&>(**parsed).value;
        this->kind = Kind::Integer;
        break;
      case arrow::Type::INT16:
        this->number = static_cast<const arrow::Int16Scalar&>(**parsed).value;
        this->kind = Kind::Integer;
        break;
      case arrow::Type::INT32:
        this->number = static_cast<const arrow::Int32Scalar&>(**parsed).value;
        this->kind = Kind::Integer;
        break;
      case arrow::Type::INT64:
        this->number = static_cast<const arrow::Int64Scalar&>(**parsed).value;
        this->kind = Kind::Integer;
        break;
      case arrow::Type::DATE32:
        this->number = static_cast<const arrow::Date32Scalar&>(**parsed).value;
        this->kind = Kind::Date;
        break;
      case arrow::Type::STRING:
      case arrow::Type::LARGE_STRING:
        this->kind = Kind::String;
        break;
      default:
        break;
    }
  }

  // min/max: statistics of the column (nullptr: unknown); records:
  // numRecords (-1: unknown). Files without statistics may hold the key;
  // for other types than strings, integers and dates (e.g. timestamps,
  // whose statistics Delta truncates to milliseconds) every file may.
  KeyMatch classify(const std::string* min, const std::string* max,
                    int64_t records) const {
    if (!this->valid || records == 0) {
      return KeyMatch::None;
    }
    if (!min || !max) {
      return KeyMatch::Possible;
    }
    switch (this->kind) {
      case Kind::Integer:
      case Kind::Date: {
        auto low = this->kind == Kind::Integer ? parse_integer(*min)
                                               : parse_date(*min);
        auto high = this->kind == Kind::Integer ? parse_integer(*max)
                                                : parse_date(*max);
        if (!low || !high) {
          return KeyMatch::Possible;
        }
        if (this->number < *low || this->number > *high) {
          return KeyMatch::None;
        }
        return records == 1 && *low == this->number && *high == this->number
                   ? KeyMatch::Exact
                   : KeyMatch::Possible;
      }
      case Kind::String: {
        // byte order of UTF-8, as Spark compares strings; a truncated
        // minimum is a prefix of the real one, so the key is below both
        if (this->key < *min) {
          return KeyMatch::None;
        }
        if (this->key > *max) {
          // a truncated maximum: the real one starts with its first
          // characters (Delta may append a character to the prefix)
          if (code_points(*max) < STRING_STATS_PREFIX) {
            return KeyMatch::None;
          }
          auto prefix = first_code_points(*max, STRING_STATS_PREFIX);
          if (this->key.compare(0, prefix.size(), prefix) != 0) {
            return KeyMatch::None;
          }
        }
        return records == 1 && *min == this->key && *max == this->key &&
                       code_points(this->key) < STRING_STATS_PREFIX
                   ? KeyMatch::Exact
                   : KeyMatch::Possible;
      }
      default:
        return KeyMatch::Possible;
    }
  }

  bool equals(const arrow::Array& array, int64_t i) const {
    switch (array.type_id()) {
      case arrow::Type::STRING:
        return static_cast<const arrow::StringArray&>(array).GetView(i) ==
               this->key;
      case arrow::Type::LARGE_STRING:
        return static_cast<const arrow::LargeStringArray&>(array).GetView(i) ==
               this->key;
      case arrow::Type::INT8:
        return static_cast<const arrow::Int8Array&>(array).Value(i) ==
               this->number;
      case arrow::Type::INT16:
        return static_cast<const arrow::Int16Array&>(array).Value(i) ==
               this->number;
      case arrow::Type::INT32:
        return static_cast<const arrow::Int32Array&>(array).Value(i) ==
               this->number;
      case arrow::Type::INT64:
        return static_cast<const arrow::Int64Array&>(array).Value(i) ==
               this->number;
      case arrow::Type::DATE32:
        return static_cast<const arrow::Date32Array&>(array).Value(i) ==
               this->number;
      default: {
        auto value = array.GetScalar(i);
        return value.ok() && (*value)->Equals(*this->scalar);
      }
    }
  }

  // the rows of the key in a column of the key's type
  std::vector<int64_t> rows_of(const arrow::ChunkedArray& column) const {
    std::vector<int64_t> rows;
    int64_t offset = 0;
    for (auto& chunk : column.chunks()) {
      for (int64_t i = 0; i < chunk->length(); i++) {
        if (!chunk->IsNull(i) && this->equals(*chunk, i)) {
          rows.push_back(offset + i);
        }
      }
      offset += chunk->length();
    }
    return rows;
  }
};

// the rows of a table at the given (ascending) indices
std::shared_ptr<arrow::Table> select_rows(
    const std::shared_ptr<arrow::Table>& table,
    const std::vector<int64_t>& rows) {
  if (static_cast<int64_t>(rows.size()) == table->num_rows()) {
    return table;
  }
  std::vector<std::shared_ptr<arrow::Table>> parts;
  for (size_t i = 0; i < rows.size();) {
    size_t j = i;
    while (j + 1 < rows.size() && rows[j + 1] == rows[j] + 1) {
      j++;
    }
    parts.push_back(table->Slice(rows[i], rows[j] - rows[i] + 1));
    i = j + 1;
  }
  if (parts.empty()) {
    return table->Slice(0, 0);
  }
  auto combined = arrow::ConcatenateTables(parts);
  return combined.ok() ? *combined : nullptr;
}

// the indices below n that are not in rows (ascending)
std::vector<int64_t> other_rows(int64_t n, const std::vector<int64_t>& rows) {
  std::vector<int64_t> result;
  size_t next = 0;
  for (int64_t i = 0; i < n; i++) {
    if (next < rows.size() && rows[next] == i) {
      next++;
    } else {
      result.push_back(i);
    }
  }
  return result;
}

}  // namespace

std::optional<std::vector<LHTransactions::TransactionManagerDeltaLake::KeyFile>>
LHTransactions::TransactionManagerDeltaLake::find_key_files(
    TableStats& table, const std::string& key, bool read_exact,
    uint32_t skip_update) {
  std::string error;
  auto schema = this->table_schema(table, &error);
  if (!error.empty()) {
    std::cerr << error << std::endl;
    return std::nullopt;
  }

  std::vector<KeyFile> files;
  if (!schema || schema->schema->num_fields() == 0) {
    // without metaData: the first column of the statistics, whole files
    auto path = table.delta_log->findFileWithStats(0, key);
    if (!path.empty()) {
      KeyFile file;
      file.path = path;
      file.exact = true;
      files.push_back(std::move(file));
    }
    if (read_exact && !this->read_key_files(table, files)) {
      return std::nullopt;
    }
    return files;
  }

  auto field = schema->schema->field(0);
  KeyMatcher matcher(field->type(), key);
  // with column mapping the statistics use the physical name
  auto column = field->name();
  if (schema->column_mapping && field->metadata()) {
    auto physical = field->metadata()->Get("delta.columnMapping.physicalName");
    if (physical.ok()) {
      column = *physical;
    }
  }
  auto candidates = table.delta_log->find_files(
      column, [&matcher](const std::string* min, const std::string* max,
                         int64_t records) {
        return matcher.classify(min, max, records);
      });

  for (auto& [path, match] : candidates) {
    if (skip_update != UINT32_MAX) {
      // the new rows of the update itself
      std::lock_guard<std::mutex> guard(this->state_mutex);
      auto created = table.created_files.find(path);
      if (created != table.created_files.end() &&
          created->second->update_id == skip_update) {
        continue;
      }
    }
    KeyFile file;
    file.path = path;
    file.exact = match == KeyMatch::Exact;
    if (file.exact && !read_exact) {
      files.push_back(std::move(file));
      continue;
    }
    // the statistics do not prove which rows the file holds: read it
    file.data = this->read_data_file(table, path);
    if (!file.data) {
      return std::nullopt;
    }
    file.rows = matcher.rows_of(*file.data->column(0));
    if (!file.rows.empty()) {
      files.push_back(std::move(file));
    }
  }
  return files;
}

bool LHTransactions::TransactionManagerDeltaLake::read_key_files(
    TableStats& table, std::vector<KeyFile>& files) {
  for (auto& file : files) {
    if (file.data) {
      continue;
    }
    // a single-row file of the key (statistics) or a file of a table
    // without metaData: all of its rows
    file.data = this->read_data_file(table, file.path);
    if (!file.data) {
      return false;
    }
    file.rows.clear();
    for (int64_t i = 0; i < file.data->num_rows(); i++) {
      file.rows.push_back(i);
    }
  }
  return true;
}

std::shared_ptr<arrow::Table>
LHTransactions::TransactionManagerDeltaLake::key_rows(
    const std::vector<KeyFile>& files) {
  std::vector<std::shared_ptr<arrow::Table>> parts;
  for (auto& file : files) {
    if (file.data) {
      parts.push_back(select_rows(file.data, file.rows));
    }
  }
  if (parts.empty()) {
    return nullptr;
  }
  if (parts.size() == 1) {
    return parts[0];
  }
  auto combined = arrow::ConcatenateTables(parts);
  if (!combined.ok()) {
    std::cerr << "unable to combine the rows of the key: "
              << combined.status().ToString() << std::endl;
    return nullptr;
  }
  return *combined;
}

bool LHTransactions::TransactionManagerDeltaLake::remove_key_rows(
    TableStats& table, KeyFile& file, uint32_t prev, bool log) {
  if (!file.all_rows()) {
    // the other rows of the file stay: they are written to a new file
    auto rest = select_rows(file.data,
                            other_rows(file.data->num_rows(), file.rows));
    rest = rest ? this->conform_for_write(table, rest) : nullptr;
    if (!rest) {
      return false;
    }
    auto encoded = LHHelpers::encodeAsParquet(rest);
    std::string path = this->new_data_file_path(table);
    if (!this->connector_ptr->write(path, encoded->first, encoded->second)) {
      return false;
    }
    auto stats = std::make_unique<FileStats>();
    stats->path = path;
    stats->size = encoded->second;
    stats->stats = LHHelpers::computeStatistics(*rest);
    if (log) {
      this->log_add_substep(table, path, UINT32_MAX);
    }
    this->register_created_file(table, std::move(stats));
  }
  if (log) {
    this->log_delete_substep(table, file.path, prev);
  }
  this->register_deleted_file(table, file.path);
  return true;
}

LHTransactions::TableStats*
LHTransactions::TransactionManagerDeltaLake::get_table_for_read(
    uint32_t table_id) {
  auto table = this->get_table(table_id);
  if (table) {
    // serializable isolation validates every table the transaction read
    table->was_read = true;
  }
  return table;
}

LHTransactions::TableStats*
LHTransactions::TransactionManagerDeltaLake::get_table(uint32_t table_id) {
  if (table_id >= this->tables.size()) {
    std::cerr << "unknown table id " << table_id << std::endl;
    return nullptr;
  }
  return this->tables[table_id].get();
}

void LHTransactions::TransactionManagerDeltaLake::read_table_simple(
    uint32_t table_id, uint32_t num_threads) {
  TableStats* table = this->get_table_for_read(table_id);
  if (!table) {
    return;
  }

  auto list = table->delta_log->get_list();

  std::vector<std::jthread> threads;
  for (auto& range : split_range(0, list.size(), num_threads)) {
    threads.emplace_back([this, &list, range] {
      this->read_part_simple(list, range.first, range.second);
    });
  }
}

void LHTransactions::TransactionManagerDeltaLake::read_partial_table_simple(
    uint32_t table_id, uint32_t skip, uint32_t num_threads) {
  TableStats* table = this->get_table_for_read(table_id);
  if (!table) {
    return;
  }

  auto list = table->delta_log->get_list();

  std::vector<std::jthread> threads;
  for (auto& range : split_range(0, list.size(), num_threads)) {
    threads.emplace_back([this, &list, range, skip] {
      this->read_part_simple_skip(list, range.first, range.second, skip);
    });
  }
}

void LHTransactions::TransactionManagerDeltaLake::head_table_simple(
    uint32_t table_id, uint32_t num_threads) {
  TableStats* table = this->get_table_for_read(table_id);
  if (!table) {
    return;
  }

  auto list = table->delta_log->get_list();

  std::vector<std::jthread> threads;
  for (auto& range : split_range(0, list.size(), num_threads)) {
    threads.emplace_back([this, &list, range] {
      this->head_part_simple(list, range.first, range.second);
    });
  }
}

std::shared_ptr<arrow::Table>
LHTransactions::TransactionManagerDeltaLake::read_table(uint32_t table_id,
                                                        uint32_t num_threads) {
  TableStats* table = this->get_table_for_read(table_id);
  if (!table) {
    return nullptr;
  }

  std::string error;
  auto schema = this->table_schema(*table, &error);
  if (!error.empty()) {
    std::cerr << error << std::endl;
    return nullptr;
  }

  auto list = table->delta_log->get_list();
  if (list.empty()) {
    // no rows; without metaData the columns are unknown
    if (!schema) {
      return nullptr;
    }
    auto empty = arrow::Table::MakeEmpty(schema->schema);
    return empty.ok() ? *empty : nullptr;
  }
  auto ranges = split_range(0, list.size(), num_threads);

  std::vector<std::shared_ptr<arrow::Table>> partial_tables(ranges.size());
  std::atomic<bool> success = true;
  {
    std::vector<std::jthread> threads;
    for (size_t i = 0; i < ranges.size(); i++) {
      threads.emplace_back([this, i, &partial_tables, &list, &ranges,
                            &success, table] {
        if (!this->read_part(partial_tables[i], list, ranges[i].first,
                             ranges[i].second, *table)) {
          success = false;
        }
      });
    }
  }

  if (!success) {
    return nullptr;
  }

  std::shared_ptr<arrow::Table> table_ptr = nullptr;

  for (auto& tbl : partial_tables) {
    if (table_ptr) {
      auto concatenated = arrow::ConcatenateTables({table_ptr, tbl});
      if (!concatenated.ok()) {
        std::cerr << "concat error: " << concatenated.status().ToString()
                  << std::endl;
        return nullptr;
      }
      table_ptr = *concatenated;
    } else {
      table_ptr = tbl;
    }
  }

  return table_ptr;
}

void LHTransactions::TransactionManagerDeltaLake::read_random_file_simple(
    uint32_t table_id, std::string& path) {
  TableStats* table = this->get_table_for_read(table_id);
  if (!table) {
    return;
  }

  auto list = table->delta_log->get_list();

  if (!list.empty()) {
    std::random_device s;
    std::mt19937 generator{s()};

    int max = list.size() - 1;

    std::uniform_int_distribution<> distr{0, max};

    auto number = distr(generator);

    path = list[number];

    std::jthread sub_thread([this, table, &path] {
      this->add_substep(path,
                        LHTransactions::TransactionManager::SubOpType::READ,
                        *table, nullptr);
    });

    connector_ptr->read_mock(path);

    sub_thread.join();
  }
}

std::shared_ptr<arrow::Table>
LHTransactions::TransactionManagerDeltaLake::read_random_file_as_table(
    uint32_t table_id, std::string& path) {
  TableStats* table = this->get_table_for_read(table_id);
  if (!table) {
    return {};
  }

  auto list = table->delta_log->get_list();

  if (!list.empty()) {
    std::random_device s;
    std::mt19937 generator{s()};

    int max = list.size() - 1;

    std::uniform_int_distribution<> distr{0, max};

    auto number = distr(generator);

    path = list[number];

    std::jthread sub_thread([this, table, &path] {
      this->add_substep(path,
                        LHTransactions::TransactionManager::SubOpType::READ,
                        *table, nullptr);
    });

    auto table_ptr = this->read_data_file(*table, path);
    sub_thread.join();
    return table_ptr;
  } else {
    return nullptr;
  }
}

std::shared_ptr<arrow::Table>
LHTransactions::TransactionManagerDeltaLake::read_file_as_table(
    uint32_t table_id, const std::string& key, std::string& path) {
  TableStats* table = this->get_table_for_read(table_id);
  if (!table) {
    return {};
  }

  // the rows of the key in all files that have them
  auto files = this->find_key_files(*table, key, false);
  if (!files || files->empty()) {
    path = "";
    return nullptr;
  }
  path = files->front().path;

  std::jthread sub_thread([this, &path, table, &key] {
    this->add_substep(path, LHTransactions::TransactionManager::SubOpType::READ,
                      *table, &key);
  });

  if (!this->read_key_files(*table, *files)) {
    return nullptr;
  }
  auto rows = this->key_rows(*files);
  sub_thread.join();
  return rows;
}

std::string LHTransactions::TransactionManagerDeltaLake::read_file(
    uint32_t table_id, const std::string& key,
    const std::vector<std::string>* fields,
    std::vector<std::pair<std::string, std::string>>& result,
    std::string& path) {
  TableStats* table = this->get_table_for_read(table_id);
  if (!table) {
    return {};
  }

  // the rows of the key in all files that have them
  auto files = this->find_key_files(*table, key, false);
  if (!files || files->empty()) {
    path = "";
    return "";
  }
  path = files->front().path;

  std::jthread sub_thread([this, &path, table, key] {
    this->add_substep(path, LHTransactions::TransactionManager::SubOpType::READ,
                      *table, &key);
  });

  if (!this->read_key_files(*table, *files)) {
    return "";
  }
  auto table_ptr = this->key_rows(*files);
  if (!table_ptr) {
    return "";
  }

  if (fields) {
    // only selected fields (YCSB: column 0 is the key)
    for (size_t i = 0; i < fields->size(); i++) {
      if (static_cast<int>(i + 1) >= table_ptr->num_columns()) {
        break;
      }
      result.emplace_back((*fields)[i], table_ptr->column(i + 1)->ToString());
    }
  } else {
    uint32_t counter = 0;
    for (auto& col : table_ptr->columns()) {
      result.emplace_back(table_ptr->ColumnNames()[counter], col->ToString());
      counter++;
    }
  }

  sub_thread.join();

  return path;
}

// unused: interactive read, only called by begin_transaction()
// bool LHTransactions::TransactionManagerDeltaLake::read_file() {
//   TableStats* table = nullptr;
//
//   while (!table) {
//     table = this->select_table();
//   }
//   table->was_read = true;
//
//   auto file_list = table->delta_log->get_list();
//
//   std::string in = "";
//   std::cin >> in;
//
//   uint32_t files_pos = std::stoi(in);
//
//   std::jthread sub_thread([this, &file_list, &files_pos, table] {
//     this->add_substep(file_list[files_pos],
//                       LHTransactions::TransactionManager::SubOpType::READ,
//                       *table, nullptr);
//   });
//
//   auto result = connector_ptr->read2(file_list[files_pos]);
//
//   if (result->data) {
//     std::cout << LHHelpers::readParquet(result->data, result->length)
//               << std::endl
//               << "----------" << std::endl;
//     table->in_bytes += result->length;
//     sub_thread.join();
//     return true;
//   } else {
//     sub_thread.join();
//     return false;
//   }
// }

std::string LHTransactions::TransactionManagerDeltaLake::new_data_file_path(
    TableStats& table) {
  thread_local std::mt19937_64 generator{std::random_device{}()};
  uint32_t step;
  {
    std::lock_guard<std::mutex> guard(this->state_mutex);
    step = this->next_step;
  }
  while (true) {
    std::stringstream stream;

    // the random part keeps names unique even if several clients share a
    // transaction id
    stream << table.base_path << "lhdata-" << this->transaction_id << "-"
           << std::time(0) << "-" << step << "-" << std::hex
           << generator() << ".parquet";

    if (!this->connector_ptr->check(stream.str())) {
      return stream.str();
    }
  }
}

void LHTransactions::TransactionManagerDeltaLake::register_created_file(
    TableStats& table, std::unique_ptr<FileStats> stats, bool overwrite) {
  std::lock_guard<std::mutex> guard(this->state_mutex);
  auto path = stats->path;
  // the statistics by column name (key lookups of this transaction)
  std::vector<std::string> columns, min, max;
  for (auto& column : stats->stats.columns) {
    if (column.has_min_max) {
      columns.push_back(column.name);
      min.push_back(column.min);
      max.push_back(column.max);
    }
  }
  LHExecutor::LHDL::DeltaLogEntry entry(LHExecutor::LHDL::DLOperation::ADD,
                                        std::move(columns), std::move(min),
                                        std::move(max),
                                        stats->stats.num_records);

  table.created_files.insert_or_assign(path, std::move(stats));

  if (table.deleted_files.count(path) != 0) {
    table.deleted_files.find(path)->second->valid = false;
  }

  table.delta_log->register_operation(LHExecutor::LHDL::DLOperation::ADD, path,
                                      std::move(entry), true, overwrite);
}

void LHTransactions::TransactionManagerDeltaLake::register_deleted_file(
    TableStats& table, const std::string& path) {
  std::lock_guard<std::mutex> guard(this->state_mutex);
  auto stats = std::make_unique<FileStats>();
  stats->path = path;
  stats->size = table.delta_log->get_size(path, true);

  table.deleted_files.insert_or_assign(path, std::move(stats));

  if (table.created_files.count(path) != 0) {
    table.created_files.find(path)->second->valid = false;
  }

  std::string copy = path;
  table.delta_log->register_operation(LHExecutor::LHDL::DLOperation::DELETE,
                                      copy, true);
}

void LHTransactions::TransactionManagerDeltaLake::log_add_substep(
    TableStats& table, std::string& path, uint32_t prev) {
  if (prev != UINT32_MAX) {
    std::string key_str = std::to_string(prev);
    this->add_substep(path, LHTransactions::TransactionManager::SubOpType::ADD,
                      table, &key_str);
  } else {
    this->add_substep(path, LHTransactions::TransactionManager::SubOpType::ADD,
                      table, nullptr);
  }
}

void LHTransactions::TransactionManagerDeltaLake::log_delete_substep(
    TableStats& table, std::string& path, uint32_t prev) {
  if (prev != UINT32_MAX) {
    std::string key_str = std::to_string(prev);
    this->add_substep(path,
                      LHTransactions::TransactionManager::SubOpType::DELETE,
                      table, &key_str);
  } else {
    this->add_substep(path,
                      LHTransactions::TransactionManager::SubOpType::DELETE,
                      table, nullptr);
  }
}

// unused: interactive add, only called by begin_transaction()
// bool LHTransactions::TransactionManagerDeltaLake::add_file() {
//   TableStats* table = nullptr;
//
//   while (!table) {
//     table = this->select_table();
//   }
//
//   auto arrowTable = LHHelpers::arrow_table_builder();
//
//   auto encoded = LHHelpers::encodeAsParquet(arrowTable);
//   std::string path = this->new_data_file_path(*table);
//
//   if (!this->connector_ptr->write(path, encoded->first, encoded->second)) {
//     return false;
//   }
//
//   auto stats = std::make_unique<FileStats>();
//   stats->path = path;
//   stats->size = encoded->second;
//   stats->stats = LHHelpers::computeStatistics(*arrowTable);
//
//   std::jthread sub_thread([this, &path, table] {
//     this->add_substep(path, LHTransactions::TransactionManager::SubOpType::ADD,
//                       *table, nullptr);
//   });
//
//   this->register_created_file(*table, std::move(stats));
//
//   sub_thread.join();
//   return true;
// }

bool LHTransactions::TransactionManagerDeltaLake::add_file(uint32_t table_id,
                                                           std::string& path,
                                                           bool overwrite,
                                                           uint32_t prev) {
  TableStats* table = this->get_table(table_id);
  if (!table) {
    return false;
  }

  std::jthread sub_thread(
      [this, &path, table, prev] { this->log_add_substep(*table, path, prev); });

  if (this->explicit_log || prev == UINT32_MAX) {
    // (re-)adds an existing file; Delta requires its size
    auto stats = std::make_unique<FileStats>();
    stats->path = path;
    stats->size = table->delta_log->get_size(path, true);
    if (stats->size < 0) {
      auto objects = this->connector_ptr->list_objects(path);
      if (objects) {
        for (auto& object : *objects) {
          if (object.key == path) {
            stats->size = object.size;
          }
        }
      }
    }
    if (stats->size < 0) {
      std::cerr << "unable to determine the size of " << path << std::endl;
      return false;
    }

    this->register_created_file(*table, std::move(stats), overwrite);
  }
  sub_thread.join();
  return true;
}

bool LHTransactions::TransactionManagerDeltaLake::add_file(
    uint32_t table_id, std::shared_ptr<arrow::Table> arrowTable,
    const std::string& key, uint32_t prev) {
  TableStats* table = this->get_table(table_id);
  if (!table || !arrowTable) {
    return false;
  }

  // data files have the table's columns and types
  arrowTable = this->conform_for_write(*table, arrowTable);
  if (!arrowTable) {
    return false;
  }

  auto encoded = LHHelpers::encodeAsParquet(arrowTable);
  std::string path = this->new_data_file_path(*table);

  std::jthread sub_thread(
      [this, &path, table, prev] { this->log_add_substep(*table, path, prev); });

  if (this->explicit_log || prev == UINT32_MAX) {
    if (!this->connector_ptr->write(path, encoded->first, encoded->second)) {
      return false;
    }

    auto stats = std::make_unique<FileStats>();
    stats->path = path;
    stats->size = encoded->second;
    stats->stats = LHHelpers::computeStatistics(*arrowTable);
    stats->update_id = prev;

    this->register_created_file(*table, std::move(stats));
  }
  sub_thread.join();
  return true;
}

bool LHTransactions::TransactionManagerDeltaLake::add_file(
    uint32_t table_id, std::string& content, const std::string& key,
    uint32_t prev) {
  TableStats* table = this->get_table(table_id);
  if (!table) {
    return false;
  }
  // the encoded content is not checked against the schema, but files of
  // tables with partition columns or column mapping are never written
  auto schema = this->table_schema(*table);
  if (schema &&
      (!schema->partition_columns.empty() || schema->column_mapping)) {
    std::cerr << "LakeVilla does not write data files of tables with "
                 "partition columns or column mapping: "
              << table->base_path << std::endl;
    return false;
  }

  std::string path = this->new_data_file_path(*table);

  std::jthread sub_thread(
      [this, &path, table, prev] { this->log_add_substep(*table, path, prev); });

  if (this->explicit_log || prev == UINT32_MAX) {
    if (!this->connector_ptr->write(path, content.c_str(), content.size())) {
      return false;
    }

    // the content is already encoded: only the row count is known cheaply
    auto stats = std::make_unique<FileStats>();
    stats->path = path;
    stats->size = content.size();
    stats->stats.num_records =
        LHHelpers::parquetNumRows(content.c_str(), content.size());
    stats->update_id = prev;

    this->register_created_file(*table, std::move(stats));
  }
  sub_thread.join();
  return true;
}

// unused: interactive remove, only called by begin_transaction()
// bool LHTransactions::TransactionManagerDeltaLake::remove_file() {
//   TableStats* table = nullptr;
//
//   while (!table) {
//     table = this->select_table();
//   }
//
//   auto file_list = table->delta_log->get_list();
//
//   std::string in = "";
//   std::cin >> in;
//
//   uint32_t file_pos = std::stoi(in);
//   if (file_pos >= file_list.size()) {
//     return false;
//   }
//
//   std::jthread sub_thread([this, &file_list, file_pos, table] {
//     this->add_substep(file_list[file_pos],
//                       LHTransactions::TransactionManager::SubOpType::DELETE,
//                       *table, nullptr);
//   });
//
//   this->register_deleted_file(*table, file_list[file_pos]);
//
//   sub_thread.join();
//   return true;
// }

bool LHTransactions::TransactionManagerDeltaLake::remove_file(uint32_t table_id,
                                                              std::string& path,
                                                              uint32_t prev) {
  TableStats* table = this->get_table(table_id);
  if (!table || path.empty()) {
    return false;
  }

  std::jthread sub_thread([this, &path, table, prev] {
    this->log_delete_substep(*table, path, prev);
  });

  if (this->explicit_log) {
    this->register_deleted_file(*table, path);
  }
  sub_thread.join();
  return true;
}

bool LHTransactions::TransactionManagerDeltaLake::remove_file(
    uint32_t table_id, const std::string& key, std::string* path_ptr,
    uint32_t prev) {
  // looking the key up reads the table
  TableStats* table = this->get_table_for_read(table_id);
  if (!table) {
    return false;
  }

  // all files with rows of the key, except the new rows of the same update
  auto files = this->find_key_files(*table, key, false, prev);
  if (path_ptr) {
    *path_ptr = files && !files->empty() ? files->front().path : "";
  }
  if (!files || files->empty()) {
    return false;
  }

  if (!this->explicit_log) {
    // logged implicitly and executed at commit: the redo searches the key
    // again in the newest snapshot (search step) and removes its rows
    this->add_substep(files->front().path,
                      LHTransactions::TransactionManager::SubOpType::READ,
                      *table, &key);
    this->log_delete_substep(*table, files->front().path, prev);
    return true;
  }

  // the rows of the key are removed; other rows of their files stay
  for (auto& file : *files) {
    if (!this->remove_key_rows(*table, file, prev, true)) {
      return false;
    }
  }
  return true;
}

bool LHTransactions::TransactionManagerDeltaLake::wait_for_marker(
    std::string& path) {
  auto table_path = path.substr(0, path.find("/_delta_log/") + 1);
  uint32_t table_id = 0;
  for (uint32_t i = 0; i < this->tables.size(); i++) {
    if (tables[i]->base_path.compare(table_path) == 0) {
      table_id = i;
      break;
    }
  }

  // bounded in case a takeover keeps failing (e.g. storage errors)
  const int64_t start = StorageConnector::local_time_ms();
  const int64_t give_up_after = int64_t(this->options.marker_lease_ms) +
                                this->options.marker_grace_ms + 60000;

  while (true) {
    StorageConnector::ObjectVersion version;
    auto content = this->connector_ptr->read_versioned(path, version);
    if (!content->data) {
      // nothing to wait for
      return true;
    }

    bool running_marker = false;
    std::optional<int64_t> marker_deadline;
    std::vector<std::unique_ptr<rapidjson::Document>> entries;

    std::stringstream lines(std::string(content->data, content->length));
    for (std::string line; std::getline(lines, line);) {
      if (line.size() <= 1) {
        continue;
      }
      auto parsed = LHHelpers::readJSON_rapid(line.c_str());
      if (parsed->HasParseError() || !parsed->IsObject()) {
        std::cerr << "skipping invalid log entry in " << path << std::endl;
        continue;
      }
      if (parsed->HasMember("marker") && (*parsed)["marker"].IsObject()) {
        running_marker = true;
        auto& marker = (*parsed)["marker"];
        if (marker.HasMember("deadline") && marker["deadline"].IsInt64()) {
          marker_deadline = marker["deadline"].GetInt64();
        }
        break;
      }
      entries.push_back(std::move(parsed));
    }

    if (!running_marker) {
      // a commit (or a released version): apply it to the snapshot
      for (auto& parsed : entries) {
        if (parsed->HasMember("add") || parsed->HasMember("remove")) {
          // a concurrent transaction changed the table's data
          tables[table_id]->concurrent_change = true;
        }
        this->parseEntry(*parsed, tables[table_id]->delta_log.get(),
                         parse_log_version(path).value_or(0));
        if (parsed->HasMember("remove") && (*parsed)["remove"].IsObject() &&
            (*parsed)["remove"].HasMember("path") &&
            (*parsed)["remove"]["path"].IsString()) {
          tables[table_id]->concurrent_removes.insert(
              (*parsed)["remove"]["path"].GetString());
        }
      }
      if (this->levels[2]) {
        auto log_number = parse_log_version(path);
        auto entry = this->local_log.find(table_path);
        if (log_number && entry != this->local_log.end()) {
          entry->second = *log_number;
        }
      }
      return true;
    }

    // a running marker: wait until its lease (plus the grace period) ended;
    // markers without a deadline (older clients) are leased from their
    // modification time
    int64_t deadline =
        marker_deadline
            ? *marker_deadline
            : (version.last_modified_ms >= 0 ? version.last_modified_ms
                                             : start) +
                  int64_t(this->options.marker_lease_ms);
    auto now = this->connector_ptr->store_time();
    int64_t takeover_at = deadline + this->options.marker_grace_ms;

    if (now.earliest() > takeover_at && !version.etag.empty()) {
      // the holder may only release the version now: take it over with an
      // empty commit, conditional on the marker's ETag
      auto takeover = Marker::empty_commit("MarkerTakeover");
      auto status = this->connector_ptr->write_if_match(
          path, takeover.c_str(), takeover.size(), version.etag);
      if (status == StorageConnector::WriteStatus::Ok) {
        std::cerr << "took over the expired marker " << path << std::endl;
        return true;
      }
      if (status == StorageConnector::WriteStatus::PreconditionFailed) {
        // committed, released or taken over in the meantime: read again
        continue;
      }
    }

    if (StorageConnector::local_time_ms() - start > give_up_after) {
      return false;
    }
    int64_t remaining = std::max<int64_t>(1, takeover_at - now.earliest());
    std::this_thread::sleep_for(std::chrono::milliseconds(
        std::min<int64_t>(WAIT_MILLISECONDS + 20, remaining)));
  }
}

bool LHTransactions::TransactionManagerDeltaLake::commit(double& data_read,
                                                         double& data_write,
                                                         bool single_threaded) {
  data_read += 1.0 * this->connector_ptr->getReadsBytes() / 1e6;
  data_write += 1.0 * this->connector_ptr->getWritesBytes() / 1e6;
  return this->commit(false, single_threaded);
}

bool LHTransactions::TransactionManagerDeltaLake::commit(bool read_only,
                                                         bool single_threaded) {
  // if the hint "read_only" is set, we can just skip all marker checks and
  // redos and treat it internally as an abort
  if (read_only) {
    return abort();
  }

  bool valid = false;
  // marker shifts and redo rounds of this commit (at most max_redos)
  uint32_t rounds = 0;
  bool redo_done = false;
  // failed version claims without markers (global-log repair)
  uint32_t claim_failures = 0;
  for (auto& table : this->tables) {
    table->committed = false;
  }

  while (!valid) {
    if (this->levels[0] || this->levels[1]) {
      while (true) {
        bool exceeded = false;
        for (auto& marker : this->prev_markers) {
          if (this->connector_ptr->check(marker)) {
            if (!this->wait_for_marker(marker)) {
              exceeded = true;
              std::cerr << "exeeded for " << marker << std::endl;
              break;
            }
          }
        }
        if (!exceeded) {
          break;
        } else {
          std::cerr << "exceeded" << std::endl;
          sleep(this->transaction_id % 5);
          if (!this->can_redo() && this->has_deletes()) {
            // the new snapshot may not contain the files we remove
            this->abort(false);
            this->clear_subdirectory();
            return false;
          }
          this->reopen_tables();
        }
      }
    }

    // a version whose lease ends before the commit is written must be given
    // up: the transaction moves to a later version and redoes (next round)
    bool shifted = false;
    for (auto& table : this->tables) {
      if (!table->committed &&
          (!table->created_files.empty() || !table->deleted_files.empty()) &&
          this->lease_expiring(*table)) {
        if (rounds >= this->options.max_redos) {
          std::cerr << "transaction " << this->transaction_id
                    << " aborts: lease expired " << rounds << " times"
                    << std::endl;
          this->abort(false);
          this->clear_subdirectory();
          return false;
        }
        this->shift_marker(*table);
        shifted = true;
      }
    }
    if (shifted) {
      rounds++;
      continue;
    }

    // validation: serializable - no table the transaction read changed
    // concurrently; snapshot - no concurrent removal of a removed file
    this->collect_unseen_versions();
    bool conflict =
        this->options.isolation == Isolation::Serializable
            ? (this->has_read_conflict() || this->has_write_conflict())
            : this->has_write_conflict();

    if (this->can_redo()) {
      // implicitly logged operations are redone against the newest state;
      // a conflict after a redo needs a fresh snapshot and another redo
      if (redo_done && conflict) {
        if (rounds >= this->options.max_redos) {
          std::cerr << "transaction " << this->transaction_id
                    << " aborts: still conflicting after " << rounds
                    << " rounds" << std::endl;
          this->abort(false);
          break;
        }
        rounds++;
        if (!this->rebuild_state()) {
          this->abort(false);
          break;
        }
        redo_done = false;
      }
      if (!redo_done) {
        this->redo(1);
        redo_done = true;
        this->clear_concurrency();
      }
    } else if (conflict) {
      std::cerr << (this->options.isolation == Isolation::Serializable
                        ? "read-write"
                        : "write-write")
                << " conflict: transaction " << this->transaction_id
                << " aborts" << std::endl;
      this->abort(false);
      break;
    }

    std::unordered_map<std::string, uint32_t> version_read_map;
    std::unordered_map<std::string, uint32_t> version_write_map;
    std::string stage_content = "";

    auto validation =
        this->start_validation(version_read_map, version_write_map,
                               stage_content);

    if (validation == Validation::Valid) {
      bool written_any = false;
      bool failed = false;
      bool lost = false;
      TableStats* taken = nullptr;
      for (auto& table : this->tables) {
        if (table->committed ||
            (table->created_files.empty() && table->deleted_files.empty())) {
          continue;
        }
        auto result =
            this->write_commit(*table, this->build_commit_log(*table));
        if (result == CommitWrite::Lost) {
          // the lease was taken over: move to a later version (next round)
          lost = true;
          if (rounds < this->options.max_redos) {
            this->shift_marker(*table);
          }
          break;
        }
        if (result == CommitWrite::Taken && this->can_repair_global_log()) {
          taken = table.get();
          break;
        }
        if (result != CommitWrite::Ok) {
          failed = true;
          break;
        }
        table->committed = true;
        written_any = true;
      }

      if (taken) {
        // the claim happens under the global-log lock: the version was
        // committed by another engine or by a client that did not update
        // the global log (crash, broken lock); after a few attempts the
        // global log is repaired and the transaction moves behind the version
        claim_failures++;
        bool repaired =
            claim_failures >= this->options.global_log_repair_after &&
            this->repair_global_log(*taken);
        this->release_global_lock();
        if (!repaired) {
          if (claim_failures >= this->options.global_log_repair_after) {
            std::cerr << "transaction " << this->transaction_id
                      << " aborts: unable to repair the global log for "
                      << taken->base_path << std::endl;
            this->abort(false);
            break;
          }
          std::this_thread::sleep_for(
              std::chrono::milliseconds(WAIT_MILLISECONDS * claim_failures));
        }
        continue;
      }

      if (lost) {
        this->release_global_lock();
        if (rounds >= this->options.max_redos) {
          std::cerr << "transaction " << this->transaction_id
                    << " aborts: marker taken over " << rounds << " times"
                    << std::endl;
          this->abort(false);
          break;
        }
        rounds++;
        continue;
      }

      if (failed) {
        if (written_any) {
          std::cerr << "commit of transaction " << this->transaction_id
                    << " was only partially written" << std::endl;
        }
        this->release_global_lock();
        this->abort(false);
        break;
      }

      this->create_global_snapshot(version_read_map, version_write_map,
                                   stage_content);
      valid = true;

      // versions reserved for tables the transaction did not change are
      // released now (they blocked others until the manager was destroyed)
      for (auto& table : this->tables) {
        if (table->marker && table->marker->is_valid()) {
          table->marker->release();
        }
      }
    } else {
      if (validation == Validation::Invalid) {
        this->release_global_lock();
        // with markers, a global log ahead of the reserved version means the
        // lease was taken over: move to a later version instead of aborting
        bool moved = false;
        if ((this->levels[0] || this->levels[1]) &&
            rounds < this->options.max_redos) {
          for (auto& table : this->tables) {
            if (!table->committed &&
                (!table->created_files.empty() ||
                 !table->deleted_files.empty()) &&
                this->marker_lost(*table)) {
              this->shift_marker(*table);
              moved = true;
            }
          }
        }
        if (moved) {
          rounds++;
          continue;
        }
      }
      if (validation == Validation::Invalid && this->levels[0] &&
          this->levels[2] && this->can_redo()) {
        std::cerr << "invalid for " << this->tables[0]->marker->marker_path
                  << std::endl;
        this->abort(false);
        if (!this->reopen_tables()) {
          break;
        }
      } else {
        this->abort(false);
        break;
      }
    }
  }

  this->clear_subdirectory();

  // like Spark: after the commit (markers and global-log lock released)
  if (valid && this->options.checkpoints) {
    for (auto& table : this->tables) {
      auto version = table->committed && table->marker
                         ? parse_log_version(table->marker->marker_path)
                         : std::nullopt;
      if (version) {
        this->checkpoint_after_commit(*table, *version);
      }
    }
  }
  return valid;
}

bool LHTransactions::TransactionManagerDeltaLake::can_redo() {
  // operations are logged implicitly (by key) and sublogs exist (level 0)
  return !this->explicit_log && this->levels[0];
}

bool LHTransactions::TransactionManagerDeltaLake::has_deletes() {
  for (auto& table : this->tables) {
    for (auto& file : table->deleted_files) {
      if (file.second->valid) {
        return true;
      }
    }
  }
  return false;
}

bool LHTransactions::TransactionManagerDeltaLake::has_read_conflict() {
  for (auto& table : this->tables) {
    if (table->was_read && table->concurrent_change) {
      return true;
    }
  }
  return false;
}

void LHTransactions::TransactionManagerDeltaLake::clear_concurrency() {
  for (auto& table : this->tables) {
    table->concurrent_change = false;
    table->concurrent_removes.clear();
  }
}

void LHTransactions::TransactionManagerDeltaLake::collect_unseen_versions() {
  // with markers, concurrent versions are waited for at commit; with the
  // global log, read-only tables are validated against it; without either,
  // newer versions of tables that are only read are checked here (written
  // tables claim the version after their read version)
  if (this->levels[0] || this->levels[1] || this->levels[2] ||
      this->options.isolation != Isolation::Serializable) {
    return;
  }
  for (auto& table : this->tables) {
    if (!table->was_read || !table->created_files.empty() ||
        !table->deleted_files.empty()) {
      continue;
    }
    for (uint32_t v = table->read_version + 1;; v++) {
      std::string path =
          table->base_path + "_delta_log/" + log_file_name(v, ".json");
      if (!this->connector_ptr->check(path)) {
        break;
      }
      this->wait_for_marker(path);
      table->read_version = v;
    }
  }
}

bool LHTransactions::TransactionManagerDeltaLake::rebuild_state() {
  // a fresh snapshot of all versions below the reserved ones; the local
  // changes are discarded and recreated by the redo
  for (auto& table : this->tables) {
    table->delta_log =
        std::make_shared<LHExecutor::LHDL::DeltaLogManager>(table->base_path);
    table->created_files.clear();
    table->deleted_files.clear();
    table->committed = false;

    auto checkpoint =
        this->find_checkpoint(table->base_path, table->marker_version - 1);
    if (!this->parse_checkpoint(table->base_path, *table->delta_log,
                                checkpoint)) {
      return false;
    }
    bool dependencies = false;
    uint32_t first = checkpoint.version ? *checkpoint.version + 1 : 0;
    if (!this->parse_logentries2(first, table->marker_version, table.get(),
                                 UINT32_MAX, dependencies)) {
      return false;
    }
    table->read_version = table->marker_version - 1;
  }
  this->clear_concurrency();
  this->found = {};
  return true;
}

namespace {
// "<table path> <version>" of the global log
bool parse_global_line(const std::string& line, std::string& path,
                       uint32_t& version);
}  // namespace

LHTransactions::TransactionManagerDeltaLake::LogVersion
LHTransactions::TransactionManagerDeltaLake::classify_version(
    const std::string& path) {
  auto content = this->connector_ptr->read2(path);
  if (!content->data) {
    return LogVersion::Missing;
  }
  bool changes = false;
  bool lakevilla = false;
  std::stringstream lines(std::string(content->data, content->length));
  for (std::string line; std::getline(lines, line);) {
    if (line.size() <= 1) {
      continue;
    }
    auto parsed = LHHelpers::readJSON_rapid(line.c_str());
    if (parsed->HasParseError() || !parsed->IsObject()) {
      continue;
    }
    if (parsed->HasMember("marker")) {
      return LogVersion::Marker;
    }
    if (parsed->HasMember("add") || parsed->HasMember("remove")) {
      changes = true;
    }
    if (parsed->HasMember("commitInfo") && (*parsed)["commitInfo"].IsObject()) {
      auto& info = (*parsed)["commitInfo"];
      lakevilla = info.HasMember("engineInfo") &&
                  info["engineInfo"].IsString() &&
                  std::string(info["engineInfo"].GetString())
                          .rfind("LHTransactions", 0) == 0;
    }
  }
  if (!changes) {
    return LogVersion::Empty;
  }
  return lakevilla ? LogVersion::LakeVillaCommit : LogVersion::ForeignCommit;
}

bool LHTransactions::TransactionManagerDeltaLake::can_repair_global_log() {
  return this->options.global_log_repair && this->levels[2] &&
         !this->levels[0] && !this->levels[1];
}

bool LHTransactions::TransactionManagerDeltaLake::repair_global_log(
    LHTransactions::TableStats& table) {
  uint32_t first = table.marker_version;
  uint32_t highest = first - 1;
  for (uint32_t v = first;; v++) {
    auto kind = this->classify_version(table.base_path + "_delta_log/" +
                                       log_file_name(v, ".json"));
    if (kind == LogVersion::Missing) {
      break;
    }
    if (kind == LogVersion::Marker) {
      // the marker's transaction updates the global log itself
      std::cerr << "global-log repair of " << table.base_path
                << " postponed: running marker at version " << v << std::endl;
      return false;
    }
    highest = v;
  }
  if (highest < first) {
    return false;
  }

  // rewrite the global log with the table's entry advanced
  bool written = this->update_global_entry(
      table.base_path, [highest](std::optional<uint32_t> current) {
        return std::max(current.value_or(0), highest);
      });
  if (!written) {
    return false;
  }
  std::cerr << "repaired the global log: " << table.base_path << " "
            << first - 1 << " -> " << highest << std::endl;

  // the versions join the snapshot (and are checked for conflicts); the
  // transaction claims the version after them
  for (uint32_t v = first; v <= highest; v++) {
    std::string path =
        table.base_path + "_delta_log/" + log_file_name(v, ".json");
    this->wait_for_marker(path);
  }
  table.read_version = highest;
  return this->reserve_version(table, highest);
}

bool LHTransactions::TransactionManagerDeltaLake::update_global_entry(
    const std::string& table_path,
    const std::function<std::optional<uint32_t>(std::optional<uint32_t>)>&
        update) {
  std::vector<std::pair<std::string, uint32_t>> entries;
  if (this->connector_ptr->check(this->global_log_path)) {
    auto current = this->read_retry(this->global_log_path);
    if (!current->data) {
      return false;
    }
    std::stringstream stream(std::string(current->data, current->length));
    for (std::string line; std::getline(stream, line);) {
      std::string path;
      uint32_t version = 0;
      if (parse_global_line(line, path, version)) {
        entries.emplace_back(path, version);
      }
    }
  }

  std::optional<uint32_t> current;
  for (auto& entry : entries) {
    if (entry.first == table_path) {
      current = entry.second;
    }
  }
  auto next = update(current);
  if (next == current) {
    return true;
  }

  std::stringstream content;
  bool found_entry = false;
  for (auto& entry : entries) {
    if (entry.first != table_path) {
      content << entry.first << " " << entry.second << "\n";
    } else if (next && !found_entry) {
      content << entry.first << " " << *next << "\n";
      found_entry = true;
    }
  }
  if (next && !found_entry) {
    content << table_path << " " << *next << "\n";
  }
  auto content_str = content.str();
  for (uint32_t attempt = 0; attempt < this->storage_retries; attempt++) {
    if (this->connector_ptr->write(this->global_log_path, content_str.c_str(),
                                   content_str.size())) {
      return true;
    }
  }
  return false;
}

uint32_t LHTransactions::TransactionManagerDeltaLake::skip_foreign_versions(
    const std::string& base_path, uint32_t version) {
  while (true) {
    std::string path =
        base_path + "_delta_log/" + log_file_name(version + 1, ".json");
    // usually a single HEAD request
    if (!this->connector_ptr->check(path)) {
      return version;
    }
    auto kind = this->classify_version(path);
    if (kind != LogVersion::ForeignCommit && kind != LogVersion::Empty) {
      // LakeVilla commits become visible through the global log
      return version;
    }
    version++;
  }
}

bool LHTransactions::TransactionManagerDeltaLake::has_write_conflict() {
  for (auto& table : this->tables) {
    if (table->concurrent_removes.empty()) {
      continue;
    }
    for (auto& file : table->deleted_files) {
      if (!file.second->valid) {
        continue;
      }
      auto& path = file.first;
      auto relative = path.compare(0, table->base_path.size(),
                                   table->base_path) == 0
                          ? path.substr(table->base_path.size())
                          : path;
      if (table->concurrent_removes.count(relative) != 0) {
        return true;
      }
    }
  }
  return false;
}

// unused: not called anywhere
// bool LHTransactions::TransactionManagerDeltaLake::commit(
//     std::vector<double>& timestamps) {
//   std::chrono::time_point<std::chrono::high_resolution_clock> start =
//       std::chrono::high_resolution_clock::now();
//   bool success = true;
//   for (auto& table : this->tables) {
//     if (table->created_files.size() > 0 || table->deleted_files.size() > 0) {
//       if (this->write_commit(*table, this->build_commit_log(*table)) !=
//           CommitWrite::Ok) {
//         success = false;
//       }
//     }
//
//     auto end = std::chrono::high_resolution_clock::now();
//     std::chrono::duration<double, std::milli> commit_duration = end - start;
//     timestamps.push_back(commit_duration.count());
//   }
//
//   return success;
// }

std::string LHTransactions::TransactionManagerDeltaLake::build_commit_log(
    TableStats& table) {
  std::string log_header = "";
  this->createHeaderCommit(log_header, table);

  std::stringstream full_log;
  full_log << log_header;

  for (auto& file : table.created_files) {
    if (file.second->valid && !file.first.empty()) {
      std::string entry = "";
      this->createOperationAddEntry(*(file.second), entry, table.base_path);
      full_log << std::endl << entry;
    }
  }

  for (auto& file : table.deleted_files) {
    if (file.second->valid && !file.first.empty()) {
      std::string entry = "";
      this->createOperationDeleteEntry(*(file.second), entry, table.base_path);
      full_log << std::endl << entry;
    }
  }

  return full_log.str();
}

LHTransactions::TransactionManagerDeltaLake::CommitWrite
LHTransactions::TransactionManagerDeltaLake::write_commit(
    TableStats& table, const std::string& log) {
  auto& path = table.marker->marker_path;
  bool uncertain = false;
  bool with_marker = this->levels[0] || this->levels[1];

  // an earlier attempt may have succeeded although its response was lost
  auto written_by_us = [&]() {
    auto content = this->connector_ptr->read2(path);
    return content->data && std::string(content->data, content->length) == log;
  };

  for (uint32_t attempt = 0; attempt < this->storage_retries; attempt++) {
    StorageConnector::WriteStatus status;
    if (with_marker && table.marker->etag.empty()) {
      // marker of an unknown ETag: plain overwrite
      status = this->connector_ptr->write(path, log.c_str(), log.size())
                   ? StorageConnector::WriteStatus::Ok
                   : StorageConnector::WriteStatus::Error;
    } else if (with_marker) {
      // replace our marker with the commit, unless the lease was taken over
      status = this->connector_ptr->write_if_match(path, log.c_str(),
                                                   log.size(),
                                                   table.marker->etag);
    } else {
      // claim the version; never overwrite a commit of another writer
      status = this->connector_ptr->write_atomic(path, log.c_str(), log.size());
    }

    if (status == StorageConnector::WriteStatus::Ok) {
      table.marker->setInvalid();
      return CommitWrite::Ok;
    }
    if (status == StorageConnector::WriteStatus::PreconditionFailed ||
        status == StorageConnector::WriteStatus::AlreadyExists) {
      if (uncertain && written_by_us()) {
        table.marker->setInvalid();
        return CommitWrite::Ok;
      }
      if (with_marker) {
        // the lease expired and another transaction took the version over
        table.marker->setInvalid();
        std::cerr << "marker " << path << " was taken over" << std::endl;
        return CommitWrite::Lost;
      }
      std::cerr << "version " << path << " was committed concurrently"
                << std::endl;
      return CommitWrite::Taken;
    }
    uncertain = true;
    std::this_thread::sleep_for(
        std::chrono::milliseconds(LV_RETRY_BASE_MS << attempt));
  }

  std::cerr << "unable to write commit " << path << std::endl;
  return CommitWrite::Failed;
}

bool LHTransactions::TransactionManagerDeltaLake::abort(bool clear_subdir) {
  if (this->levels[0] || this->levels[1]) {
    for (auto& table : this->tables) {
      // only release versions that are still reserved (not yet committed);
      // conditional on the marker's ETag (it may have been taken over)
      if (table->marker && table->marker->is_valid() &&
          !table->marker->release()) {
        std::cerr << "unable to release " << table->marker->marker_path
                  << std::endl;
        table->marker->setInvalid();
      }
    }
  }

  if (clear_subdir) {
    this->clear_subdirectory();
  }

  return true;
}

// unused: interactive open, only called by begin_transaction()
// bool LHTransactions::TransactionManagerDeltaLake::open_new_table() {
//   std::string path;
//   std::cin >> path;
//
//   auto table = this->open_table(std::move(path));
//
//   if (table) {
//     this->tables.push_back(std::move(table));
//     return true;
//   }
//   return false;
// }

std::string LHTransactions::log_file_name(uint64_t version,
                                          const std::string& suffix) {
  auto id = std::to_string(version);
  std::string name = id.size() < 20 ? std::string(20 - id.size(), '0') : "";
  return name + id + suffix;
}

std::optional<uint64_t> LHTransactions::parse_log_version(
    const std::string& key) {
  // <table>/_delta_log/<20 digits>.json
  static const std::string log_dir = "_delta_log/";
  auto dir_pos = key.rfind(log_dir);
  if (dir_pos == std::string::npos) {
    return std::nullopt;
  }
  auto name = key.substr(dir_pos + log_dir.size());
  if (name.size() != 25 || name.compare(20, 5, ".json") != 0) {
    return std::nullopt;
  }
  for (size_t i = 0; i < 20; i++) {
    if (!std::isdigit(static_cast<unsigned char>(name[i]))) {
      return std::nullopt;
    }
  }
  return std::stoull(name.substr(0, 20));
}

LHTransactions::CheckpointInfo LHTransactions::newest_checkpoint(
    const std::vector<StorageConnector::ObjectInfo>& objects,
    uint64_t max_version) {
  static const std::string log_dir = "_delta_log/";
  static const std::string single = ".checkpoint.parquet";
  auto digits = [](const std::string& text, size_t pos, size_t count) {
    return std::all_of(text.begin() + pos, text.begin() + pos + count,
                       [](unsigned char c) { return std::isdigit(c); });
  };

  // parts found per version and number of parts (single file: one part)
  std::map<std::pair<uint64_t, uint32_t>, uint32_t> found;
  for (auto& object : objects) {
    auto dir_pos = object.key.rfind(log_dir);
    if (dir_pos == std::string::npos) {
      continue;
    }
    auto name = object.key.substr(dir_pos + log_dir.size());
    if (name.size() < 20 || !digits(name, 0, 20)) {
      continue;
    }
    auto version = std::stoull(name.substr(0, 20));
    auto suffix = name.substr(20);
    if (version > max_version) {
      continue;
    }
    if (suffix == single) {
      found[{version, 1}] = 1;
      continue;
    }
    // .checkpoint.<part, 10 digits>.<parts, 10 digits>.parquet
    if (suffix.size() != 41 || suffix.compare(0, 12, ".checkpoint.") != 0 ||
        !digits(suffix, 12, 10) || suffix[22] != '.' ||
        !digits(suffix, 23, 10) || suffix.compare(33, 8, ".parquet") != 0) {
      continue;
    }
    auto part = std::stoul(suffix.substr(12, 10));
    auto parts = std::stoul(suffix.substr(23, 10));
    if (parts >= 2 && part >= 1 && part <= parts) {
      found[{version, parts}]++;
    }
  }

  CheckpointInfo newest;
  for (auto& [checkpoint, count] : found) {
    if (count == checkpoint.second &&
        (!newest.version || checkpoint.first > *newest.version)) {
      newest.version = checkpoint.first;
      newest.parts = checkpoint.second;
    }
  }
  return newest;
}

namespace {
// a table property (metaData.configuration) of the metaData JSON
std::optional<std::string> table_property(const std::string& metadata,
                                          const char* key) {
  rapidjson::Document doc;
  doc.Parse(metadata.c_str());
  if (doc.HasParseError() || !doc.IsObject() ||
      !doc.HasMember("configuration") || !doc["configuration"].IsObject()) {
    return std::nullopt;
  }
  auto& configuration = doc["configuration"];
  auto it = configuration.FindMember(key);
  if (it == configuration.MemberEnd() || !it->value.IsString()) {
    return std::nullopt;
  }
  return std::string(it->value.GetString());
}

// a Spark interval ("interval 1 weeks", "7 days", "interval 1 day 12 hours")
// in ms
std::optional<int64_t> parse_interval_ms(const std::string& text) {
  static const std::vector<std::pair<std::string, double>> units = {
      {"week", 7 * 24 * 3600e3}, {"day", 24 * 3600e3}, {"hour", 3600e3},
      {"minute", 60e3},          {"second", 1e3},      {"millisecond", 1},
      {"microsecond", 1e-3}};
  std::stringstream tokens(text);
  std::vector<std::string> words;
  for (std::string word; tokens >> word;) {
    std::transform(word.begin(), word.end(), word.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    words.push_back(word);
  }
  size_t i = !words.empty() && words[0] == "interval" ? 1 : 0;
  if (i >= words.size() || (words.size() - i) % 2 != 0) {
    return std::nullopt;
  }
  double total = 0;
  for (; i < words.size(); i += 2) {
    char* end = nullptr;
    auto amount = std::strtod(words[i].c_str(), &end);
    if (end == words[i].c_str() || *end != '\0' || amount < 0) {
      return std::nullopt;
    }
    auto unit = words[i + 1];
    if (unit.size() > 1 && unit.back() == 's') {
      unit.pop_back();
    }
    auto match = std::find_if(units.begin(), units.end(),
                              [&unit](auto& u) { return u.first == unit; });
    if (match == units.end()) {
      return std::nullopt;
    }
    total += amount * match->second;
  }
  return static_cast<int64_t>(total);
}
}  // namespace

uint32_t LHTransactions::checkpoint_interval(const std::string& metadata) {
  auto value = table_property(metadata, "delta.checkpointInterval");
  if (value) {
    char* end = nullptr;
    auto interval = std::strtoul(value->c_str(), &end, 10);
    if (end != value->c_str() && *end == '\0' && interval > 0 &&
        interval <= UINT32_MAX) {
      return static_cast<uint32_t>(interval);
    }
    std::cerr << "ignoring invalid delta.checkpointInterval " << *value
              << std::endl;
  }
  return 10;
}

int64_t LHTransactions::tombstone_retention_ms(const std::string& metadata) {
  const int64_t one_week = int64_t(7) * 24 * 3600 * 1000;
  auto value = table_property(metadata, "delta.deletedFileRetentionDuration");
  if (!value) {
    return one_week;
  }
  auto retention = parse_interval_ms(*value);
  if (!retention) {
    std::cerr << "ignoring invalid delta.deletedFileRetentionDuration "
              << *value << std::endl;
    return one_week;
  }
  return *retention;
}

std::string LHTransactions::checkpoint_blocker(const std::string& protocol) {
  // features that do not change the content of a (classic) checkpoint, or
  // whose actions LakeVilla keeps (domainMetadata)
  static const std::set<std::string> reader_features = {
      "columnMapping", "timestampNtz", "typeWidening", "typeWidening-preview",
      "vacuumProtocolCheck"};
  static const std::set<std::string> writer_features = {
      "appendOnly",          "invariants",       "checkConstraints",
      "changeDataFeed",      "generatedColumns", "columnMapping",
      "identityColumns",     "timestampNtz",     "domainMetadata",
      "icebergCompatV1",     "icebergCompatV2",  "typeWidening",
      "typeWidening-preview", "vacuumProtocolCheck", "allowColumnDefaults"};

  rapidjson::Document doc;
  doc.Parse(protocol.c_str());
  if (doc.HasParseError() || !doc.IsObject()) {
    return "an unreadable protocol";
  }
  auto version = [&doc](const char* name) {
    return doc.HasMember(name) && doc[name].IsInt() ? doc[name].GetInt() : 1;
  };
  if (version("minReaderVersion") > 3 || version("minWriterVersion") > 7) {
    return "an unknown protocol version";
  }
  auto check = [&doc](const char* name, const std::set<std::string>& known,
                      const char* kind) -> std::string {
    if (!doc.HasMember(name) || !doc[name].IsArray()) {
      return "";
    }
    for (auto& feature : doc[name].GetArray()) {
      if (!feature.IsString() || !known.count(feature.GetString())) {
        return std::string(kind) + " feature " +
               (feature.IsString() ? feature.GetString() : "?");
      }
    }
    return "";
  };
  auto reader = check("readerFeatures", reader_features, "the reader");
  return reader.empty() ? check("writerFeatures", writer_features, "the writer")
                        : reader;
}

std::vector<std::pair<uint32_t, uint32_t>> LHTransactions::split_range(
    uint32_t begin, uint32_t end, uint32_t parts) {
  std::vector<std::pair<uint32_t, uint32_t>> ranges;
  if (end <= begin) {
    return ranges;
  }
  uint32_t count = end - begin;
  parts = std::max<uint32_t>(1, std::min(parts, count));
  uint32_t step = count / parts;
  uint32_t remainder = count % parts;

  uint32_t start = begin;
  for (uint32_t i = 0; i < parts; i++) {
    uint32_t stop = start + step + (i < remainder ? 1 : 0);
    ranges.emplace_back(start, stop);
    start = stop;
  }
  return ranges;
}

std::unique_ptr<StorageConnector::Container>
LHTransactions::TransactionManagerDeltaLake::read_retry(
    const std::string& path) {
  for (uint32_t attempt = 0; attempt < this->storage_retries; attempt++) {
    auto content = this->connector_ptr->read2(path);
    if (content->data) {
      return content;
    }
    std::this_thread::sleep_for(
        std::chrono::milliseconds(LV_RETRY_BASE_MS << attempt));
  }
  std::cerr << "unable to read " << path << std::endl;
  return std::make_unique<StorageConnector::Container>();
}

std::optional<std::vector<StorageConnector::ObjectInfo>>
LHTransactions::TransactionManagerDeltaLake::list_retry(
    const std::string& prefix) {
  for (uint32_t attempt = 0; attempt < this->storage_retries; attempt++) {
    auto objects = this->connector_ptr->list_objects(prefix);
    if (objects) {
      return objects;
    }
    std::this_thread::sleep_for(
        std::chrono::milliseconds(LV_RETRY_BASE_MS << attempt));
  }
  std::cerr << "unable to list " << prefix << std::endl;
  return std::nullopt;
}

LHTransactions::CheckpointInfo
LHTransactions::TransactionManagerDeltaLake::read_last_checkpoint(
    const std::string& base_path, std::string* etag) {
  CheckpointInfo info;
  auto path = base_path + "_delta_log/_last_checkpoint";
  std::unique_ptr<StorageConnector::Container> content;
  if (etag) {
    StorageConnector::ObjectVersion version;
    content = this->connector_ptr->read_versioned(path, version);
    *etag = content->data ? version.etag : "";
  } else {
    if (!this->connector_ptr->check(path)) {
      return info;
    }
    content = this->read_retry(path);
  }
  if (!content->data) {
    return info;
  }

  std::string text(content->data, content->length);
  rapidjson::Document doc;
  doc.Parse(text.c_str());
  if (!doc.HasParseError() && doc.IsObject() && doc.HasMember("version") &&
      doc["version"].IsUint64()) {
    // Delta format: {"version":10,"size":...,"parts":...}
    info.version = doc["version"].GetUint64();
    if (doc.HasMember("parts") && doc["parts"].IsUint()) {
      info.parts = doc["parts"].GetUint();
    }
  } else if (!doc.HasParseError() && doc.IsUint64()) {
    info.version = doc.GetUint64();
  } else {
    std::cerr << "ignoring unreadable " << path << std::endl;
  }
  return info;
}

LHTransactions::CheckpointInfo
LHTransactions::TransactionManagerDeltaLake::find_checkpoint(
    const std::string& base_path, uint64_t max_version) {
  auto hint = this->read_last_checkpoint(base_path);
  if (!hint.version || *hint.version <= max_version) {
    return hint;
  }
  // the snapshot is older than the last checkpoint (global log, redo): the
  // versions before it may have been removed by a log cleanup
  auto objects = this->list_retry(base_path + "_delta_log/");
  if (!objects) {
    return {};
  }
  return newest_checkpoint(*objects, max_version);
}

bool LHTransactions::TransactionManagerDeltaLake::parse_checkpoint(
    const std::string& base_path, LHExecutor::LHDL::DeltaLogManager& delta_log,
    const CheckpointInfo& checkpoint) {
  if (!checkpoint.version) {
    return true;
  }

  std::vector<std::string> files;
  if (checkpoint.parts <= 1) {
    files.push_back(base_path + "_delta_log/" +
                    log_file_name(*checkpoint.version, ".checkpoint.parquet"));
  } else {
    for (uint32_t part = 1; part <= checkpoint.parts; part++) {
      std::stringstream suffix;
      suffix << ".checkpoint." << std::setw(10) << std::setfill('0') << part
             << "." << std::setw(10) << std::setfill('0') << checkpoint.parts
             << ".parquet";
      files.push_back(base_path + "_delta_log/" +
                      log_file_name(*checkpoint.version, suffix.str()));
    }
  }

  for (auto& file : files) {
    auto content = this->read_retry(file);
    if (!content->data) {
      return false;
    }

    auto checkpoint_content =
        LHHelpers::readDLCheckpointContent(content->data, content->length);
    if (!checkpoint_content) {
      std::cerr << "unable to parse checkpoint " << file << std::endl;
      return false;
    }

    using LHExecutor::LHDL::DeltaLogEntry;
    using LHExecutor::LHDL::DLOperation;
    int64_t version = *checkpoint.version;
    for (auto& add : checkpoint_content->adds) {
      DeltaLogEntry entry(DLOperation::ADD, add.stats.c_str(), add.size);
      entry.modification_time = add.modification_time;
      entry.partition_values = std::move(add.partition_values);
      entry.tags = std::move(add.tags);
      entry.version = version;
      delta_log.register_action(add.path, std::move(entry));
    }
    for (auto& remove : checkpoint_content->removes) {
      DeltaLogEntry entry(DLOperation::DELETE);
      entry.deletion_timestamp = remove.deletion_timestamp;
      entry.size = remove.size;
      entry.partition_values = std::move(remove.partition_values);
      entry.version = version;
      delta_log.register_action(remove.path, std::move(entry));
    }
    for (auto& txn : checkpoint_content->txns) {
      delta_log.set_transaction(txn.app_id, txn.version, txn.last_updated,
                                *checkpoint.version);
    }
    for (auto& domain : checkpoint_content->domains) {
      delta_log.set_domain_metadata(domain.domain, domain.configuration,
                                    *checkpoint.version);
    }
    if (!checkpoint_content->protocol.empty()) {
      delta_log.set_protocol(checkpoint_content->protocol, *checkpoint.version);
    }
    if (!checkpoint_content->metadata.empty()) {
      delta_log.set_metadata(checkpoint_content->metadata, *checkpoint.version);
    }
    if (!checkpoint_content->unsupported.empty()) {
      delta_log.mark_unsupported(checkpoint_content->unsupported);
    }
  }
  return true;
}

bool LHTransactions::TransactionManagerDeltaLake::parse_logentries2(
    uint32_t start, uint32_t end, LHTransactions::TableStats* stats,
    uint32_t max_version, bool& dependencies) {
  for (uint32_t s = start; s < end; s++) {
    std::string path =
        stats->base_path + "_delta_log/" + log_file_name(s, ".json");

    if (s <= max_version) {
      auto content = this->read_retry(path);
      if (!content->data) {
        return false;
      }
      std::string in(content->data, content->length);

      std::stringstream lines(in);
      for (std::string single_entry; std::getline(lines, single_entry);) {
        if (single_entry.size() <= 1) {
          continue;
        }
        auto parsed = LHHelpers::readJSON_rapid(single_entry.c_str());
        if (parsed->HasParseError() || !parsed->IsObject()) {
          std::cerr << "skipping invalid log entry in " << path << std::endl;
          continue;
        }
        auto dep = this->parseEntry(*parsed, stats->delta_log.get(), s);
        if (dep != UINT32_MAX) {
          std::lock_guard<std::mutex> guard(this->parse_mutex);
          if (this->levels[0] || this->levels[1]) {
            this->prev_markers.push_back(path);
          }

          if (this->levels[1]) {
            if (this->relation->register_dependency(dep)) {
              dependencies = true;
            }
          }
          break;
        }
      }
    } else if (this->levels[0] || this->levels[1]) {
      std::lock_guard<std::mutex> guard(this->parse_mutex);
      this->prev_markers.push_back(path);
    }
  }
  return true;
}

bool LHTransactions::TransactionManagerDeltaLake::reserve_version(
    LHTransactions::TableStats& table, uint32_t& highest_version) {
  while (true) {
    std::string path = table.base_path + "_delta_log/" +
                       log_file_name(highest_version + 1, ".json");

    if (this->levels[0] || this->levels[1]) {
      if (this->connector_ptr->check(path)) {
        highest_version++;
        continue;
      }

      // the lease ends at a deadline in object-store time
      auto now = this->connector_ptr->store_time();
      int64_t deadline = now.now_ms + this->options.marker_lease_ms;

      std::string marker_header = "";
      std::string marker_entry = "";

      this->createHeaderMarker(marker_header);
      this->createMarkerEntry(this->working_dir, marker_entry, deadline);

      std::string complete = marker_header + "\n" + marker_entry;
      std::string etag;
      auto status = connector_ptr->write_atomic(path, complete.c_str(),
                                                complete.size(), &etag);
      // on a transient error, retry the same version
      if (status == StorageConnector::WriteStatus::Error) {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(WAIT_MILLISECONDS));
        continue;
      }

      if (status == StorageConnector::WriteStatus::Ok) {
        table.marker = std::make_unique<LHTransactions::Marker>(
            std::string(path), this->connector_ptr, etag, deadline);
        table.marker_version = highest_version + 1;
        return true;
      }
      highest_version++;
    } else {
      table.marker = std::make_unique<LHTransactions::Marker>(
          std::string(path), this->connector_ptr);
      table.marker->setInvalid();
      table.marker_version = highest_version + 1;
      table.delta_log->register_read_version(highest_version);
      table.delta_log->register_write_version(highest_version + 1);
      return true;
    }
  }
}

bool LHTransactions::TransactionManagerDeltaLake::lease_expiring(
    LHTransactions::TableStats& table) {
  if (!(this->levels[0] || this->levels[1]) || !table.marker ||
      !table.marker->is_valid() || table.marker->deadline_ms == 0) {
    return false;
  }
  // the commit has to be written before the deadline
  int64_t margin = std::min<int64_t>(500, this->options.marker_lease_ms / 4);
  return this->connector_ptr->store_time().latest() + margin >=
         table.marker->deadline_ms;
}

bool LHTransactions::TransactionManagerDeltaLake::marker_lost(
    LHTransactions::TableStats& table) {
  if (!table.marker || !table.marker->is_valid() || table.marker->etag.empty()) {
    return false;
  }
  StorageConnector::ObjectVersion version;
  auto content =
      this->connector_ptr->read_versioned(table.marker->marker_path, version);
  if (!content->data) {
    return false;
  }
  if (version.etag != table.marker->etag) {
    // the version was taken over (or released): it is not ours anymore
    table.marker->setInvalid();
    return true;
  }
  return false;
}

bool LHTransactions::TransactionManagerDeltaLake::shift_marker(
    LHTransactions::TableStats& table) {
  uint32_t old_version = table.marker_version;
  // after the deadline only an empty commit may replace the marker
  table.marker->release();

  uint32_t highest_version = old_version;
  if (!this->reserve_version(table, highest_version)) {
    return false;
  }

  // versions committed or reserved in between are waited for and checked
  // like the markers seen at begin
  std::lock_guard<std::mutex> guard(this->parse_mutex);
  for (uint32_t v = old_version + 1; v < table.marker_version; v++) {
    this->prev_markers.push_back(table.base_path + "_delta_log/" +
                                 log_file_name(v, ".json"));
  }
  std::cerr << "transaction " << this->transaction_id << " moved its marker of "
            << table.base_path << " from version " << old_version << " to "
            << table.marker_version << std::endl;
  return true;
}

bool LHTransactions::TransactionManagerDeltaLake::load_table(
    LHTransactions::TableStats& table, uint32_t num_threads,
    bool& dependencies) {
  uint32_t max_version = UINT32_MAX;
  if (this->levels[2]) {
    auto entry = this->local_log.find(table.base_path);
    if (entry != this->local_log.end()) {
      if (this->options.global_log_check_at_begin) {
        entry->second = this->skip_foreign_versions(table.base_path,
                                                    entry->second);
      }
      max_version = entry->second;
    }
  }

  table.delta_log =
      std::make_shared<LHExecutor::LHDL::DeltaLogManager>(table.base_path);
  table.file_num = 0;
  table.in_bytes = 0;
  table.out_bytes = 0;
  table.prev_marker = false;
  table.opened = true;
  table.concurrent_removes.clear();
  table.concurrent_change = false;

  // highest committed (or reserved) version of the table
  std::optional<uint64_t> highest;
  // the checkpoint the snapshot starts from
  CheckpointInfo checkpoint;

  if (this->levels[2] && max_version != UINT32_MAX) {
    highest = max_version;
    checkpoint = this->find_checkpoint(table.base_path, max_version);
  } else {
    auto file_list = this->list_retry(table.base_path + "_delta_log/");
    if (!file_list) {
      return false;
    }
    for (auto& ref : *file_list) {
      auto version = parse_log_version(ref.key);
      if (version && (!highest || *version > *highest)) {
        highest = version;
      }
    }
    // the listing shows the newest checkpoint (_last_checkpoint may be
    // missing or behind)
    checkpoint = newest_checkpoint(*file_list, UINT64_MAX);
    if (checkpoint.version && (!highest || *checkpoint.version > *highest)) {
      highest = checkpoint.version;
    }
  }

  if (!highest) {
    std::cerr << "no Delta table at " << table.base_path << std::endl;
    return false;
  }
  uint32_t highest_version = static_cast<uint32_t>(*highest);

  // reserve the next version: with markers (level 0/1) by placing a marker,
  // otherwise the version is claimed at commit time
  this->reserve_version(table, highest_version);

  // the checkpoint contains all versions up to (and including) its version
  bool use_checkpoint = checkpoint.version.has_value();
  std::atomic<bool> checkpoint_ok = true;
  std::jthread sub_thread([this, &table, &checkpoint, &checkpoint_ok] {
    checkpoint_ok =
        this->parse_checkpoint(table.base_path, *table.delta_log, checkpoint);
  });

  uint32_t first_log = use_checkpoint ? *checkpoint.version + 1 : 0;

  std::atomic<bool> logs_ok = true;
  std::vector<std::jthread> threads;
  for (auto& range : split_range(first_log, highest_version + 1, num_threads)) {
    threads.emplace_back([this, range, &table, max_version, &dependencies,
                          &logs_ok] {
      if (!this->parse_logentries2(range.first, range.second, &table,
                                   max_version, dependencies)) {
        logs_ok = false;
      }
    });
  }

  table.read_version = highest_version;

  if (this->levels[2] && max_version == UINT32_MAX) {
    this->local_log.insert({table.base_path, highest_version});
  }

  sub_thread.join();
  for (auto& t : threads) {
    t.join();
  }

  return checkpoint_ok && logs_ok;
}

std::unique_ptr<LHTransactions::TableStats>
LHTransactions::TransactionManagerDeltaLake::open_tableV2(
    std::string&& table_path, uint32_t num_threads) {
  auto table =
      std::make_unique<LHTransactions::TableStats>(std::move(table_path));
  table->table_id = this->tables.size();

  bool dependencies = false;
  if (!this->load_table(*table, num_threads, dependencies)) {
    // release a reserved version
    if (table->marker && (this->levels[0] || this->levels[1])) {
      std::string log_header = "";
      this->createHeaderCommit(log_header, *table);
      this->connector_ptr->write(table->marker->marker_path, log_header.c_str(),
                                 log_header.size());
      table->marker->setInvalid();
    }
    return nullptr;
  }

  if (dependencies) {
    bool fixed = false;
    while (!fixed) {
      this->abort(false);
      fixed = this->reopen_tables();
    }
  }
  return table;
}

bool LHTransactions::TransactionManagerDeltaLake::reopen_tables(
    uint32_t num_threads) {
  bool dependencies = false;
  this->relation = std::make_unique<LHTransactions::GlobalRelation>();
  if (this->levels[2]) {
    this->local_log.clear();
    // result was stored in the unused global_log_exists
    this->parse_global_snapshots();
  }

  for (auto& table : this->tables) {
    if (!this->load_table(*table, num_threads, dependencies)) {
      return false;
    }
  }

  if (dependencies) {
    bool fixed = false;
    while (!fixed) {
      fixed = this->reopen_tables(num_threads);
    }
  }

  return true;
}

// unused: stub, only called by open_new_table()
// std::unique_ptr<LHTransactions::TableStats>
// LHTransactions::TransactionManagerDeltaLake::open_table(
//     std::string&& table_path) {
//   return nullptr;
// }

// version 21.7.24
bool LHTransactions::TransactionManagerDeltaLake::begin(std::string base_path) {
  this->tables.clear();
  this->created_files.clear();
  this->deleted_files.clear();
  this->found = {};
  std::stringstream work_place;

  work_place << base_path << "_delta_log/" << std::rand() << "-"
             << this->transaction_id << "/";

  this->working_dir = work_place.str();

  std::unique_ptr<LHTransactions::TableStats> table = nullptr;

  std::atomic<bool> sublog_ok = true;
  std::jthread sub_thread([this, &sublog_ok] {
    if (this->levels[0]) {
      std::string init_log = "init transaction";
      for (uint32_t attempt = 0; attempt < this->storage_retries; attempt++) {
        if (this->connector_ptr->write(this->working_dir + "000.sub",
                                       init_log.c_str(), init_log.size())) {
          return;
        }
        std::this_thread::sleep_for(
            std::chrono::milliseconds(LV_RETRY_BASE_MS << attempt));
      }
      sublog_ok = false;
    }
  });

  if (this->levels[2]) {
    // result was stored in the unused global_log_exists
    this->parse_global_snapshots();
  }

  table = this->open_tableV2(std::move(base_path));
  sub_thread.join();

  if (!table) {
    // remove the initial sublog of the failed attempt
    this->next_step = 1;
    this->clear_subdirectory();
    return false;
  }

  this->tables.push_back(std::move(table));

  if (!sublog_ok) {
    this->abort();
    return false;
  }

  this->next_step = 1;

  return true;
}

bool LHTransactions::TransactionManagerDeltaLake::reroll() {
  uint32_t current_reroll_step = this->next_step - 1;

  std::string redo = "";
  std::string undo = "";

  while (true) {
    if (current_reroll_step <= 0) {
      current_reroll_step = 1;
    }

    std::stringstream sublog_key;
    sublog_key << this->working_dir << "00" << current_reroll_step << ".sub";

    auto result = this->connector_ptr->read2(sublog_key.str());

    auto content = std::string(result->data, result->length);

    if (content.size() == 0) {
      std::cerr << "9" << std::endl;
    }
    redo = content.substr(0, content.find('\n'));
    undo = content.substr(content.find('\n') + 1);

    std::cout << "We are at step " << current_reroll_step << std::endl;
    std::cout << "Redo: " << redo << std::endl;
    std::cout << "Undo: " << undo << std::endl;
    std::cout << "What do you want to do?" << std::endl
              << "[0] Redo" << std::endl
              << "[1] Undo" << std::endl
              << "[2] complete reroll" << std::endl;

    std::string in;
    std::cin >> in;

    std::string execution_string = "";

    switch (std::stoi(in)) {
      case 0: {
        execution_string = redo;
        current_reroll_step++;
      } break;
      case 1: {
        execution_string = undo;
        current_reroll_step--;
      } break;
      default: {
        return true;
      }
    }

    if (!this->execute_reroll_step(execution_string)) {
      return false;
    }
  }

  return true;
}

// unused: not called anywhere
// bool LHTransactions::TransactionManagerDeltaLake::undo(int untilStep) {
//   uint32_t current_undo_step = this->next_step - 1;
//
//   std::string redo = "";
//   std::string undo = "";
//
//   while (current_undo_step > untilStep) {
//     std::stringstream sublog_key;
//     sublog_key << this->working_dir << "00" << current_undo_step << ".sub";
//     auto result = this->connector_ptr->read2(sublog_key.str());
//
//     if (result->data) {
//       auto content = std::string(result->data, result->length);
//
//       undo = content.substr(content.find('\n') + 1);
//
//       std::string execution_string = "";
//
//       if (this->execute_reroll_step(undo)) {
//         current_undo_step--;
//       } else if (!undo.empty()) {
//         current_undo_step++;
//       } else {
//         return false;
//       }
//     } else {
//       current_undo_step--;
//     }
//   }
//
//   return true;
// }

bool LHTransactions::TransactionManagerDeltaLake::redo(int atStep) {
  uint32_t current_redo_step = atStep;
  // this->times_redo_num++;
  std::string redo = "";
  std::string undo = "";

  while (current_redo_step < this->next_step) {
    std::stringstream sublog_key;
    sublog_key << this->working_dir << "00" << current_redo_step << ".sub";
    auto result = this->connector_ptr->read2(sublog_key.str());

    if (result->data) {
      auto content = std::string(result->data, result->length);
      if (content.size() == 0) {
        std::cerr << "10" << std::endl;
      }
      redo = content.substr(0, content.find('\n'));

      std::string execution_string = "";

      if (this->execute_reroll_step(redo)) {
        current_redo_step++;
      } else if (!undo.empty()) {
        current_redo_step--;
      } else {
        return false;
      }
    } else {
      current_redo_step++;
    }
  }

  return true;
}

// unused: not called anywhere
// std::pair<std::string, LHExecutor::LHDL::DLOperation>
// LHTransactions::TransactionManagerDeltaLake::parseEntry(
//     rapidjson::Document& val) {
//   LHExecutor::LHDL::DLOperation op = LHExecutor::LHDL::DLOperation::NONE;
//   std::string content = "";
//
//   bool is_entry_or_invalid = true;
//
//   if (val.HasMember("metaData")) {
//     op = LHExecutor::LHDL::DLOperation::METADATA;
//   }
//
//   if (val.HasMember("add")) {
//     op = LHExecutor::LHDL::DLOperation::ADD;
//     if (val["add"].HasMember("path")) {
//       content = val["add"]["path"].GetString();
//     }
//   }
//
//   if (val.HasMember("remove")) {
//     op = LHExecutor::LHDL::DLOperation::DELETE;
//     content = val["remove"]["path"].GetString();
//   }
//
//   if (val.HasMember("marker")) {
//     if (val["marker"].HasMember("running")) {
//       this->explicit_log = false;
//       op = LHExecutor::LHDL::DLOperation::MARKER;
//     }
//   }
//
//   return {content, op};
// }

namespace {
// applies an action of log version 'version' to the snapshot
void apply_log_action(const rapidjson::Value& val,
                      LHExecutor::LHDL::DeltaLogManager& manager,
                      uint32_t version) {
  auto to_json = [](const rapidjson::Value& value) {
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    value.Accept(writer);
    return std::string(buffer.GetString(), buffer.GetSize());
  };
  auto member = [&val](const char* name) -> const rapidjson::Value* {
    auto it = val.FindMember(name);
    return it != val.MemberEnd() && it->value.IsObject() ? &it->value
                                                         : nullptr;
  };

  if (auto metadata = member("metaData")) {
    manager.set_metadata(to_json(*metadata), version);
  }

  if (auto protocol = member("protocol")) {
    manager.set_protocol(to_json(*protocol), version);
  }

  if (auto txn = member("txn")) {
    auto app_id = txn->FindMember("appId");
    auto txn_version = txn->FindMember("version");
    auto updated = txn->FindMember("lastUpdated");
    if (app_id == txn->MemberEnd() || !app_id->value.IsString() ||
        txn_version == txn->MemberEnd() || !txn_version->value.IsInt64()) {
      manager.mark_unsupported("invalid txn action");
    } else {
      manager.set_transaction(
          app_id->value.GetString(), txn_version->value.GetInt64(),
          updated != txn->MemberEnd() && updated->value.IsInt64()
              ? updated->value.GetInt64()
              : -1,
          version);
    }
  }

  if (auto domain = member("domainMetadata")) {
    auto name = domain->FindMember("domain");
    auto configuration = domain->FindMember("configuration");
    auto removed = domain->FindMember("removed");
    if (name == domain->MemberEnd() || !name->value.IsString()) {
      manager.mark_unsupported("invalid domainMetadata action");
    } else if (removed != domain->MemberEnd() && removed->value.IsBool() &&
               removed->value.GetBool()) {
      manager.set_domain_metadata(name->value.GetString(), std::nullopt,
                                  version);
    } else {
      manager.set_domain_metadata(
          name->value.GetString(),
          configuration != domain->MemberEnd() &&
                  configuration->value.IsString()
              ? configuration->value.GetString()
              : "",
          version);
    }
  }

  auto add = member("add");
  if (add && add->HasMember("path") && (*add)["path"].IsString()) {
    if (add->HasMember("deletionVector") && !(*add)["deletionVector"].IsNull()) {
      manager.mark_unsupported("deletion vectors");
    }
    manager.register_action(
        (*add)["path"].GetString(),
        LHExecutor::LHDL::DeltaLogEntry::from_action(
            LHExecutor::LHDL::DLOperation::ADD, *add, version));
  }

  auto remove = member("remove");
  if (remove && remove->HasMember("path") && (*remove)["path"].IsString()) {
    manager.register_action(
        (*remove)["path"].GetString(),
        LHExecutor::LHDL::DeltaLogEntry::from_action(
            LHExecutor::LHDL::DLOperation::DELETE, *remove, version));
  }
}
}  // namespace

// returns is entry is an active marker
uint32_t LHTransactions::TransactionManagerDeltaLake::parseEntry(
    rapidjson::Document& val, LHExecutor::LHDL::DeltaLogManager* manager,
    uint32_t version) {
  apply_log_action(val, *manager, version);

  if (val.HasMember("marker") && val["marker"].IsObject()) {
    if (val["marker"].HasMember("running")) {
      // a concurrent transaction placed a marker: defer the operations and
      // redo them at commit time; this needs the sublogs of level 0
      if (this->levels[0]) {
        this->explicit_log = false;
      }

      if (val["marker"].HasMember("txnid") && val["marker"]["txnid"].IsUint()) {
        return val["marker"]["txnid"].GetUint();
      }
    }
  }

  return UINT32_MAX;
}

void LHTransactions::TransactionManagerDeltaLake::createHeaderMarker(
    std::string& out_string) {
  rapidjson::Document d;
  d.SetObject();

  rapidjson::Value operation_para;
  rapidjson::Value operation_metrics;

  operation_para.SetObject();
  operation_metrics.SetObject();

  operation_para.AddMember("mode", "Marker", d.GetAllocator());
  operation_para.AddMember("partitionBy", "[]", d.GetAllocator());

  operation_metrics.AddMember("numOutputBytes", "0", d.GetAllocator());
  operation_metrics.AddMember("numOutputRows", "0", d.GetAllocator());
  operation_metrics.AddMember("numFiles", "0", d.GetAllocator());

  rapidjson::Value commit_info;
  commit_info.SetObject();

  commit_info.AddMember("txnId", this->transaction_id, d.GetAllocator());
  commit_info.AddMember("engineInfo", "LHTransactions 0.1 - Delta Lake 3.0",
                        d.GetAllocator());
  commit_info.AddMember("operationMetrics", operation_metrics,
                        d.GetAllocator());
  commit_info.AddMember("isBlindAppend", true, d.GetAllocator());
  commit_info.AddMember("isolationLevel", "Serializable", d.GetAllocator());
  commit_info.AddMember("readVersion", 0, d.GetAllocator());
  commit_info.AddMember("operationParameters", operation_para,
                        d.GetAllocator());
  commit_info.AddMember("operation", "WRITE", d.GetAllocator());
  commit_info.AddMember("timestamp", LHHelpers::now_ms(), d.GetAllocator());

  d.AddMember("commitInfo", commit_info, d.GetAllocator());

  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  d.Accept(writer);

  out_string = buffer.GetString();
}

// unused: not called anywhere
// bool LHTransactions::TransactionManagerDeltaLake::checkMarker(
//     const rapidjson::Document* content) {
//   if (content && content->HasMember("commitInfo")) {
//     auto& tmp = content->FindMember("commitInfo")->value;
//     if (tmp.HasMember("txnId") &&
//         tmp.FindMember("txnId")->value.GetInt() == this->transaction_id) {
//       return true;
//     }
//   }
//
//   return false;
// }

// simplified version for the test
void LHTransactions::TransactionManagerDeltaLake::createMarkerEntry(
    std::string& working_dir, std::string& out_str, int64_t deadline_ms) {
  rapidjson::Document d;
  d.SetObject();
  rapidjson::Value marker;
  marker.SetObject();

  rapidjson::Value val;
  val.SetString(working_dir.c_str(), working_dir.size());
  marker.AddMember("workDir", val, d.GetAllocator());
  marker.AddMember("running", true, d.GetAllocator());

  auto dep = this->relation->get_dependencies();
  rapidjson::Value arr;
  arr.SetArray();
  for (auto& elem : dep) {
    rapidjson::Value e;
    e.SetUint(elem);
    arr.PushBack(e, d.GetAllocator());
  }
  marker.AddMember("dependencies", arr, d.GetAllocator());
  marker.AddMember("txnid", this->transaction_id, d.GetAllocator());
  // end of the lease (object-store time, ms since the epoch)
  marker.AddMember("deadline", deadline_ms, d.GetAllocator());

  d.AddMember("marker", marker, d.GetAllocator());

  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  d.Accept(writer);

  out_str = buffer.GetString();
}

void LHTransactions::TransactionManagerDeltaLake::createHeaderCommit(
    std::string& out_string, LHTransactions::TableStats& stats) {
  rapidjson::Document d;
  d.SetObject();

  rapidjson::Value operation_para;
  rapidjson::Value operation_metrics;

  operation_para.SetObject();
  operation_metrics.SetObject();

  operation_para.AddMember("mode", "MultiQuery", d.GetAllocator());
  operation_para.AddMember("partitionBy", "[]", d.GetAllocator());

  operation_metrics.AddMember("numOutputBytes", stats.out_bytes,
                              d.GetAllocator());
  operation_metrics.AddMember("numOutputRows", stats.in_bytes,
                              d.GetAllocator());
  operation_metrics.AddMember("numFiles", stats.file_num, d.GetAllocator());

  rapidjson::Value commit_info;
  commit_info.SetObject();

  commit_info.AddMember("txnId", this->transaction_id, d.GetAllocator());
  commit_info.AddMember("engineInfo", "LHTransactions 0.1 - Delta Lake 3.0",
                        d.GetAllocator());
  commit_info.AddMember("operationMetrics", operation_metrics,
                        d.GetAllocator());
  commit_info.AddMember("isBlindAppend", false, d.GetAllocator());
  commit_info.AddMember("isolationLevel", "Serializable", d.GetAllocator());
  commit_info.AddMember("readVersion", stats.read_version, d.GetAllocator());
  commit_info.AddMember("operationParameters", operation_para,
                        d.GetAllocator());
  commit_info.AddMember("operation", "WRITE", d.GetAllocator());
  commit_info.AddMember("timestamp", LHHelpers::now_ms(), d.GetAllocator());

  d.AddMember("commitInfo", commit_info, d.GetAllocator());

  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  d.Accept(writer);

  out_string = buffer.GetString();
}

namespace {
// path of a data file relative to the table (as stored in the Delta log)
std::string relative_path(const std::string& path, const std::string& base) {
  if (path.compare(0, base.size(), base) == 0) {
    return path.substr(base.size());
  }
  return path;
}

rapidjson::Value stat_value(const LHHelpers::ColumnStatistics& column,
                            const std::string& value,
                            rapidjson::Document::AllocatorType& allocator) {
  rapidjson::Value result;
  if (column.numeric) {
    if (value.find_first_of(".eE") != std::string::npos) {
      result.SetDouble(std::stod(value));
    } else {
      result.SetInt64(std::stoll(value));
    }
  } else {
    result.SetString(value.c_str(), value.size(), allocator);
  }
  return result;
}
}  // namespace

void LHTransactions::TransactionManagerDeltaLake::createOperationAddEntry(
    FileStats& file, std::string& out_str, const std::string& base_path) {
  rapidjson::Document d;
  d.SetObject();
  auto& allocator = d.GetAllocator();

  rapidjson::Value add;
  add.SetObject();

  auto path_str = relative_path(file.path, base_path);
  rapidjson::Value path;
  path.SetString(path_str.c_str(), path_str.size(), allocator);
  add.AddMember("path", path, allocator);
  rapidjson::Value partition_values;
  partition_values.SetObject();
  add.AddMember("partitionValues", partition_values, allocator);
  add.AddMember("size", file.size < 0 ? 0 : file.size, allocator);
  add.AddMember("modificationTime", LHHelpers::now_ms(), allocator);
  add.AddMember("dataChange", true, allocator);

  // statistics (optional in Delta): only values that are actually known
  auto& stats = file.stats;
  if (stats.num_records >= 0 || !stats.columns.empty()) {
    rapidjson::Document d2;
    d2.SetObject();
    auto& allocator2 = d2.GetAllocator();
    if (stats.num_records >= 0) {
      d2.AddMember("numRecords", stats.num_records, allocator2);
    }
    if (!stats.columns.empty()) {
      rapidjson::Value min_values, max_values, null_count;
      min_values.SetObject();
      max_values.SetObject();
      null_count.SetObject();

      for (auto& column : stats.columns) {
        if (column.has_min_max) {
          rapidjson::Value name1(column.name.c_str(), allocator2);
          rapidjson::Value name2(column.name.c_str(), allocator2);
          min_values.AddMember(name1, stat_value(column, column.min, allocator2),
                               allocator2);
          max_values.AddMember(name2, stat_value(column, column.max, allocator2),
                               allocator2);
        }
        rapidjson::Value name3(column.name.c_str(), allocator2);
        null_count.AddMember(name3, column.null_count, allocator2);
      }
      d2.AddMember("minValues", min_values, allocator2);
      d2.AddMember("maxValues", max_values, allocator2);
      d2.AddMember("nullCount", null_count, allocator2);
    }

    rapidjson::StringBuffer buffer1;
    rapidjson::Writer<rapidjson::StringBuffer> writer1(buffer1);
    d2.Accept(writer1);

    rapidjson::Value stats_str;
    stats_str.SetString(buffer1.GetString(), buffer1.GetSize(), allocator);
    add.AddMember("stats", stats_str, allocator);
  }

  d.AddMember("add", add, allocator);

  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  d.Accept(writer);

  out_str = buffer.GetString();
}

void LHTransactions::TransactionManagerDeltaLake::createOperationDeleteEntry(
    FileStats& file, std::string& out_str, const std::string& base_path) {
  rapidjson::Document d;
  d.SetObject();
  auto& allocator = d.GetAllocator();

  rapidjson::Value remove;
  remove.SetObject();

  auto path_str = relative_path(file.path, base_path);
  rapidjson::Value path;
  path.SetString(path_str.c_str(), path_str.size(), allocator);
  remove.AddMember("path", path, allocator);
  remove.AddMember("deletionTimestamp", LHHelpers::now_ms(), allocator);
  remove.AddMember("dataChange", true, allocator);
  rapidjson::Value partition_values;
  partition_values.SetObject();
  remove.AddMember("partitionValues", partition_values, allocator);
  // extendedFileMetadata: partitionValues and size are present
  if (file.size >= 0) {
    remove.AddMember("extendedFileMetadata", true, allocator);
    remove.AddMember("size", file.size, allocator);
  } else {
    remove.AddMember("extendedFileMetadata", false, allocator);
  }

  d.AddMember("remove", remove, allocator);

  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  d.Accept(writer);

  out_str = buffer.GetString();
}

bool LHTransactions::TransactionManagerDeltaLake::add_substep(
    std::string& file, LHTransactions::TransactionManager::SubOpType type,
    LHTransactions::TableStats& table, const std::string* key) {
  // {
  //   std::lock_guard<std::mutex> guard(this->state_mutex);
  //   ops.push_back({type, file});
  // }

  if (this->levels[0]) {
    if (this->explicit_log) {
      return add_substep_explicit(file, type, table, key);
    } else {
      return add_substep_implicit(file, type, table, key);
    }
  }
  return true;
}

bool LHTransactions::TransactionManagerDeltaLake::add_substep_explicit(
    std::string& file, LHTransactions::TransactionManager::SubOpType type,
    LHTransactions::TableStats& table, const std::string* key) {
  std::stringstream sublog_key;
  std::stringstream sublog_content;
  // operations of one transaction may run concurrently (e.g. the remove and
  // add of an update): reserve the step and build the entry under the lock
  std::unique_lock<std::mutex> guard(this->state_mutex);
  sublog_key << this->working_dir << "00" << this->next_step << ".sub";

  switch (type) {
    case LHTransactions::TransactionManager::SubOpType::READ:
      sublog_content << table.table_id << " r " << file << "\n";
      sublog_content << table.table_id << " r " << file;
      break;
    case LHTransactions::TransactionManager::SubOpType::ADD:
      sublog_content << table.table_id << " a " << file << "\n"
                     << table.table_id << " d " << file;
      break;
    case LHTransactions::TransactionManager::SubOpType::DELETE:
      sublog_content << table.table_id << " d " << file << "\n"
                     << table.table_id << " a " << file;
      break;
    default:
      break;
  }

  std::string sublog_c = sublog_content.str();
  this->next_step++;
  guard.unlock();

  return this->write_sublog(sublog_key.str(), sublog_c);
}

bool LHTransactions::TransactionManagerDeltaLake::add_substep_implicit(
    std::string& file, LHTransactions::TransactionManager::SubOpType type,
    LHTransactions::TableStats& table, const std::string* key) {
  std::stringstream sublog_key;
  std::stringstream sublog_content;
  // operations of one transaction may run concurrently (e.g. the remove and
  // add of an update): reserve the step and build the entry under the lock
  std::unique_lock<std::mutex> guard(this->state_mutex);
  sublog_key << this->working_dir << "00" << this->next_step << ".sub";

  switch (type) {
    case LHTransactions::TransactionManager::SubOpType::READ:

      if (key) {
        sublog_content << table.table_id << " s " << *key << "\n";
        sublog_content << table.table_id << " o " << this->found.second;
      } else {
        sublog_content << table.table_id << " r " << file << "\n";
        sublog_content << table.table_id << " r " << file;
      }
      break;
    case LHTransactions::TransactionManager::SubOpType::ADD:
      if (key) {
        sublog_content << table.table_id << " e found "
                       << this->update_vals.size() - 1 << "\n"
                       << table.table_id << " d found";
        this->found = {*key, file};
      } else {
        sublog_content << table.table_id << " a " << file << "\n"
                       << table.table_id << " d " << file;
      }
      // table.read_version++;
      break;
    case LHTransactions::TransactionManager::SubOpType::DELETE:
      if (key) {
        sublog_content << table.table_id << " d found" << *key << "\n"
                       << table.table_id << " a found "
                       << this->update_ops.size() - 1;
        this->found = {*key, file};
      } else {
        sublog_content << table.table_id << " d " << file << "\n"
                       << table.table_id << " a " << file;
      }
      break;
    default:
      break;
  }

  std::string sublog_c = sublog_content.str();
  this->next_step++;
  guard.unlock();

  return this->write_sublog(sublog_key.str(), sublog_c);
}

bool LHTransactions::TransactionManagerDeltaLake::write_sublog(
    const std::string& key, const std::string& content) {
  for (uint32_t attempt = 0; attempt < this->storage_retries; attempt++) {
    if (this->connector_ptr->write(key, content.c_str(), content.size())) {
      return true;
    }
    std::this_thread::sleep_for(
        std::chrono::milliseconds(LV_RETRY_BASE_MS << attempt));
  }
  // recovery (redo/undo) cannot replay this step
  std::cerr << "unable to write sublog " << key << std::endl;
  return false;
}

void LHTransactions::TransactionManagerDeltaLake::clear_subdirectory() {
  if (this->levels[0]) {
    for (int log_id = 0; log_id < this->next_step; log_id++) {
      std::stringstream sublog_key;
      sublog_key << this->working_dir << "00" << log_id << ".sub";

      this->connector_ptr->delete_file(sublog_key.str());
    }
  }
}

bool LHTransactions::TransactionManagerDeltaLake::execute_reroll_step(
    std::string exec_string) {
  // a sublog step is "<table id> <command> <argument>"
  auto pos = exec_string.find(' ');
  if (pos == std::string::npos || pos + 1 >= exec_string.size()) {
    std::cerr << "invalid sublog step: '" << exec_string << "'" << std::endl;
    return false;
  }

  uint32_t table_idx = 0;
  try {
    table_idx = std::stoul(exec_string.substr(0, pos));
  } catch (const std::exception&) {
    std::cerr << "invalid sublog step: '" << exec_string << "'" << std::endl;
    return false;
  }
  if (table_idx >= this->tables.size()) {
    std::cerr << "sublog step for unknown table " << table_idx << std::endl;
    return false;
  }

  auto& table = this->tables[table_idx];

  std::string command = exec_string.substr(pos + 1);
  std::string argument = command.size() > 2 ? command.substr(2) : "";

  // index of the registered update values in "found <index>"
  auto update_index = [&argument](size_t limit) -> std::optional<size_t> {
    try {
      size_t idx = std::stoul(argument.substr(6));
      if (idx < limit) {
        return idx;
      }
    } catch (const std::exception&) {
    }
    std::cerr << "invalid update reference '" << argument << "'" << std::endl;
    return std::nullopt;
  };

  if (command[0] == 'r') {
    auto result = this->connector_ptr->read2(argument);

    auto decoded = LHHelpers::readParquet(result->data, result->length);

    return true;
  }

  // the rows of the searched key are removed from their files once; the
  // other rows of a file are written to a new file
  auto remove_found_rows = [this, &table]() {
    for (auto& file : this->found_files) {
      if (this->retired_files.insert(file.path).second &&
          !this->remove_key_rows(*table, file, UINT32_MAX, false)) {
        return false;
      }
    }
    return true;
  };

  // writes (updated) rows of the key as a new data file
  auto write_rows = [this, &table](std::shared_ptr<arrow::Table> rows) {
    rows = this->conform_for_write(*table, rows);
    if (!rows) {
      return false;
    }
    auto encoded = LHHelpers::encodeAsParquet(rows);
    std::string path = this->new_data_file_path(*table);
    if (!this->connector_ptr->write(path, encoded->first, encoded->second)) {
      return false;
    }
    auto stats = std::make_unique<FileStats>();
    stats->path = path;
    stats->size = encoded->second;
    stats->stats = LHHelpers::computeStatistics(*rows);
    this->register_created_file(*table, std::move(stats));
    return true;
  };

  if (command[0] == 's') {
    // the files with rows of the key in the newest snapshot
    auto files = this->find_key_files(*table, argument, true);
    if (!files) {
      return false;
    }
    this->found_files = std::move(*files);
    this->retired_files.clear();
    this->found = {argument, this->found_files.empty()
                                 ? std::string()
                                 : this->found_files.front().path};

    return true;
  }

  if (command[0] == 'o') {
    this->found = {"", argument};

    return true;
  }

  if (command[0] == 'a') {
    this->times_redo++;
    if (argument.find("found") != std::string::npos) {
      auto idx = update_index(update_vals.size());
      if (!idx) {
        return false;
      }
      auto& values = update_vals[*idx];
      // the rows of the key with the values replaced
      auto arrowBaseTable = this->key_rows(this->found_files);
      if (!arrowBaseTable) {
        return false;
      }
      for (auto& ref : values) {
        auto col_names = arrowBaseTable->ColumnNames();
        for (int i = 0; i < col_names.size(); i++) {
          if (same_column(col_names[i], ref.first)) {
            auto field =
                std::make_shared<arrow::Field>(col_names[i], arrow::utf8());

            arrow::StringBuilder builder;
            for (int64_t row = 0; row < arrowBaseTable->num_rows(); row++) {
              PARQUET_THROW_NOT_OK(builder.Append(ref.second));
            }
            std::shared_ptr<arrow::Array> arrow_array;
            std::vector<std::shared_ptr<arrow::Array>> chunks;

            PARQUET_THROW_NOT_OK(builder.Finish(&arrow_array));
            chunks.push_back(std::move(arrow_array));

            auto new_table = arrowBaseTable->SetColumn(
                i, field,
                std::make_shared<arrow::ChunkedArray>(std::move(chunks)));

            arrowBaseTable = *new_table;
          }
        }
      }

      if (!write_rows(arrowBaseTable)) {
        return false;
      }

    } else {
      table->delta_log->register_operation(LHExecutor::LHDL::DLOperation::ADD,
                                           argument, true, true);

      if (table->created_files.count(argument) != 0) {
        // REDO step
        table->created_files.find(argument)->second->valid = true;
      }

      if (table->deleted_files.count(argument) != 0) {
        // UNDO step
        table->deleted_files.find(argument)->second->valid = false;
      }
    }

    return true;
  }

  if (command[0] == 'e') {
    this->times_redo++;
    if (argument.find("found") != std::string::npos) {
      auto idx =
          update_index(std::min(update_vals.size(), update_ops.size()));
      if (!idx) {
        return false;
      }
      auto& values = update_vals[*idx];
      auto& op = update_ops[*idx];
      // the operation is applied to the rows of the key
      auto arrowBaseTable = this->key_rows(this->found_files);
      if (!arrowBaseTable) {
        return false;
      }
      arrowBaseTable = arrowBaseTable->CombineChunks().ValueOr(nullptr);
      if (!arrowBaseTable) {
        return false;
      }

      for (auto& ref : values) {
        std::shared_ptr<arrow::Array> arrow_array;

        int index = -1;
        for (int i = 0; i < arrowBaseTable->num_columns(); i++) {
          if (same_column(arrowBaseTable->field(i)->name(), ref.first)) {
            index = i;
            break;
          }
        }
        if (index < 0) {
          std::cerr << "column " << ref.first << " not found; skipping "
                    << "instruction" << std::endl;
          continue;
        }
        auto name = arrowBaseTable->field(index)->name();
        auto col = arrowBaseTable->column(index);
        auto type = arrowBaseTable->field(index)->type();
        if (type->Equals(arrow::utf8())) {
          arrow_array = this->build_string_col(name, col, op, ref.second);
          auto field = std::make_shared<arrow::Field>(name, arrow::utf8());
          std::vector<std::shared_ptr<arrow::Array>> chunks;

          chunks.push_back(std::move(arrow_array));

          auto new_table = arrowBaseTable->SetColumn(
              index, field,
              std::make_shared<arrow::ChunkedArray>(std::move(chunks)));

          arrowBaseTable = *new_table;
        } else if (type->Equals(arrow::int32())) {
          arrow_array =
              this->build_int_col(name, col, op, std::stoi(ref.second));
          auto field =
              std::make_shared<arrow::Field>(name, arrow::int32());
          std::vector<std::shared_ptr<arrow::Array>> chunks;

          chunks.push_back(std::move(arrow_array));

          auto new_table = arrowBaseTable->SetColumn(
              index, field,
              std::make_shared<arrow::ChunkedArray>(std::move(chunks)));
          arrowBaseTable = *new_table;
        } else {
          std::cerr << "data type " << type->ToString()
                    << " not implemented; skipping instruction" << std::endl;
        }
      }

      // the updated rows replace those of the key in the old files
      if (!write_rows(arrowBaseTable) || !remove_found_rows()) {
        return false;
      }
    } else {
      table->delta_log->register_operation(LHExecutor::LHDL::DLOperation::ADD,
                                           argument, true, true);

      if (table->created_files.count(argument) != 0) {
        // REDO step
        table->created_files.find(argument)->second->valid = true;
      }

      if (table->deleted_files.count(argument) != 0) {
        // UNDO step
        table->deleted_files.find(argument)->second->valid = false;
      }
    }

    return true;
  }

  if (command[0] == 'd') {
    this->times_redo++;
    if (argument.find("found") != std::string::npos) {
      // the rows of the key are removed (other rows stay)
      if (!remove_found_rows()) {
        return false;
      }

    } else {
      table->delta_log->register_operation(
          LHExecutor::LHDL::DLOperation::DELETE, argument, true, true);

      if (table->created_files.count(argument) != 0) {
        // UNDO step
        table->created_files.find(argument)->second->valid = false;
      }

      if (table->deleted_files.count(argument) != 0) {
        // REDO step
        table->deleted_files.find(argument)->second->valid = true;
      }
    }

    return true;
  }

  return false;
}

uint32_t LHTransactions::TransactionManagerDeltaLake::register_update_pairs(
    std::vector<std::pair<std::string, std::string>>& values) {
  std::lock_guard<std::mutex> guard(this->state_mutex);
  uint32_t id = this->update_vals.size();
  update_vals.push_back(values);
  return id;
}

uint32_t LHTransactions::TransactionManagerDeltaLake::register_update_pairs(
    std::vector<std::pair<std::string, std::string>>& values,
    LHTransactions::UpdateOperations op,
    std::vector<std::pair<std::string, std::string>>& impl) {
  std::lock_guard<std::mutex> guard(this->state_mutex);
  uint32_t id = this->update_vals.size();

  if (this->explicit_log) {
    update_vals.push_back(values);
  } else {
    update_vals.push_back(impl);
    this->update_ops.push_back(op);
  }

  return id;
}

bool LHTransactions::TransactionManagerDeltaLake::open_new_table(
    const std::string& path) {
  std::unique_ptr<TableStats> table = nullptr;
  uint32_t counter = 0;
  while (!table && counter < 5) {
    table = this->open_tableV2(std::string(path));
    counter++;
  }

  if (table) {
    this->tables.push_back(std::move(table));
    return true;
  }

  this->abort();
  return false;
}

// unused: not called anywhere
// void LHTransactions::TransactionManagerDeltaLake::print_stats() {}

uint32_t LHTransactions::TransactionManagerDeltaLake::get_table_id(
    std::string& key) {
  for (uint32_t i = 0; i < tables.size(); i++) {
    if (tables[i]->base_path.compare(key) == 0) {
      return i;
    }
  }
  return UINT32_MAX;
}

namespace {
// parses a line "<table path> <version>" of the global log
bool parse_global_line(const std::string& line, std::string& path,
                       uint32_t& version) {
  auto pos = line.rfind(' ');
  if (pos == std::string::npos || pos == 0) {
    return false;
  }
  try {
    version = std::stoul(line.substr(pos + 1));
  } catch (const std::exception&) {
    return false;
  }
  path = line.substr(0, pos);
  return true;
}
}  // namespace

bool LHTransactions::TransactionManagerDeltaLake::parse_global_snapshots() {
  if (!this->levels[2]) {
    return true;
  }

  if (!this->connector_ptr->check(this->global_log_path)) {
    return false;
  }

  auto log_content = this->read_retry(this->global_log_path);

  if (log_content->data) {
    std::stringstream stream(
        std::string(log_content->data, log_content->length));

    for (std::string line; std::getline(stream, line);) {
      std::string path;
      uint32_t read_version = 0;
      if (!parse_global_line(line, path, read_version)) {
        if (!line.empty()) {
          std::cerr << "ignoring invalid global log line '" << line << "'"
                    << std::endl;
        }
        continue;
      }
      this->local_log.insert({path, read_version});
    }
  }

  return true;
}

bool LHTransactions::TransactionManagerDeltaLake::acquire_global_lock() {
  thread_local std::mt19937_64 generator{std::random_device{}()};

  std::stringstream token_stream;
  token_stream << this->transaction_id << "-" << std::hex << generator()
               << std::dec << "-" << LHHelpers::now_ms();
  std::string token = token_stream.str();

  auto start = std::chrono::steady_clock::now();
  std::string observed;
  auto observed_since = start;
  uint32_t backoff_ms = 1;

  while (true) {
    auto status = this->connector_ptr->write_atomic(
        this->global_tmp_log_path, token.c_str(), token.size());

    if (status == StorageConnector::WriteStatus::Ok) {
      this->global_lock_token = token;
      return true;
    }

    auto now = std::chrono::steady_clock::now();
    auto current = this->connector_ptr->read2(this->global_tmp_log_path);
    if (current->data) {
      std::string content(current->data, current->length);
      if (content == token) {
        // an earlier PUT succeeded although its response was lost
        this->global_lock_token = token;
        return true;
      }

      if (content != observed) {
        observed = content;
        observed_since = now;
      } else if (now - observed_since >
                 std::chrono::milliseconds(this->global_lock_lease_ms)) {
        // the holder did not release the lock within the lease (crashed)
        std::cerr << "breaking stale global-log lock '" << content << "'"
                  << std::endl;
        auto again = this->connector_ptr->read2(this->global_tmp_log_path);
        if (again->data &&
            std::string(again->data, again->length) == observed) {
          this->connector_ptr->delete_file(this->global_tmp_log_path);
        }
        observed.clear();
        continue;
      }
    }

    if (now - start >
        std::chrono::milliseconds(this->global_lock_timeout_ms)) {
      std::cerr << "timeout while waiting for the global-log lock"
                << std::endl;
      return false;
    }

    std::uniform_int_distribution<uint32_t> jitter(0, backoff_ms);
    std::this_thread::sleep_for(
        std::chrono::milliseconds(backoff_ms + jitter(generator)));
    backoff_ms = std::min<uint32_t>(backoff_ms * 2, 20);
  }
}

void LHTransactions::TransactionManagerDeltaLake::release_global_lock() {
  if (this->global_lock_token.empty()) {
    return;
  }

  auto current = this->connector_ptr->read2(this->global_tmp_log_path);
  if (current->data &&
      std::string(current->data, current->length) == this->global_lock_token) {
    bool deleted = false;
    for (uint32_t attempt = 0; attempt < this->storage_retries && !deleted;
         attempt++) {
      deleted = this->connector_ptr->delete_file(this->global_tmp_log_path);
    }
  }
  this->global_lock_token.clear();
}

LHTransactions::TransactionManagerDeltaLake::Validation
LHTransactions::TransactionManagerDeltaLake::start_validation(
    std::unordered_map<std::string, uint32_t>& version_read_map,
    std::unordered_map<std::string, uint32_t>& version_write_map,
    std::string& content) {
  if (!this->levels[2]) {
    return Validation::Valid;
  }

  for (auto& table : this->tables) {
    if (table->deleted_files.size() != 0 || table->created_files.size() != 0) {
      version_write_map.insert({table->base_path, table->marker_version});
    } else {
      version_read_map.insert({table->base_path, table->read_version});
    }
  }

  // only one transaction may validate and write the global log at a time
  if (!this->acquire_global_lock()) {
    return Validation::LockTimeout;
  }
  content = this->global_lock_token;

  if (this->connector_ptr->check(this->global_log_path)) {
    auto last_snapshot_data = this->read_retry(this->global_log_path);
    if (!last_snapshot_data->data) {
      // without the global log we cannot validate
      return Validation::Invalid;
    }

    std::stringstream stream(
        std::string(last_snapshot_data->data, last_snapshot_data->length));

    for (std::string line; std::getline(stream, line);) {
      std::string path;
      uint32_t version = 0;
      if (!parse_global_line(line, path, version)) {
        continue;
      }

      // tables that were only read: validated for serializability only
      auto read = version_read_map.find(path);
      if (read != version_read_map.end() && version > read->second) {
        if (this->options.isolation == Isolation::Serializable) {
          std::cerr << "global log: " << path << " was committed at version "
                    << version << " after the read version " << read->second
                    << std::endl;
          return Validation::Invalid;
        }
        // snapshot isolation: the global log must not go backwards
        read->second = version;
      }

      if (version_write_map.count(path) != 0) {
        if (version >= version_write_map.find(path)->second) {
          std::cerr << "global log: " << path << " is at version " << version
                    << ", not below the write version "
                    << version_write_map.find(path)->second << std::endl;
          return Validation::Invalid;
        }
      } else {
        version_read_map.insert({path, version});
      }
    }
  }

  return Validation::Valid;
}

bool LHTransactions::TransactionManagerDeltaLake::create_global_snapshot(
    std::unordered_map<std::string, uint32_t>& version_read_map,
    std::unordered_map<std::string, uint32_t>& version_write_map,
    std::string& content) {
  if (!this->levels[2]) {
    return true;
  }

  std::stringstream global_log_content;

  for (auto& ref : version_read_map) {
    if (version_write_map.count(ref.first) == 0) {
      global_log_content << ref.first << " " << ref.second << "\n";
    }
  }
  for (auto& ref : version_write_map) {
    global_log_content << ref.first << " " << ref.second << "\n";
  }

  std::string global_log_content_str = global_log_content.str();

  bool written = false;
  for (uint32_t attempt = 0; attempt < this->storage_retries && !written;
       attempt++) {
    written = this->connector_ptr->write(this->global_log_path,
                                         global_log_content_str.c_str(),
                                         global_log_content_str.size());
  }
  if (!written) {
    // the commit itself is in the Delta log; LV[Read] clients see it once a
    // later commit to the table updates the global log
    std::cerr << "unable to update the global log " << this->global_log_path
              << std::endl;
  }

  this->release_global_lock();

  return written;
}

bool LHTransactions::TransactionManagerDeltaLake::read_part(
    std::shared_ptr<arrow::Table>& table_ptr, std::vector<std::string>& list,
    uint32_t start, uint32_t end, TableStats& table) {
  table_ptr = nullptr;
  for (uint32_t i = start; i < end; i++) {
    auto partial_table = this->read_data_file(table, list[i]);

    if (!partial_table) {
      std::cerr << "read error for " << list[i] << std::endl;
      table_ptr = nullptr;
      return false;
    }

    if (table_ptr) {
      auto con_tbl = arrow::ConcatenateTables({table_ptr, partial_table});
      if (!con_tbl.ok()) {
        std::cerr << "concat error: " << con_tbl.status().ToString()
                  << std::endl;
        table_ptr = nullptr;
        return false;
      }
      table_ptr = *con_tbl;
    } else {
      table_ptr = partial_table;
    }
  }
  return true;
}

void LHTransactions::TransactionManagerDeltaLake::read_part_simple(
    std::vector<std::string>& list, uint32_t start, uint32_t end) {
  uint32_t counter = 0;

  for (int i = start; i < end; i++) {
    connector_ptr->read_mock(list[i]);

    counter++;
  }
}

void LHTransactions::TransactionManagerDeltaLake::read_part_simple_skip(
    std::vector<std::string>& list, uint32_t start, uint32_t end,
    uint32_t skip) {
  uint32_t counter = 0;
  uint32_t skip_counter = 0;

  for (int i = start; i < end; i++) {
    if (skip_counter == skip) {
      skip_counter = 0;
    } else {
      skip_counter++;
      connector_ptr->read_mock(list[i]);
    }
    counter++;
  }
}

void LHTransactions::TransactionManagerDeltaLake::head_part_simple(
    std::vector<std::string>& list, uint32_t start, uint32_t end) {
  uint32_t counter = 0;

  for (int i = start; i < end; i++) {
    connector_ptr->check(list[i]);

    counter++;
  }
}

std::shared_ptr<arrow::Array>
LHTransactions::TransactionManagerDeltaLake::build_string_col(
    std::string& name, std::shared_ptr<arrow::ChunkedArray>& old_col,
    LHTransactions::UpdateOperations op, std::string& val) {
  // the operation is applied to every row (all rows of the key)
  arrow::StringBuilder builder;
  for (auto& chunk : old_col->chunks()) {
    auto strings = std::static_pointer_cast<arrow::StringArray>(chunk);
    for (int64_t i = 0; i < strings->length(); i++) {
      if (op == LHTransactions::UpdateOperations::REP) {
        PARQUET_THROW_NOT_OK(builder.Append(val));
        continue;
      }
      if (strings->IsNull(i) || op == LHTransactions::UpdateOperations::NONE) {
        PARQUET_THROW_NOT_OK(strings->IsNull(i)
                                 ? builder.AppendNull()
                                 : builder.Append(strings->GetView(i)));
        continue;
      }
      auto change = std::stof(val);
      auto old_value = std::stof(strings->GetString(i));
      float value = old_value;
      switch (op) {
        case LHTransactions::UpdateOperations::ADD:
          value = change + old_value;
          break;
        case LHTransactions::UpdateOperations::SUB:
          value = old_value - change;
          break;
        case LHTransactions::UpdateOperations::DIV:
          value = old_value / change;
          break;
        case LHTransactions::UpdateOperations::MUL:
          value = old_value * change;
          break;
        default:
          break;
      }
      PARQUET_THROW_NOT_OK(builder.Append(std::to_string(value)));
    }
  }

  std::shared_ptr<arrow::Array> arrow_array;

  PARQUET_THROW_NOT_OK(builder.Finish(&arrow_array));

  return arrow_array;
}

std::shared_ptr<arrow::Array>
LHTransactions::TransactionManagerDeltaLake::build_int_col(
    std::string& name, std::shared_ptr<arrow::ChunkedArray>& old_col,
    LHTransactions::UpdateOperations op, int val) {
  // the operation is applied to every row (all rows of the key)
  arrow::Int32Builder builder;
  for (auto& chunk : old_col->chunks()) {
    auto ints = std::static_pointer_cast<arrow::Int32Array>(chunk);
    for (int64_t i = 0; i < ints->length(); i++) {
      if (op == LHTransactions::UpdateOperations::REP) {
        PARQUET_THROW_NOT_OK(builder.Append(val));
        continue;
      }
      if (ints->IsNull(i)) {
        PARQUET_THROW_NOT_OK(builder.AppendNull());
        continue;
      }
      int32_t value = ints->Value(i);
      switch (op) {
        case LHTransactions::UpdateOperations::ADD:
          value = value + val;
          break;
        case LHTransactions::UpdateOperations::SUB:
          value = value - val;
          break;
        case LHTransactions::UpdateOperations::DIV:
          value = value / val;
          break;
        case LHTransactions::UpdateOperations::MUL:
          value = value * val;
          break;
        default:
          break;
      }
      PARQUET_THROW_NOT_OK(builder.Append(value));
    }
  }

  std::shared_ptr<arrow::Array> arrow_array;

  PARQUET_THROW_NOT_OK(builder.Finish(&arrow_array));

  return arrow_array;
}

bool LHTransactions::TransactionManagerDeltaLake::create_checkpoint(
    uint32_t tbl_id) {
  auto* table = this->get_table(tbl_id);
  if (!table) {
    return false;
  }

  // a checkpoint must describe exactly the snapshot at its version
  if (!table->created_files.empty() || !table->deleted_files.empty()) {
    std::cerr << "checkpoint: the transaction has uncommitted changes"
              << std::endl;
    return false;
  }

  // versions reserved by concurrent transactions must be final
  auto log_dir = table->base_path + "_delta_log/";
  for (auto& marker : this->prev_markers) {
    auto version = parse_log_version(marker);
    if (marker.compare(0, log_dir.size(), log_dir) != 0 || !version ||
        *version > table->read_version) {
      continue;
    }
    if (this->connector_ptr->check(marker) && !this->wait_for_marker(marker)) {
      std::cerr << "checkpoint: version " << *version << " is still reserved"
                << std::endl;
      return false;
    }
  }

  return this->write_checkpoint(table->base_path, table->read_version,
                                *table->delta_log);
}

std::shared_ptr<LHExecutor::LHDL::DeltaLogManager>
LHTransactions::TransactionManagerDeltaLake::load_snapshot(
    const std::string& base_path, uint32_t version) {
  auto delta_log =
      std::make_shared<LHExecutor::LHDL::DeltaLogManager>(base_path);
  auto checkpoint = this->find_checkpoint(base_path, version);
  if (!this->parse_checkpoint(base_path, *delta_log, checkpoint)) {
    return nullptr;
  }

  // the versions after the checkpoint (in parallel: actions are applied by
  // version, in any order)
  std::atomic<bool> ok = true;
  uint32_t first = checkpoint.version ? *checkpoint.version + 1 : 0;
  std::vector<std::jthread> threads;
  for (auto& range : split_range(first, version + 1, 4)) {
    threads.emplace_back([this, range, &base_path, &delta_log, &ok] {
      for (uint32_t v = range.first; v < range.second && ok; v++) {
        auto path = base_path + "_delta_log/" + log_file_name(v, ".json");
        auto content = this->read_retry(path);
        if (!content->data) {
          ok = false;
          return;
        }
        std::stringstream lines(std::string(content->data, content->length));
        for (std::string line; std::getline(lines, line);) {
          if (line.size() <= 1) {
            continue;
          }
          auto parsed = LHHelpers::readJSON_rapid(line.c_str());
          if (parsed->HasParseError() || !parsed->IsObject()) {
            continue;
          }
          if (parsed->HasMember("marker")) {
            // reserved by a running transaction: not final
            ok = false;
            return;
          }
          apply_log_action(*parsed, *delta_log, v);
        }
      }
    });
  }
  for (auto& thread : threads) {
    thread.join();
  }
  return ok ? delta_log : nullptr;
}

bool LHTransactions::TransactionManagerDeltaLake::write_checkpoint(
    const std::string& base_path, uint32_t version,
    LHExecutor::LHDL::DeltaLogManager& delta_log) {
  auto unsupported = delta_log.get_unsupported();
  if (!unsupported.empty()) {
    std::cerr << "checkpoint: the table uses " << unsupported
              << ", which LakeVilla does not track" << std::endl;
    return false;
  }

  LHHelpers::CheckpointContent content;
  content.protocol = delta_log.get_protocol();
  content.metadata = delta_log.get_metadata();
  if (content.protocol.empty() || content.metadata.empty()) {
    std::cerr << "checkpoint: protocol or metadata of the table unknown"
              << std::endl;
    return false;
  }

  auto blocker = checkpoint_blocker(content.protocol);
  if (!blocker.empty()) {
    std::cerr << "checkpoint: the table uses " << blocker
              << ", which LakeVilla does not support" << std::endl;
    return false;
  }

  for (auto& [path, entry] : delta_log.get_entries()) {
    content.adds.push_back({path, entry.stats, entry.size,
                            entry.modification_time, entry.partition_values,
                            entry.tags});
  }

  // tombstones are kept for delta.deletedFileRetentionDuration (VACUUM)
  auto retention_start =
      LHHelpers::now_ms() - tombstone_retention_ms(content.metadata);
  for (auto& [path, entry] : delta_log.get_tombstones()) {
    if (entry.deletion_timestamp >= retention_start) {
      content.removes.push_back({path, entry.deletion_timestamp, entry.size,
                                 entry.partition_values});
    }
  }

  content.txns = delta_log.get_transactions();
  content.domains = delta_log.get_domain_metadata();

  std::unique_ptr<LHHelpers::ParquetPair> checkpoint_data;
  try {
    checkpoint_data = LHHelpers::generateCheckpoint(content);
  } catch (const std::exception& e) {
    std::cerr << "checkpoint: " << e.what() << std::endl;
    return false;
  }

  // a checkpoint of the version that exists already (written by another
  // client, e.g. Spark) describes the same snapshot: keep it
  auto log_dir = base_path + "_delta_log/";
  auto status = this->connector_ptr->write_atomic(
      log_dir + log_file_name(version, ".checkpoint.parquet"),
      checkpoint_data->first, checkpoint_data->second);
  if (status != StorageConnector::WriteStatus::Ok &&
      status != StorageConnector::WriteStatus::AlreadyExists) {
    std::cerr << "checkpoint: unable to write version " << version << " of "
              << base_path << std::endl;
    return false;
  }

  std::stringstream last_checkpoint;
  last_checkpoint << "{\"version\":" << version << ",\"size\":"
                  << (2 + content.adds.size() + content.removes.size() +
                      content.txns.size() + content.domains.size());
  if (status == StorageConnector::WriteStatus::Ok) {
    // only known for our file
    last_checkpoint << ",\"sizeInBytes\":" << checkpoint_data->second
                    << ",\"numOfAddFiles\":" << content.adds.size();
  }
  last_checkpoint << "}";
  auto last_str = last_checkpoint.str();

  // _last_checkpoint never moves backwards: conditional on the version read
  for (uint32_t attempt = 0; attempt < this->storage_retries; attempt++) {
    std::string etag;
    auto last = this->read_last_checkpoint(base_path, &etag);
    if (last.version && *last.version >= version) {
      return true;
    }
    auto path = log_dir + "_last_checkpoint";
    status = etag.empty()
                 ? this->connector_ptr->write_atomic(path, last_str.c_str(),
                                                     last_str.size())
                 : this->connector_ptr->write_if_match(
                       path, last_str.c_str(), last_str.size(), etag);
    if (status == StorageConnector::WriteStatus::Ok) {
      return true;
    }
    if (status == StorageConnector::WriteStatus::Error) {
      std::this_thread::sleep_for(
          std::chrono::milliseconds(LV_RETRY_BASE_MS << attempt));
    }
  }
  std::cerr << "checkpoint: unable to update _last_checkpoint of " << base_path
            << std::endl;
  return false;
}

void LHTransactions::TransactionManagerDeltaLake::checkpoint_after_commit(
    TableStats& table, uint32_t version) {
  uint32_t interval = this->options.checkpoint_interval > 0
                          ? this->options.checkpoint_interval
                          : checkpoint_interval(table.delta_log->get_metadata());
  if (version == 0 || version % interval != 0) {
    return;
  }
  // all versions up to the commit are final: the commit waited for them
  auto snapshot = this->load_snapshot(table.base_path, version);
  if (!snapshot || !this->write_checkpoint(table.base_path, version, *snapshot)) {
    std::cerr << "no checkpoint written for version " << version << " of "
              << table.base_path << std::endl;
  }
}
