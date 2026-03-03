#include "CABClientTableScaling.hpp"

LHCAB::CABClientTableScaling::CABClientTableScaling(
    uint32_t max_tables, std::string scale_table, std::string& base_path,
    uint64_t tendant, StorageConnector::MinIOConfig config, uint32_t id,
    preparedContent* content)
    : CABClient(base_path, tendant, config, id, content) {
  this->max_tables = max_tables;
  this->scale_table = scale_table;
}

bool LHCAB::CABClientTableScaling::executeMultiTableTransaction(
    uint32_t runs, uint32_t num_tables,
    std::vector<std::pair<int64_t, int64_t>>& times) {
  std::vector<std::string> table_paths;
  for (uint32_t i = 0; i < num_tables; i++) {
    std::stringstream s;
    s << this->scale_table << i << "/";
    table_paths.push_back(s.str());
  }

  for (uint32_t i = 0; i < runs; i++) {
    auto begin = std::chrono::high_resolution_clock::now();
    auto txnmanager = LHTransactions::TransactionManagerGeneric(
        this->level_config, table_paths[0], this->connector.get(), this->id);
    bool started = false;
    while (!started) {
      started = txnmanager.begin_transaction_ycsb();
    }

    auto tbl_id = txnmanager.get_table_id(table_paths[0]);
    std::string key = "key";
    txnmanager.add_file(tbl_id, table_paths[0], key);

    for (uint32_t j = 1; j < num_tables; j++) {
      txnmanager.open_new_table(table_paths[j]);
      auto tbl_id2 = txnmanager.get_table_id(table_paths[j]);
      txnmanager.add_file(tbl_id2, this->content->customer, key);
    }

    auto commit_begin = std::chrono::high_resolution_clock::now();
    txnmanager.commit();
    auto end = std::chrono::high_resolution_clock::now();

    auto txn_latency =
        std::chrono::duration_cast<std::chrono::milliseconds>(end - begin)
            .count();

    auto commit_latency = std::chrono::duration_cast<std::chrono::milliseconds>(
                              end - commit_begin)
                              .count();
    times.push_back({txn_latency, commit_latency});
  }

  return true;
}

bool LHCAB::CABClientTableScaling::startAll(
    std::vector<std::vector<bool>> levels, std::vector<bool>& triggers,
    bool& end, std::vector<std::vector<std::pair<int64_t, int64_t>>>& times,
    double& reads_mb, double& writes_mb) {
  for (uint32_t num_tables = 1; num_tables <= this->max_tables; num_tables++) {
    times.emplace_back();
    std::cerr << num_tables << " tables" << std::endl;
    this->executeMultiTableTransaction(3, num_tables, times.back());
  }

  std::cerr << "benchmark ended" << std::endl;

  for (size_t i = 0; i < times.size(); i++) {
    std::cout << "---" << i + 1 << "---" << std::endl;
    std::cout << "latency;commit" << std::endl;

    for (auto& ref : times[i]) {
      std::cout << ref.first << ";" << ref.second << std::endl;
    }
  }

  std::cout << "--- avgs ---" << std::endl;
  std::cout << "table;latency;commit" << std::endl;

  for (size_t i = 0; i < times.size(); i++) {
    int64_t latency = 0;
    int64_t commit = 0;
    for (auto& ref : times[i]) {
      latency += (ref.first / 3);
      commit += (ref.second / 3);
    }
    std::cout << i << ";" << latency << ";" << commit << std::endl;
  }

  return true;
}