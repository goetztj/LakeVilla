//
//  ycsbc.cc
//  YCSB-C
//
//  Created by Jinglei Ren on 12/19/14.
//  Copyright (c) 2014 Jinglei Ren <jinglei@ren.systems>.
//
// adapted for the LHTransactions Prototype by Tobias Götz

#include <aws/core/Aws.h>
#include <aws/core/auth/AWSCredentialsProviderChain.h>
#include <aws/s3/S3Client.h>

#include <cstring>
#include <future>
#include <iostream>
#include <string>
#include <vector>

#include "Settings/LvSettings.hpp"
#include "YCSB-C/core/client.h"
#include "YCSB-C/core/core_workload.h"
#include "YCSB-C/core/timer.h"
#include "YCSB-C/core/utils.h"
#include "YCSBDB/db_factory.h"
#include "YCSBDB/stats_db.hpp"

using namespace std;

void UsageMessage(const char* command);
bool StrStartWith(const char* str, const char* pre);
string ParseCommandLine(int argc, const char* argv[], utils::Properties& props);

// executes num_ops operations; Init() has to be called before
int DelegateClient(ycsbc::DB* db, ycsbc::CoreWorkload* wl, const int num_ops,
                   bool is_loading, uint64_t id) {
  ycsbc::Client client(*db, *wl, id);
  int oks = 0;
  for (int i = 0; i < num_ops; ++i) {
    if (is_loading) {
      oks += client.DoInsert();
    } else {
      oks += client.DoTransaction();
    }
  }
  // commits single-transaction runs; statistics are printed later
  db->Close();
  return oks;
}

// operations of thread i when total_ops are split across num_threads
int OpsOfThread(int total_ops, int num_threads, int i) {
  return total_ops / num_threads + (i < total_ops % num_threads ? 1 : 0);
}

void PrintStats(vector<ycsbc::DB*>& dbs) {
  for (auto db : dbs) {
    if (auto stats = dynamic_cast<ycsbc::StatsDB*>(db)) {
      stats->print_stats();
    }
  }
}

int main(const int argc, const char* argv[]) {
  Aws::SDKOptions options;
  Aws::InitAPI(options);
  int exit_code = 0;
  {
    utils::Properties props;
    string file_name = ParseCommandLine(argc, argv, props);

    if (props.GetProperty("config", "error").compare("error") == 0) {
      std::cerr << "no config file provided!" << std::endl;
      Aws::ShutdownAPI(options);
      return 1;
    }

    ycsbc::CoreWorkload wl;
    wl.Init(props);

    const int num_threads = stoi(props.GetProperty("threadcount", "1"));

    vector<future<int>> actual_ops;
    vector<ycsbc::DB*> dbs;
    int total_ops = stoi(props[ycsbc::CoreWorkload::RECORD_COUNT_PROPERTY]);
    int sum = 0;

    for (int i = 0; i < num_threads; ++i) {
      ycsbc::DB* db = ycsbc::DBFactory::CreateDB(props);
      if (!db) {
        cerr << "Unknown database name " << props["dbname"] << endl;
        for (auto created : dbs) {
          delete created;
        }
        Aws::ShutdownAPI(options);
        return 1;
      }
      dbs.push_back(db);
      if (auto stats = dynamic_cast<ycsbc::StatsDB*>(db)) {
        stats->defer_stats = true;
      }
    }

    // Loads data
    if (props.GetProperty("load", "true") == "true") {
      for (auto db : dbs) {
        db->Init();
      }
      for (int i = 0; i < num_threads; ++i) {
        actual_ops.emplace_back(async(launch::async, DelegateClient, dbs[i],
                                      &wl, OpsOfThread(total_ops, num_threads, i),
                                      true, i));
      }
      for (auto& n : actual_ops) {
        sum += n.get();
      }
      cerr << "# Loading records:\t" << sum << endl;
      PrintStats(dbs);
    }

    // Peforms transactions
    if (props.GetProperty("run", "true") == "true") {
      actual_ops.clear();
      total_ops = stoi(props[ycsbc::CoreWorkload::OPERATION_COUNT_PROPERTY]);

      // connection setup is not part of the measurement
      for (auto db : dbs) {
        db->Init();
      }

      utils::Timer<double> timer;
      timer.Start();
      for (int i = 0; i < num_threads; ++i) {
        actual_ops.emplace_back(async(launch::async, DelegateClient, dbs[i],
                                      &wl, OpsOfThread(total_ops, num_threads, i),
                                      false, i));
      }

      sum = 0;
      for (auto& n : actual_ops) {
        sum += n.get();
      }
      double duration = timer.End();
      cerr << "# Transaction throughput (KTPS)" << endl;
      cerr << props["dbname"] << '\t' << file_name << '\t' << num_threads
           << '\t';
      cerr << total_ops / duration / 1000 << endl;
      cerr << "# Successful operations:\t" << sum << "/" << total_ops << endl;
      PrintStats(dbs);
    } else {
      cout << "skipping run" << endl;
    }

    for (auto db : dbs) {
      delete db;
    }
  }
  Aws::ShutdownAPI(options);
  return exit_code;
}

string ParseCommandLine(int argc, const char* argv[],
                        utils::Properties& props) {
  int argindex = 1;
  string filename;
  while (argindex < argc && StrStartWith(argv[argindex], "-")) {
    if (strcmp(argv[argindex], "-threads") == 0) {
      argindex++;
      if (argindex >= argc) {
        UsageMessage(argv[0]);
        exit(0);
      }
      props.SetProperty("threadcount", argv[argindex]);
      argindex++;
    } else if (strcmp(argv[argindex], "-db") == 0) {
      argindex++;
      if (argindex >= argc) {
        UsageMessage(argv[0]);
        exit(0);
      }
      props.SetProperty("dbname", argv[argindex]);
      argindex++;
    } else if (strcmp(argv[argindex], "-host") == 0) {
      argindex++;
      if (argindex >= argc) {
        UsageMessage(argv[0]);
        exit(0);
      }
      props.SetProperty("host", argv[argindex]);
      argindex++;
    } else if (strcmp(argv[argindex], "-port") == 0) {
      argindex++;
      if (argindex >= argc) {
        UsageMessage(argv[0]);
        exit(0);
      }
      props.SetProperty("port", argv[argindex]);
      argindex++;
    } else if (strcmp(argv[argindex], "-slaves") == 0) {
      argindex++;
      if (argindex >= argc) {
        UsageMessage(argv[0]);
        exit(0);
      }
      props.SetProperty("slaves", argv[argindex]);
      argindex++;
    } else if (strcmp(argv[argindex], "-P") == 0) {
      argindex++;
      if (argindex >= argc) {
        UsageMessage(argv[0]);
        exit(0);
      }
      filename.assign(argv[argindex]);
      ifstream input(argv[argindex]);
      try {
        props.Load(input);
      } catch (const string& message) {
        cout << message << endl;
        exit(0);
      }
      input.close();
      argindex++;
    } else if (strcmp(argv[argindex], "-config") == 0) {
      argindex++;
      if (argindex >= argc) {
        UsageMessage(argv[0]);
        exit(0);
      }
      props.SetProperty("config", argv[argindex]);
      argindex++;
    } else if (strcmp(argv[argindex], "-load") == 0) {
      argindex++;
      if (argindex >= argc) {
        UsageMessage(argv[0]);
        exit(0);
      }
      props.SetProperty("load", argv[argindex]);
      argindex++;
    } else if (strcmp(argv[argindex], "-run") == 0) {
      argindex++;
      if (argindex >= argc) {
        UsageMessage(argv[0]);
        exit(0);
      }
      props.SetProperty("run", argv[argindex]);
      argindex++;
    } else {
      cout << "Unknown option '" << argv[argindex] << "'" << endl;
      exit(0);
    }
  }

  if (argindex == 1 || argindex != argc) {
    UsageMessage(argv[0]);
    exit(0);
  }

  return filename;
}

void UsageMessage(const char* command) {
  cout << "Usage: " << command << " [options]" << endl;
  cout << "Options:" << endl;
  cout << "  -threads n: execute using n threads (default: 1)" << endl;
  cout << "  -config path: the path to the LakeVilla config file (required!)"
       << endl;
  cout << "  -load bool: if loading shoudl be executed" << endl;
  cout << "  -run bool: only performs run" << endl;
  cout << "  -db dbname: specify the name of the DB to use (default: basic)"
       << endl;
  cout << "  -P propertyfile: load properties from the given file. Multiple "
          "files can"
       << endl;
  cout << "                   be specified, and will be processed in the order "
          "specified"
       << endl;
}

inline bool StrStartWith(const char* str, const char* pre) {
  return strncmp(str, pre, strlen(pre)) == 0;
}
