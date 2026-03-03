#include "CABClientReadSwitching.hpp"

LHCAB::CABClientReadSwitching::CABClientReadSwitching(
    uint32_t num_clients, std::string& base_path, uint64_t tendant,
    StorageConnector::MinIOConfig config, uint32_t stream_id,
    LHCAB::preparedContent* content)
    : CABClient(base_path, tendant, config, stream_id, content) {
  this->num_clients = num_clients;
}

bool LHCAB::CABClientReadSwitching::startAll(
    std::vector<std::vector<bool>> levels, std::vector<bool>& triggers,
    bool& end, std::vector<std::vector<std::pair<int64_t, int64_t>>>& times,
    double& reads_mb, double& writes_mb) {
  std::vector<std::vector<std::vector<std::pair<int64_t, int64_t>>>> part_times;
  std::vector<std::thread> threads;
  for (uint32_t i = 0; i < num_clients; i++) {
    part_times.emplace_back();

    threads.emplace_back([this, &part_times, i, &levels, &end, &triggers,
                          &reads_mb, &writes_mb] {
      this->executeReadStream(levels, triggers, end, part_times[i], reads_mb,
                              writes_mb);
    });
  }

  std::cerr << "All threads started" << std::endl;

  for (auto& ref : threads) {
    ref.join();
  }

  std::cerr << "Benchmark stopped; merging results" << std::endl;

  while (times.size() < triggers.size()) {
    times.emplace_back();
  }

  for (uint32_t step = 0; step < triggers.size(); step++) {
    for (auto& part : part_times) {
      for (auto& res : part[step]) {
        times[step].push_back(res);
      }
    }
  }

  return true;
}

bool LHCAB::CABClientReadSwitching::executeReadStream(
    std::vector<std::vector<bool>> levels, std::vector<bool>& triggers,
    bool& end, std::vector<std::vector<std::pair<int64_t, int64_t>>>& times,
    double& reads_mb, double& writes_mb) {
  std::ifstream q_file;
  q_file.open(this->query_file);

  if (!q_file.is_open()) {
    std::cerr << "Error opening query file" << query_file << std::endl;
  }

  std::string query_str((std::istreambuf_iterator<char>(q_file)),
                        std::istreambuf_iterator<char>());

  std::cerr << "open" << std::endl;
  auto workload = LHHelpers::readJSON_rapid(query_str.c_str());
  std::cerr << "parsed" << std::endl;

  if (workload->HasMember("queries") && (*workload)["queries"].IsArray()) {
    auto queries = (*workload)["queries"].GetArray();

    std::vector<int> query_ids;

    for (auto& entry : queries) {
      if (entry.HasMember("query_id") && entry["query_id"].IsInt()) {
        query_ids.push_back(entry["query_id"].GetInt());
      } else {
        std::cerr << "Array element without id in " << this->query_file
                  << std::endl;
      }
    }

    times.reserve(triggers.size());

    for (int i = 0; i < triggers.size(); i++) {
      times.emplace_back();
    }

    while (!(triggers[0])) {
      // wait
    }
    auto begin = std::chrono::high_resolution_clock::now();

    this->level_config = levels[0];

    uint32_t counter = 0;
    uint32_t step = 1;
    uint32_t current_step = 0;
    while (!end) {
      if (step < triggers.size() && triggers[step]) {
        this->level_config = levels[step];
        step++;
        current_step++;
      }

      auto begin_query = std::chrono::high_resolution_clock::now();
      if (!this->start_query(query_ids[counter], this->level_config)) {
        std::cerr << "query execution failed for Q" << query_ids[counter]
                  << std::endl;
        return false;
      }
      auto end_query = std::chrono::high_resolution_clock::now();

      auto op_dur = std::chrono::duration_cast<std::chrono::milliseconds>(
                        end_query - begin_query)
                        .count();
      auto time_dur = std::chrono::duration_cast<std::chrono::milliseconds>(
                          begin_query - begin)
                          .count();
      times[current_step].push_back({time_dur, op_dur});

      counter++;
      if (counter >= queries.Size()) {
        counter = 0;
      }
    }

    auto end = std::chrono::high_resolution_clock::now();

    reads_mb = this->read_mb_count;
    writes_mb = this->write_mb_count;
    return true;
  } else {
    std::cerr << "invalid input file " << this->query_file << std::endl;
    return false;
  }
}

bool LHCAB::CABClientReadSwitching::executeIngestionEnd() {
  auto txnmanager = LHTransactions::TransactionManagerGeneric(
      {false, false, true}, this->customer_path, this->connector.get(),
      this->id);

  bool started = false;
  while (!started) {
    started = txnmanager.begin_transaction_ycsb();
  }

  txnmanager.open_new_table(this->lineitem_path);
  txnmanager.open_new_table(this->nation_path);
  txnmanager.open_new_table(this->orders_path);
  txnmanager.open_new_table(this->part_path);
  txnmanager.open_new_table(this->partsupp_path);
  txnmanager.open_new_table(this->region_path);
  txnmanager.open_new_table(this->supplier_path);

  return txnmanager.commit(this->read_mb_count, this->write_mb_count);
}
