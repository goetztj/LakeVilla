#include "CABClientWriteSwitching.hpp"

LHCAB::CABClientWriteSwitching::CABClientWriteSwitching(
    uint32_t num_clients, std::string& base_path, uint64_t tendant,
    StorageConnector::MinIOConfig config, uint32_t id, preparedContent* content)
    : CABClient(base_path, tendant, config, id, content) {
  this->num_clients = num_clients;
}

bool LHCAB::CABClientWriteSwitching::executeWriteStream(
    std::vector<std::vector<bool>> levels, Phases& phases,
    std::vector<std::vector<std::pair<int64_t, int64_t>>>& times,
    double& reads_mb, double& writes_mb) {
  this->level_config = levels[0];
  this->read_mb_count = 0;
  this->write_mb_count = 0;
  uint32_t next_stage = 1;
  uint32_t curr_stage = 0;

  times.resize(phases.size());

  phases.wait_for_start();

  auto begin = std::chrono::high_resolution_clock::now();

  while (!phases.end) {
    if (next_stage < phases.size() && phases.started(next_stage)) {
      this->level_config = levels[next_stage];
      next_stage++;
      curr_stage++;
    }

    std::random_device s;
    std::mt19937 generator{s()};

    std::uniform_int_distribution<> distr{1, 100};

    auto number = distr(generator);

    bool success = false;

    // retry the operation until it commits (or the run ends)
    while (!phases.end) {
      auto op_start = std::chrono::high_resolution_clock::now();
      // 20% updates and 80% updates per stream

      if (number < 21) {
        std::uniform_int_distribution<> distr2{1, 9};

        auto number2 = distr2(generator);
        try {
          switch (number2) {
            case 1:
              success = this->uTable(this->customer_path);
              break;
            case 2:
              success = this->uTable(this->lineitem_path);
              break;
            case 3:
              success = this->uTable(this->nation_path);
              break;
            case 4:
              success = this->uTable(this->orders_path);
              break;
            case 5:
              success = this->uTable(this->part_path);
              break;
            case 6:
              success = this->uTable(this->partsupp_path);
              break;
            case 7:
              success = this->uTable(this->region_path);
              break;
            case 8:
              success = this->uTable(this->supplier_path);
              break;
            default:
              success = this->uTable(this->supplier_path);
              break;
          }
        } catch (...) {
          std::cerr << "error in update stream" << std::endl;
          success = false;
        }
      } else {
        // std::cerr << "insert" << std::endl;
        //  insert
        std::uniform_int_distribution<> distr2{1, 8};

        auto number2 = distr2(generator);
        // this->level_config = {true, false, false};
        try {
          switch (number2) {
            case 1:
              success = this->iCustomer();
              break;
            case 2:
              success = this->iLineitem();
              break;
            case 3:
              success = this->iNation();
              break;
            case 4:
              success = this->iOrders();
              break;
            case 5:
              success = this->iPart();
              break;
            case 6:
              success = this->iPartsupp();
              break;
            case 7:
              success = this->iRegion();
              break;
            default:
              success = this->iSupplier();
              break;
          }
        } catch (...) {
          std::cerr << "error in write stream" << std::endl;
          success = false;
        }
      }

      auto op_end = std::chrono::high_resolution_clock::now();

      if (success) {
        auto op_dur = std::chrono::duration_cast<std::chrono::milliseconds>(
                          op_end - op_start)
                          .count();
        auto time_dur = std::chrono::duration_cast<std::chrono::milliseconds>(
                            op_start - begin)
                            .count();
        times[curr_stage].push_back({time_dur, op_dur});
        break;
      } else {
        auto time_dur = std::chrono::duration_cast<std::chrono::milliseconds>(
                            op_end - begin)
                            .count();
        times[curr_stage].push_back({time_dur, -1});
      }
    }
  }

  reads_mb = this->read_mb_count;
  writes_mb = this->write_mb_count;
  return true;
}

bool LHCAB::CABClientWriteSwitching::startAll(
    std::vector<std::vector<bool>> levels, Phases& phases,
    std::vector<std::vector<std::pair<int64_t, int64_t>>>& times,
    double& reads_mb, double& writes_mb) {
  // one client object per thread: level configuration, transaction ids and
  // counters are per client
  std::vector<std::unique_ptr<CABClientWriteSwitching>> clients;
  // sized before the threads start (no reallocation while they write)
  std::vector<std::vector<std::vector<std::pair<int64_t, int64_t>>>> part_times(
      num_clients);
  std::vector<double> part_reads(num_clients, 0), part_writes(num_clients, 0);
  for (uint32_t i = 0; i < num_clients; i++) {
    clients.push_back(std::make_unique<CABClientWriteSwitching>(
        1, this->base_path, this->tendant, this->config, this->id,
        this->content));
  }

  {
    std::vector<std::jthread> threads;
    for (uint32_t i = 0; i < num_clients; i++) {
      threads.emplace_back([&, i] {
        clients[i]->executeWriteStream(levels, phases, part_times[i],
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

bool LHCAB::CABClientWriteSwitching::executeIngestionEnd() {
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
