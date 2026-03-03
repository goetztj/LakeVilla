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

  this->txnmanager =
      std::make_unique<LHTransactions::TransactionManagerGeneric>(
          level_config, this->paths.customer_address, connector, id);
  this->txnmanager->begin_transaction_ycsb();
  this->txnmanager->open_new_table(this->paths.date_dim);
  this->txnmanager->open_new_table(this->paths.household_demographics);
  this->txnmanager->open_new_table(this->paths.item);
  this->txnmanager->open_new_table(this->paths.reason);
  this->txnmanager->open_new_table(this->paths.store);
  this->txnmanager->open_new_table(this->paths.store_sales);
  this->txnmanager->open_new_table(this->paths.time_dim);
  this->txnmanager->open_new_table(this->paths.web_page);
  this->txnmanager->open_new_table(this->paths.web_sales);

  for (auto& t : tbls) {
    if (!this->txnmanager) {
      this->txnmanager =
          std::make_unique<LHTransactions::TransactionManagerGeneric>(
              level_config, t, connector, id);
      this->txnmanager->begin_transaction_ycsb();
    } else {
      this->txnmanager->open_new_table(t);
    }
    auto tbl_id = this->txnmanager->get_table_id(t);

    this->txnmanager->create_checkpoint(tbl_id);
    counter++;
    if (counter > this->num_queries) {
      break;
    }
  }

  if (this->txnmanager) {
    this->txnmanager->commit();
  }
}