//
//  db.h
//  YCSB-C
//
//  Created by Jinglei Ren on 12/18/14.
//  Copyright (c) 2014 Jinglei Ren <jinglei@ren.systems>.
//
// adapted for the LakeVilla Prototype by Tobias Götz

#ifndef YCSB_C_DB_FACTORY_H_
#define YCSB_C_DB_FACTORY_H_

#include "YCSB-C/core/db.h"
#include "YCSB-C/core/properties.h"
#include "Settings/LvSettings.hpp"

namespace ycsbc {

class DBFactory {
 public:
  static DB* CreateDB(utils::Properties &props);
};

} // ycsbc

#endif // YCSB_C_DB_FACTORY_H_

