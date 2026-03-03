#include "CABClientWriteSwitching.hpp"

LHCAB::CABClientWriteSwitching::CABClientWriteSwitching(
    uint32_t num_clients, std::string& base_path, uint64_t tendant,
    StorageConnector::MinIOConfig config, uint32_t id, preparedContent* content)
    : CABClient(base_path, tendant, config, id, content) {
  this->num_clients = num_clients;
}

bool LHCAB::CABClientWriteSwitching::executeWriteStream(
    std::vector<std::vector<bool>> levels, std::vector<bool>& triggers,
    bool& end, std::vector<std::vector<std::pair<int64_t, int64_t>>>& times,
    double& reads_mb, double& writes_mb) {
  this->level_config = levels[0];
  this->read_mb_count = 0;
  this->write_mb_count = 0;
  uint32_t next_stage = 1;
  uint32_t curr_stage = 0;

  while (times.size() < triggers.size()) {
    times.emplace_back();
  }

  while (!(triggers[0])) {
    // wait
  }

  auto begin = std::chrono::high_resolution_clock::now();

  while (!end) {
    if (next_stage < triggers.size() && triggers[next_stage]) {
      this->level_config = levels[next_stage];
      next_stage++;
      curr_stage++;
    }

    std::random_device s;
    std::mt19937 generator{s()};

    std::uniform_int_distribution<> distr{1, 100};

    auto number = distr(generator);

    bool success = false;

    while (true) {
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
    std::vector<std::vector<bool>> levels, std::vector<bool>& triggers,
    bool& end, std::vector<std::vector<std::pair<int64_t, int64_t>>>& times,
    double& reads_mb, double& writes_mb) {
  std::vector<std::vector<std::vector<std::pair<int64_t, int64_t>>>> part_times;
  std::vector<std::thread> threads;
  for (uint32_t i = 0; i < num_clients; i++) {
    part_times.emplace_back();

    threads.emplace_back([this, &part_times, i, &levels, &end, &triggers,
                          &reads_mb, &writes_mb] {
      this->executeWriteStream(levels, triggers, end, part_times[i], reads_mb,
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

bool LHCAB::CABClientWriteSwitching::executeIngestionEnd() {
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
