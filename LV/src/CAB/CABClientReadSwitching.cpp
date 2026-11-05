#include "CABClientReadSwitching.hpp"

LHCAB::CABClientReadSwitching::CABClientReadSwitching(
    uint32_t num_clients, std::string& base_path, uint64_t tendant,
    StorageConnector::MinIOConfig config, uint32_t stream_id,
    LHCAB::preparedContent* content)
    : CABClient(base_path, tendant, config, stream_id, content) {
  this->num_clients = num_clients;
}

bool LHCAB::CABClientReadSwitching::startAll(
    std::vector<std::vector<bool>> levels, Phases& phases,
    std::vector<std::vector<std::pair<int64_t, int64_t>>>& times,
    double& reads_mb, double& writes_mb) {
  // one client object per thread: level configuration, transaction ids and
  // counters are per client
  std::vector<std::unique_ptr<CABClientReadSwitching>> clients;
  // sized before the threads start (no reallocation while they write)
  std::vector<std::vector<std::vector<std::pair<int64_t, int64_t>>>> part_times(
      num_clients);
  std::vector<double> part_reads(num_clients, 0), part_writes(num_clients, 0);
  for (uint32_t i = 0; i < num_clients; i++) {
    clients.push_back(std::make_unique<CABClientReadSwitching>(
        1, this->base_path, this->tendant, this->config, this->id,
        this->content));
  }

  {
    std::vector<std::jthread> threads;
    for (uint32_t i = 0; i < num_clients; i++) {
      threads.emplace_back([&, i] {
        clients[i]->executeReadStream(levels, phases, part_times[i],
                                      part_reads[i], part_writes[i]);
      });
    }
    std::cerr << "All threads started" << std::endl;
  }

  std::cerr << "Benchmark stopped; merging results" << std::endl;

  while (times.size() < phases.size()) {
    times.emplace_back();
  }

  reads_mb = 0;
  writes_mb = 0;
  for (uint32_t i = 0; i < num_clients; i++) {
    reads_mb += part_reads[i];
    writes_mb += part_writes[i];
    for (uint32_t step = 0; step < part_times[i].size(); step++) {
      for (auto& res : part_times[i][step]) {
        times[step].push_back(res);
      }
    }
  }

  return true;
}

bool LHCAB::CABClientReadSwitching::executeReadStream(
    std::vector<std::vector<bool>> levels, Phases& phases,
    std::vector<std::vector<std::pair<int64_t, int64_t>>>& times,
    double& reads_mb, double& writes_mb) {
  std::ifstream q_file;
  q_file.open(this->query_file);

  if (!q_file.is_open()) {
    std::cerr << "Error opening query file " << query_file << std::endl;
    return false;
  }

  std::string query_str((std::istreambuf_iterator<char>(q_file)),
                        std::istreambuf_iterator<char>());

  auto workload = LHHelpers::readJSON_rapid(query_str.c_str());

  if (!workload->IsObject() || !workload->HasMember("queries") ||
      !(*workload)["queries"].IsArray()) {
    std::cerr << "invalid input file " << this->query_file << std::endl;
    return false;
  }

  std::vector<int> query_ids;
  for (auto& entry : (*workload)["queries"].GetArray()) {
    if (entry.HasMember("query_id") && entry["query_id"].IsInt()) {
      query_ids.push_back(entry["query_id"].GetInt());
    } else {
      std::cerr << "Array element without id in " << this->query_file
                << std::endl;
    }
  }
  if (query_ids.empty()) {
    std::cerr << "no queries in " << this->query_file << std::endl;
    return false;
  }

  times.resize(phases.size());

  phases.wait_for_start();
  auto begin = std::chrono::high_resolution_clock::now();

  this->level_config = levels[0];

  uint32_t counter = 0;
  uint32_t step = 1;
  uint32_t current_step = 0;
  while (!phases.end) {
    if (step < phases.size() && phases.started(step)) {
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

    counter = (counter + 1) % query_ids.size();
  }

  reads_mb = this->read_mb_count;
  writes_mb = this->write_mb_count;
  return true;
}

bool LHCAB::CABClientReadSwitching::executeIngestionEnd() {
  auto txnmanager = LHTransactions::TransactionManagerDeltaLake(
      {false, false, true}, this->customer_path, this->connector.get(),
      LHTransactions::unique_transaction_id());

  // begin_transaction_ycsb() retries internally
  if (!txnmanager.begin_transaction_ycsb()) {
    return false;
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
