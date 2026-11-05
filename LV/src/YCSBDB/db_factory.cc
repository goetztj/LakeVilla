//
//  basic_db.cc
//  YCSB-C
//
//  Created by Jinglei Ren on 12/17/14.
//  Copyright (c) 2014 Jinglei Ren <jinglei@ren.systems>.
//
// adapted for the LakeVilla Prototype by Tobias Götz

#include "db_factory.h"

#include <string>
#include "basic_db.h"
#include "lhtransactions.hpp"
#include "lhtransactionsBanking.hpp"

using namespace std;
using ycsbc::DB;
using ycsbc::DBFactory;

DB* DBFactory::CreateDB(utils::Properties &props) {
  std::string config_path = props["config"];
  if (props["dbname"] == "basic") {
    return new BasicDB;
  } else if (props["dbname"] == "LakeVillaWrite") {
    return new LHTransactionsDB(config_path, {true, true, false}, false);
  } else if (props["dbname"] == "LakeVillaRead") {
    return new LHTransactionsDB(config_path, {false, false, true}, false);
  } else if (props["dbname"] == "LakeVilla") {
    return new LHTransactionsDB(config_path, {true, true, true}, false);
  } else if (props["dbname"] == "LakeVillaWriteMulti") {
    return new LHTransactionsDB(config_path, {true, true, false}, true);
  } else if (props["dbname"] == "LakeVillaReadMulti") {
    return new LHTransactionsDB(config_path, {false, false, true}, true);
  } else if (props["dbname"] == "LakeVillaMulti") {
    return new LHTransactionsDB(config_path, {true, true, true}, true);
  } else if (props["dbname"] == "LakeVillaBankingQuerylvl0") {
    return new LHTransactionsBankingDB(config_path, {true, false, false}, LHTransactionsBankingDB::Layout::Query);
  } else if (props["dbname"] == "LakeVillaBankingTablelvl0") {
    return new LHTransactionsBankingDB(config_path, {true, false, false}, LHTransactionsBankingDB::Layout::SeparateTables);
  } else if (props["dbname"] == "LakeVillaBankingQuerylvl1") {
    return new LHTransactionsBankingDB(config_path, {true, true, false}, LHTransactionsBankingDB::Layout::Query);
  }else if (props["dbname"] == "LakeVillaBankingTablelvl1") {
    return new LHTransactionsBankingDB(config_path, {true, true, false}, LHTransactionsBankingDB::Layout::MultiTable);
  } else if (props["dbname"] == "LakeVillaBankingQuerylvl2") {
    return new LHTransactionsBankingDB(config_path, {true, true, true}, LHTransactionsBankingDB::Layout::Query);
  }else if (props["dbname"] == "LakeVillaBankingTablelvl2") {
    return new LHTransactionsBankingDB(config_path, {true, true, true}, LHTransactionsBankingDB::Layout::MultiTable, /*init_global_log=*/true);
  } else if (props["dbname"] == "LakeVillalvl0") {
    // single levels (used by run_bench.sh)
    return new LHTransactionsDB(config_path, {true, false, false}, false);
  } else if (props["dbname"] == "LakeVillalvl1") {
    return new LHTransactionsDB(config_path, {false, true, false}, false);
  } else if (props["dbname"] == "LakeVillalvl2") {
    return new LHTransactionsDB(config_path, {false, false, true}, false);
  }else if (props["dbname"] == "LakeVillaDL") {
    return new LHTransactionsDB(config_path, {false, false, false}, false);
  } else return NULL;
}

