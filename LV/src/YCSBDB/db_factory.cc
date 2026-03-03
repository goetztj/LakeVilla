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
#include "lhtransactionsLvl0.hpp"
#include "lhtransactionsLvl1.hpp"
#include "lhtransactionsLvl2.hpp"
#include "lhtransactionsDL.hpp"
#include "lhtransactionsBankingTableLvl0.hpp"
#include "lhtransactionsBankingTableLvl1.hpp"
#include "lhtransactionsBankingQueryLvl0.hpp"
#include "lhtransactionsBankingQueryLvl1.hpp"
#include "lhtransactionsBankingTableLvl2.hpp"
#include "lhtransactionsBankingQueryLvl2.hpp"

using namespace std;
using ycsbc::DB;
using ycsbc::DBFactory;

DB* DBFactory::CreateDB(utils::Properties &props) {
  std::string config_path = props["config"];
  if (props["dbname"] == "basic") {
    return new BasicDB;
  } else if (props["dbname"] == "LakeVillalvl0") {
    return new LHTransactionsDBLvl0(config_path);
  } else if (props["dbname"] == "LakeVillalvl1") {
    return new LHTransactionsDBLvl1(config_path);
  } else if (props["dbname"] == "LakeVillalvl2") {
    return new LHTransactionsDBLvl2(config_path);
  } else if (props["dbname"] == "LakeVillaBankingQuerylvl0") {
    return new LHTransactionsBankingQueryDBLvl0(config_path);
  } else if (props["dbname"] == "LakeVillaBankingTablelvl0") {
    return new LHTransactionsBankingTableDBLvl0(config_path);
  } else if (props["dbname"] == "LakeVillaBankingQuerylvl1") {
    return new LHTransactionsBankingQueryDBLvl1(config_path);
  }else if (props["dbname"] == "LakeVillaBankingTablelvl1") {
    return new LHTransactionsBankingTableDBLvl1(config_path);
  } else if (props["dbname"] == "LakeVillaBankingQuerylvl2") {
    return new LHTransactionsBankingQueryDBLvl2(config_path);
  }else if (props["dbname"] == "LakeVillaBankingTablelvl2") {
    return new LHTransactionsBankingTableDBLvl2(config_path);
  }else if (props["dbname"] == "LakeVillaDL") {
    return new LHTransactionsDBDL(config_path);
  } else return NULL;
}

