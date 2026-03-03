//
//  ycsbc.cc
//  YCSB-C
//
//  Created by Jinglei Ren on 12/19/14.
//  Copyright (c) 2014 Jinglei Ren <jinglei@ren.systems>.
//
// adapted for the LakeVilla Prototype by Tobias Götz

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

using namespace std;

void UsageMessage(const char* command);
bool StrStartWith(const char* str, const char* pre);
string ParseCommandLine(int argc, const char* argv[], utils::Properties& props);

int DelegateClient(ycsbc::DB* db, ycsbc::CoreWorkload* wl, const int num_ops,
                   bool is_loading) {
  db->Init();
  ycsbc::Client client(*db, *wl);
  int oks = 0;
  for (int i = 0; i < num_ops; ++i) {
    if (is_loading) {
      oks += client.DoInsert();
    } else {
      oks += client.DoTransaction();
    }
  }
  db->Close();
  return oks;
}

int main(const int argc, const char* argv[]) {
  Aws::SDKOptions options;
  Aws::InitAPI(options);
  {
    utils::Properties props;

    string file_name = ParseCommandLine(argc, argv, props);

    if (props.GetProperty("config", "error").compare("error") == 0) {
      std::cerr << "no config file provided!" << std::endl;
      return 1;
    }

    ycsbc::CoreWorkload wl;
    wl.Init(props);

    const int num_threads = stoi(props.GetProperty("threadcount", "1"));

    // Loads data
    vector<future<int>> actual_ops;
    vector<ycsbc::DB*> dbs;
    int total_ops = stoi(props[ycsbc::CoreWorkload::RECORD_COUNT_PROPERTY]);
    for (int i = 0; i < num_threads; ++i) {
      ycsbc::DB* db = ycsbc::DBFactory::CreateDB(props);
      if (!db) {
        cout << "Unknown database name " << props["dbname"] << endl;
        exit(0);
      }
      dbs.push_back(db);
      actual_ops.emplace_back(async(launch::async, DelegateClient, db, &wl,
                                    total_ops / num_threads, true));
    }
    assert((int)actual_ops.size() == num_threads);

    int sum = 0;
    for (auto& n : actual_ops) {
      assert(n.valid());
      sum += n.get();
    }
    cerr << "# Loading records:\t" << sum << endl;

    // Peforms transactions
    actual_ops.clear();
    total_ops = stoi(props[ycsbc::CoreWorkload::OPERATION_COUNT_PROPERTY]);
    utils::Timer<double> timer;
    timer.Start();
    for (int i = 0; i < num_threads; ++i) {
      actual_ops.emplace_back(async(launch::async, DelegateClient, dbs[i], &wl,
                                    total_ops / num_threads, false));
    }
    assert((int)actual_ops.size() == num_threads);

    sum = 0;
    for (auto& n : actual_ops) {
      assert(n.valid());
      sum += n.get();
    }
    double duration = timer.End();
    cerr << "# Transaction throughput (KTPS)" << endl;
    cerr << props["dbname"] << '\t' << file_name << '\t' << num_threads << '\t';
    cerr << total_ops / duration / 1000 << endl;
  }
  Aws::ShutdownAPI(options);
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
