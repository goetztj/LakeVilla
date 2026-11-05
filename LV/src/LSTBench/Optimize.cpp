#include "Optimize.hpp"

#include <algorithm>

LHLST::Optimize::Optimize(TpcdsPaths paths,
                          StorageConnector::MinIOConnector* connector,
                          uint32_t num_queries, uint32_t id,
                          preparedLSTContent* content,
                          std::vector<bool> level_config)
    : Phase(paths, connector, num_queries, id, level_config) {
  this->txnmanager = nullptr;

  this->content = content;
}

void LHLST::Optimize::run(std::vector<double>& times) {
  std::vector<std::string> tbls = {this->paths.customer_address,
                                   this->paths.date_dim,
                                   this->paths.household_demographics,
                                   this->paths.item,
                                   this->paths.reason,
                                   this->paths.store,
                                   this->paths.store_sales,
                                   this->paths.time_dim,
                                   this->paths.web_page,
                                   this->paths.web_sales};

  uint32_t counter = 0;

  // every table is opened once (opening it again reserved a second version
  // per table with markers)
  for (auto& t : tbls) {
    if (counter >= this->num_queries) {
      break;
    }
    if (!this->txnmanager) {
      this->txnmanager =
          std::make_unique<LHTransactions::TransactionManagerDeltaLake>(
              level_config, t, connector, id);
      if (!this->txnmanager->begin_transaction_ycsb()) {
        std::cerr << "unable to open " << t << std::endl;
        this->txnmanager = nullptr;
        return;
      }
    } else if (!this->txnmanager->open_new_table(t)) {
      std::cerr << "unable to open " << t << std::endl;
      continue;
    }
    auto tbl_id = this->txnmanager->get_table_id(t);

    if (!this->txnmanager->create_checkpoint(tbl_id)) {
      std::cerr << "no checkpoint written for " << t << std::endl;
    }
    counter++;
  }

  if (this->txnmanager) {
    this->txnmanager->commit();
  }
}