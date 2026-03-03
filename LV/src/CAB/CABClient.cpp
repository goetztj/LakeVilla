#include "CABClient.hpp"

LHCAB::CABClient::CABClient(std::string& base_path, uint64_t tendant,
                            StorageConnector::MinIOConfig config,
                            uint32_t stream_id,
                            LHCAB::preparedContent* content) {
  this->content = content;
  this->base_path = base_path;
  this->tendant = tendant;
  this->config = std::move(config);
  this->id = stream_id;

  this->connector =
      std::make_unique<StorageConnector::MinIOConnector>(&(this->config));

  std::stringstream s;
  s << CAB_READ_PATH << "query_stream_" << id << ".json";
  this->query_file = s.str();

  std::cerr << s.str() << std::endl;

  this->generateTablePaths(base_path, tendant);
}

void LHCAB::CABClient::generateTablePaths(std::string& base_path,
                                          uint64_t tendant) {
  std::stringstream base_path_stream;
  base_path_stream << base_path << "t" << tendant << ".db/";

  std::stringstream customer;
  customer << base_path_stream.str() << "customer/";
  this->customer_path = customer.str();

  std::stringstream lineitem;
  lineitem << base_path_stream.str() << "lineitem/";
  this->lineitem_path = lineitem.str();

  std::stringstream nation;
  nation << base_path_stream.str() << "nation/";
  this->nation_path = nation.str();

  std::stringstream orders;
  orders << base_path_stream.str() << "orders/";
  this->orders_path = orders.str();

  std::stringstream part;
  part << base_path_stream.str() << "part/";
  this->part_path = part.str();

  std::stringstream partsupp;
  partsupp << base_path_stream.str() << "partsupp/";
  this->partsupp_path = partsupp.str();

  std::stringstream region;
  region << base_path_stream.str() << "region/";
  this->region_path = region.str();

  std::stringstream supplier;
  supplier << base_path_stream.str() << "supplier/";
  this->supplier_path = supplier.str();
}

LHCAB::preparedContent::preparedContent(std::string& base_path) {
  std::ifstream sample_file;
  sample_file.open(base_path + "customer.parquet");

  if (!sample_file.is_open()) {
    std::cout << "Error opening sample customer" << std::endl;
  }

  std::string customer_str((std::istreambuf_iterator<char>(sample_file)),
                           std::istreambuf_iterator<char>());

  if (customer_str.empty()) {
    std::cerr << "customer error" << std::endl;
  }

  this->customer = LHHelpers::encodeAsParquet_str(
      LHHelpers::readParquetAsTable(customer_str.c_str(), customer_str.size()));

  sample_file.close();
  // -------------------------

  sample_file.open(base_path + "lineitem.parquet");

  if (!sample_file.is_open()) {
    std::cout << "Error opening sample lineitem" << std::endl;
  }

  std::string lineitem_str((std::istreambuf_iterator<char>(sample_file)),
                           std::istreambuf_iterator<char>());

  if (lineitem_str.empty()) {
    std::cerr << "lineitem error" << std::endl;
  }

  this->lineitem = LHHelpers::encodeAsParquet_str(
      LHHelpers::readParquetAsTable(lineitem_str.c_str(), lineitem_str.size()));

  sample_file.close();

  // -------------------------

  sample_file.open(base_path + "nation.parquet");

  if (!sample_file.is_open()) {
    std::cout << "Error opening sample nation" << std::endl;
  }

  std::string nation_str((std::istreambuf_iterator<char>(sample_file)),
                         std::istreambuf_iterator<char>());

  if (nation_str.empty()) {
    std::cerr << "nation error" << std::endl;
  }

  this->nation = LHHelpers::encodeAsParquet_str(
      LHHelpers::readParquetAsTable(nation_str.c_str(), nation_str.size()));

  sample_file.close();

  // -------------------------

  sample_file.open(base_path + "orders.parquet");

  if (!sample_file.is_open()) {
    std::cout << "Error opening sample orders" << std::endl;
  }

  std::string orders_str((std::istreambuf_iterator<char>(sample_file)),
                         std::istreambuf_iterator<char>());

  if (orders_str.empty()) {
    std::cerr << "orders error" << std::endl;
  }

  this->orders = LHHelpers::encodeAsParquet_str(
      LHHelpers::readParquetAsTable(orders_str.c_str(), orders_str.size()));

  sample_file.close();

  // -------------------------

  sample_file.open(base_path + "part.parquet");

  if (!sample_file.is_open()) {
    std::cout << "Error opening sample part" << std::endl;
  }

  std::string part_str((std::istreambuf_iterator<char>(sample_file)),
                       std::istreambuf_iterator<char>());

  if (part_str.empty()) {
    std::cerr << "part error" << std::endl;
  }

  this->part = LHHelpers::encodeAsParquet_str(
      LHHelpers::readParquetAsTable(part_str.c_str(), part_str.size()));

  sample_file.close();

  // -------------------------

  sample_file.open(base_path + "partsupp.parquet");

  if (!sample_file.is_open()) {
    std::cout << "Error opening sample partsupp" << std::endl;
  }

  std::string partsupp_str((std::istreambuf_iterator<char>(sample_file)),
                           std::istreambuf_iterator<char>());

  if (partsupp_str.empty()) {
    std::cerr << "partsupp error" << std::endl;
  }

  this->partsupp = LHHelpers::encodeAsParquet_str(
      LHHelpers::readParquetAsTable(partsupp_str.c_str(), partsupp_str.size()));

  sample_file.close();

  // -------------------------

  sample_file.open(base_path + "region.parquet");

  if (!sample_file.is_open()) {
    std::cout << "Error opening sample region" << std::endl;
  }

  std::string region_str((std::istreambuf_iterator<char>(sample_file)),
                         std::istreambuf_iterator<char>());

  if (region_str.empty()) {
    std::cerr << "region error" << std::endl;
  }

  this->region = LHHelpers::encodeAsParquet_str(
      LHHelpers::readParquetAsTable(region_str.c_str(), region_str.size()));

  sample_file.close();

  // -------------------------

  sample_file.open(base_path + "supplier.parquet");

  if (!sample_file.is_open()) {
    std::cout << "Error opening sample supplier" << std::endl;
  }

  std::string supplier_str((std::istreambuf_iterator<char>(sample_file)),
                           std::istreambuf_iterator<char>());

  if (supplier_str.empty()) {
    std::cerr << "supplier error" << std::endl;
  }

  this->supplier = LHHelpers::encodeAsParquet_str(
      LHHelpers::readParquetAsTable(supplier_str.c_str(), supplier_str.size()));

  sample_file.close();
}

bool LHCAB::CABClient::start_query(int id) {
  return start_query(id, {false, false, true});
}

bool LHCAB::CABClient::start_query(int id, std::vector<bool> levels) {
  this->level_config = levels;
  try {
    switch (id) {
      case 1:
        return this->q1();
        break;
      case 2:
        return this->q2();
        break;
      case 3:
        return this->q3();
        break;
      case 4:
        return this->q4();
        break;
      case 5:
        return this->q5();
        break;
      case 6:
        return this->q6();
        break;
      case 7:
        return this->q7();
        break;
      case 8:
        return this->q8();
        break;
      case 9:
        return this->q9();
        break;
      case 10:
        return this->q10();
        break;
      case 11:
        return this->q11();
        break;
      case 12:
        return this->q12();
        break;
      case 13:
        return this->q13();
        break;
      case 14:
        return this->q14();
        break;
      case 15:
        return this->q15();
        break;
      case 16:
        return this->q16();
        break;
      case 17:
        return this->q17();
        break;
      case 18:
        return this->q18();
        break;
      case 19:
        return this->q19();
        break;
      case 20:
        return this->q20();
        break;
      case 21:
        return this->q21();
        break;
      case 22:
        return this->q22();
        break;
      case 23:
        return this->q23();
        break;

      default:
        break;
    }
  } catch (...) {
    std::cerr << "error in execute query" << std::endl;
    return false;
  }

  return false;
}

// analytical
/*select
    l_returnflag,
    l_linestatus,
    sum(l_quantity) as sum_qty,
    sum(l_extendedprice) as sum_base_price,
    sum(l_extendedprice * (1 - l_discount)) as sum_disc_price,
    sum(l_extendedprice * (1 - l_discount) * (1 + l_tax)) as sum_charge,
    avg(l_quantity) as avg_qty,
    avg(l_extendedprice) as avg_price,
    avg(l_discount) as avg_disc,
    count(*) as count_order
from
    :lineitem
where
    l_shipdate <= '1998-12-01'::date - :1::int
group by
    l_returnflag,
    l_linestatus
order by
    l_returnflag,
    l_linestatus;*/
bool LHCAB::CABClient::q1() {
  auto txnmanager = LHTransactions::TransactionManagerGeneric(
      this->level_config, this->lineitem_path, this->connector.get(), this->id);

  while (true) {
    bool started = false;
    while (!started) {
      started = txnmanager.begin_transaction_ycsb();
    }

    auto lineitem_id = txnmanager.get_table_id(this->lineitem_path);

    std::string tmp;

    txnmanager.read_table_simple(lineitem_id);

    if (txnmanager.commit(true)) {
      return true;
    }

    txnmanager.abort();
  }
}

bool LHCAB::CABClient::q2() {
  /*select
    s_acctbal,
    s_name,
    n_name,
    p_partkey,
    p_mfgr,
    s_address,
    s_phone,
    s_comment
from
    :part,
    :supplier,
    :partsupp,
    :nation,
    :region
where
        p_partkey = ps_partkey
  and s_suppkey = ps_suppkey
  and p_size = :1
  and p_type like '%' || :2
  and s_nationkey = n_nationkey
  and n_regionkey = r_regionkey
  and r_name = :3
  and ps_supplycost = (
    select
        min(ps_supplycost)
    from
        :partsupp,
        :supplier,
        :nation,
        :region
    where
            p_partkey = ps_partkey
      and s_suppkey = ps_suppkey
      and s_nationkey = n_nationkey
      and n_regionkey = r_regionkey
      and r_name = :3
)
order by
    s_acctbal desc,
    n_name,
    s_name,
    p_partkey
limit 100;*/
  auto txnmanager = LHTransactions::TransactionManagerGeneric(
      this->level_config, this->partsupp_path, this->connector.get(), this->id);

  while (true) {
    bool started = false;
    while (!started) {
      started = txnmanager.begin_transaction_ycsb();
    }
    txnmanager.open_new_table(supplier_path);
    txnmanager.open_new_table(nation_path);
    txnmanager.open_new_table(region_path);

    auto partsupp_id = txnmanager.get_table_id(partsupp_path);
    auto supplier_id = txnmanager.get_table_id(supplier_path);
    auto nation_id = txnmanager.get_table_id(nation_path);
    auto region_id = txnmanager.get_table_id(region_path);

    txnmanager.read_table_simple(partsupp_id);
    txnmanager.read_table_simple(supplier_id);
    txnmanager.read_table_simple(nation_id);
    txnmanager.read_table_simple(region_id);

    txnmanager.open_new_table(part_path);

    std::string part_str;
    auto part_id = txnmanager.get_table_id(part_path);

    txnmanager.read_random_file_simple(part_id, part_str);
    txnmanager.read_table_simple(partsupp_id);
    txnmanager.read_table_simple(supplier_id);
    txnmanager.read_table_simple(nation_id);
    txnmanager.read_table_simple(region_id);

    if (txnmanager.commit(true)) {
      return true;
    }

    txnmanager.abort();
  }
}

bool LHCAB::CABClient::q3() {
  /*select
    l_orderkey,
    sum(l_extendedprice * (1 - l_discount)) as revenue,
    o_orderdate,
    o_shippriority
from
    :customer,
    :orders,
    :lineitem
where
        c_mktsegment = :1
  and c_custkey = o_custkey
  and l_orderkey = o_orderkey
  and o_orderdate < :2::date
  and l_shipdate > :2::date
group by
    l_orderkey,
    o_orderdate,
    o_shippriority
order by
    revenue desc,
    o_orderdate
limit 10;*/
  auto txnmanager = LHTransactions::TransactionManagerGeneric(
      this->level_config, this->customer_path, this->connector.get(), this->id);
  while (true) {
    bool started = false;
    while (!started) {
      started = txnmanager.begin_transaction_ycsb();
    }
    txnmanager.open_new_table(orders_path);
    txnmanager.open_new_table(lineitem_path);

    auto customer_id = txnmanager.get_table_id(customer_path);
    auto orders_id = txnmanager.get_table_id(orders_path);
    auto lineitem_id = txnmanager.get_table_id(lineitem_path);

    std::string tmp = "";
    txnmanager.read_table_simple(customer_id);
    txnmanager.read_table_simple(orders_id);
    txnmanager.read_table_simple(lineitem_id);

    if (txnmanager.commit(true)) {
      return true;
    }

    txnmanager.abort();
  }
}

bool LHCAB::CABClient::q4() {
  /*select
    o_orderpriority,
    count(*) as order_count
from
    :orders
where
        o_orderdate >= :1::date
  and o_orderdate < add_months(:1::date, 3)
  and exists (
        select
            *
        from
            :lineitem
        where
                l_orderkey = o_orderkey
          and l_commitdate < l_receiptdate
    )
group by
    o_orderpriority
order by
    o_orderpriority;*/
  auto txnmanager = LHTransactions::TransactionManagerGeneric(
      this->level_config, this->lineitem_path, this->connector.get(), this->id);
  while (true) {
    bool started = false;
    while (!started) {
      started = txnmanager.begin_transaction_ycsb();
    }
    auto lineitem_id = txnmanager.get_table_id(lineitem_path);
    std::string tmp;
    txnmanager.read_table_simple(lineitem_id);

    txnmanager.open_new_table(orders_path);

    auto orders_id = txnmanager.get_table_id(orders_path);

    txnmanager.read_table_simple(orders_id);

    if (txnmanager.commit(true)) {
      return true;
    }

    txnmanager.abort();
  }
}

bool LHCAB::CABClient::q5() {
  /*select
    n_name,
    sum(l_extendedprice * (1 - l_discount)) as revenue
from
    :customer,
    :orders,
    :lineitem,
    :supplier,
    :nation,
    :region
where
        c_custkey = o_custkey
  and l_orderkey = o_orderkey
  and l_suppkey = s_suppkey
  and c_nationkey = s_nationkey
  and s_nationkey = n_nationkey
  and n_regionkey = r_regionkey
  and r_name = :1
  and o_orderdate >= :2::date
  and o_orderdate < dateadd(year, 1, :2::date)
group by
    n_name
order by
    revenue desc;*/
  auto txnmanager = LHTransactions::TransactionManagerGeneric(
      this->level_config, this->customer_path, this->connector.get(), this->id);
  while (true) {
    bool started = false;
    while (!started) {
      started = txnmanager.begin_transaction_ycsb();
    }
    txnmanager.open_new_table(orders_path);
    txnmanager.open_new_table(lineitem_path);
    txnmanager.open_new_table(supplier_path);
    txnmanager.open_new_table(nation_path);
    txnmanager.open_new_table(region_path);

    auto customer_id = txnmanager.get_table_id(customer_path);
    auto orders_id = txnmanager.get_table_id(orders_path);
    auto lineitem_id = txnmanager.get_table_id(lineitem_path);
    auto supplier_id = txnmanager.get_table_id(supplier_path);
    auto nation_id = txnmanager.get_table_id(nation_path);
    auto region_id = txnmanager.get_table_id(region_path);

    std::string tmp = "";
    txnmanager.read_table_simple(customer_id);
    txnmanager.read_table_simple(orders_id);
    txnmanager.read_table_simple(lineitem_id);
    txnmanager.read_table_simple(supplier_id);
    txnmanager.read_table_simple(nation_id);
    txnmanager.read_table_simple(region_id);

    if (txnmanager.commit(true)) {
      return true;
    }

    txnmanager.abort();
  }
}

bool LHCAB::CABClient::q6() {
  /*select
    sum(l_extendedprice * l_discount) as revenue
from
    :lineitem
where
        l_shipdate >= :1::date
  and l_shipdate < dateadd(year, 1, :1::date)
  and l_discount between (:2::number(12,2) / 100) - 0.01 and (:2::number(12,2) /
100) + 0.01 and l_quantity < :3;*/
  auto txnmanager = LHTransactions::TransactionManagerGeneric(
      this->level_config, this->lineitem_path, this->connector.get(), this->id);
  while (true) {
    bool started = false;
    while (!started) {
      started = txnmanager.begin_transaction_ycsb();
    }
    auto lineitem_id = txnmanager.get_table_id(lineitem_path);
    std::string tmp;
    txnmanager.read_table_simple(lineitem_id);

    if (txnmanager.commit(true)) {
      return true;
    }

    txnmanager.abort();
  }
}

bool LHCAB::CABClient::q7() {
  /*select
    supp_nation,
    cust_nation,
    l_year,
    sum(volume) as revenue
from
    (
        select
            n1.n_name as supp_nation,
            n2.n_name as cust_nation,
            extract(year from l_shipdate) as l_year,
            l_extendedprice * (1 - l_discount) as volume
        from
            :supplier,
            :lineitem,
            :orders,
            :customer,
            :nation n1,
            :nation n2
        where
                s_suppkey = l_suppkey
          and o_orderkey = l_orderkey
          and c_custkey = o_custkey
          and s_nationkey = n1.n_nationkey
          and c_nationkey = n2.n_nationkey
          and (
                (n1.n_name = :1 and n2.n_name = :2)
                or (n1.n_name = :2 and n2.n_name = :1)
            )
          and l_shipdate between date '1995-01-01' and date '1996-12-31'
    ) as shipping
group by
    supp_nation,
    cust_nation,
    l_year
order by
    supp_nation,
    cust_nation,
    l_year;*/
  auto txnmanager = LHTransactions::TransactionManagerGeneric(
      this->level_config, this->supplier_path, this->connector.get(), this->id);
  while (true) {
    bool started = false;
    while (!started) {
      started = txnmanager.begin_transaction_ycsb();
    }
    txnmanager.open_new_table(lineitem_path);
    txnmanager.open_new_table(orders_path);
    txnmanager.open_new_table(customer_path);
    txnmanager.open_new_table(nation_path);

    auto customer_id = txnmanager.get_table_id(customer_path);
    auto orders_id = txnmanager.get_table_id(orders_path);
    auto lineitem_id = txnmanager.get_table_id(lineitem_path);
    auto supplier_id = txnmanager.get_table_id(supplier_path);
    auto nation_id = txnmanager.get_table_id(nation_path);

    txnmanager.read_table_simple(customer_id);
    txnmanager.read_table_simple(orders_id);
    std::string tmp;
    txnmanager.read_table_simple(lineitem_id);
    txnmanager.read_table_simple(supplier_id);
    txnmanager.read_table_simple(nation_id);
    txnmanager.read_table_simple(nation_id);

    if (txnmanager.commit(true)) {
      return true;
    }

    txnmanager.abort();
  }
}

bool LHCAB::CABClient::q8() {
  /*select
    o_year,
    sum(case
            when nation = :1 then volume
            else 0
        end) / sum(volume) as mkt_share
from
    (
        select
            extract(year from o_orderdate) as o_year,
            l_extendedprice * (1 - l_discount) as volume,
            n2.n_name as nation
        from
            :part,
            :supplier,
            :lineitem,
            :orders,
            :customer,
            :nation n1,
            :nation n2,
            :region
        where
                p_partkey = l_partkey
          and s_suppkey = l_suppkey
          and l_orderkey = o_orderkey
          and o_custkey = c_custkey
          and c_nationkey = n1.n_nationkey
          and n1.n_regionkey = r_regionkey
          and r_name = :2
          and s_nationkey = n2.n_nationkey
          and o_orderdate between date '1995-01-01' and date '1996-12-31'
          and p_type = :3
    ) as all_nations
group by
    o_year
order by
    o_year;*/
  auto txnmanager = LHTransactions::TransactionManagerGeneric(
      this->level_config, this->part_path, this->connector.get(), this->id);
  while (true) {
    bool started = false;
    while (!started) {
      started = txnmanager.begin_transaction_ycsb();
    }

    txnmanager.open_new_table(supplier_path);
    txnmanager.open_new_table(lineitem_path);
    txnmanager.open_new_table(orders_path);
    txnmanager.open_new_table(customer_path);
    txnmanager.open_new_table(nation_path);
    txnmanager.open_new_table(region_path);

    auto part_id = txnmanager.get_table_id(part_path);
    auto customer_id = txnmanager.get_table_id(customer_path);
    auto orders_id = txnmanager.get_table_id(orders_path);
    auto lineitem_id = txnmanager.get_table_id(lineitem_path);
    auto supplier_id = txnmanager.get_table_id(supplier_path);
    auto nation_id = txnmanager.get_table_id(nation_path);
    auto region_id = txnmanager.get_table_id(region_path);

    txnmanager.read_table_simple(part_id);
    txnmanager.read_table_simple(customer_id);
    txnmanager.read_table_simple(orders_id);
    std::string tmp;
    txnmanager.read_table_simple(lineitem_id);
    txnmanager.read_table_simple(supplier_id);
    txnmanager.read_table_simple(nation_id);
    txnmanager.read_table_simple(nation_id);
    txnmanager.read_table_simple(region_id);
    if (txnmanager.commit(true)) {
      return true;
    }

    txnmanager.abort();
  }
}

bool LHCAB::CABClient::q9() {
  /*select
    nation,
    o_year,
    sum(amount) as sum_profit
from
    (
        select
            n_name as nation,
            extract(year from o_orderdate) as o_year,
            l_extendedprice * (1 - l_discount) - ps_supplycost * l_quantity as
amount from :part, :supplier, :lineitem, :partsupp, :orders, :nation where
                s_suppkey = l_suppkey
          and ps_suppkey = l_suppkey
          and ps_partkey = l_partkey
          and p_partkey = l_partkey
          and o_orderkey = l_orderkey
          and s_nationkey = n_nationkey
          and p_name like '%' || :1 || '%'
    ) as profit
group by
    nation,
    o_year
order by
    nation,
    o_year desc;*/
  auto txnmanager = LHTransactions::TransactionManagerGeneric(
      this->level_config, this->part_path, this->connector.get(), this->id);
  while (true) {
    bool started = false;
    while (!started) {
      started = txnmanager.begin_transaction_ycsb();
    }
    txnmanager.open_new_table(supplier_path);
    txnmanager.open_new_table(lineitem_path);
    txnmanager.open_new_table(orders_path);
    txnmanager.open_new_table(partsupp_path);
    txnmanager.open_new_table(nation_path);

    auto part_id = txnmanager.get_table_id(part_path);
    auto partsupp_id = txnmanager.get_table_id(partsupp_path);
    auto orders_id = txnmanager.get_table_id(orders_path);
    auto lineitem_id = txnmanager.get_table_id(lineitem_path);
    auto supplier_id = txnmanager.get_table_id(supplier_path);
    auto nation_id = txnmanager.get_table_id(nation_path);

    txnmanager.read_table_simple(part_id);
    txnmanager.read_table_simple(partsupp_id);
    txnmanager.read_table_simple(orders_id);
    std::string tmp;
    txnmanager.read_table_simple(lineitem_id);
    txnmanager.read_table_simple(supplier_id);
    txnmanager.read_table_simple(nation_id);
    if (txnmanager.commit(true)) {
      return true;
    }

    txnmanager.abort();
  }
}

bool LHCAB::CABClient::q10() {
  /*select
    c_custkey,
    c_name,
    sum(l_extendedprice * (1 - l_discount)) as revenue,
    c_acctbal,
    n_name,
    c_address,
    c_phone,
    c_comment
from
    :customer,
    :orders,
    :lineitem,
    :nation
where
        c_custkey = o_custkey
  and l_orderkey = o_orderkey
  and o_orderdate >= :1::date
  and o_orderdate < add_months(:1::date, 3)
  and l_returnflag = 'R'
  and c_nationkey = n_nationkey
group by
    c_custkey,
    c_name,
    c_acctbal,
    c_phone,
    n_name,
    c_address,
    c_comment
order by
    revenue desc
limit 20;*/

  auto txnmanager = LHTransactions::TransactionManagerGeneric(
      this->level_config, this->customer_path, this->connector.get(), this->id);
  while (true) {
    bool started = false;
    while (!started) {
      started = txnmanager.begin_transaction_ycsb();
    }
    txnmanager.open_new_table(orders_path);
    txnmanager.open_new_table(lineitem_path);
    txnmanager.open_new_table(nation_path);

    auto customer_id = txnmanager.get_table_id(customer_path);
    auto orders_id = txnmanager.get_table_id(orders_path);
    auto lineitem_id = txnmanager.get_table_id(lineitem_path);
    auto nation_id = txnmanager.get_table_id(nation_path);

    txnmanager.read_table_simple(customer_id);
    txnmanager.read_table_simple(orders_id);
    std::string tmp;
    txnmanager.read_table_simple(lineitem_id);
    txnmanager.read_table_simple(nation_id);
    if (txnmanager.commit(true)) {
      return true;
    }

    txnmanager.abort();
  }
}

bool LHCAB::CABClient::q11() {
  /*select
    ps_partkey,
    sum(ps_supplycost * ps_availqty) as "value"
from
    :partsupp,
    :supplier,
    :nation
where
        ps_suppkey = s_suppkey
  and s_nationkey = n_nationkey
  and n_name = :1
group by
    ps_partkey having
        sum(ps_supplycost * ps_availqty) > (
        select
                sum(ps_supplycost * ps_availqty) * (0.0001 / :2)
        from
            :partsupp,
            :supplier,
            :nation
        where
                ps_suppkey = s_suppkey
          and s_nationkey = n_nationkey
          and n_name = :1
    )
order by
    "value" desc;*/
  auto txnmanager = LHTransactions::TransactionManagerGeneric(
      this->level_config, this->partsupp_path, this->connector.get(), this->id);
  while (true) {
    bool started = false;
    while (!started) {
      started = txnmanager.begin_transaction_ycsb();
    }
    txnmanager.open_new_table(supplier_path);
    txnmanager.open_new_table(nation_path);

    auto partsupp_id = txnmanager.get_table_id(partsupp_path);
    auto supplier_id = txnmanager.get_table_id(supplier_path);
    auto nation_id = txnmanager.get_table_id(nation_path);

    txnmanager.read_table_simple(partsupp_id);
    txnmanager.read_table_simple(supplier_id);
    txnmanager.read_table_simple(nation_id);

    txnmanager.read_table_simple(partsupp_id);
    txnmanager.read_table_simple(supplier_id);
    txnmanager.read_table_simple(nation_id);
    if (txnmanager.commit(true)) {
      return true;
    }

    txnmanager.abort();
  }
}

bool LHCAB::CABClient::q12() {
  /*select
    l_shipmode,
    sum(case
            when o_orderpriority = '1-URGENT'
                or o_orderpriority = '2-HIGH'
                then 1
            else 0
        end) as high_line_count,
    sum(case
            when o_orderpriority <> '1-URGENT'
                and o_orderpriority <> '2-HIGH'
                then 1
            else 0
        end) as low_line_count
from
    :orders,
    :lineitem
where
        o_orderkey = l_orderkey
  and l_shipmode in (:1, :2)
  and l_commitdate < l_receiptdate
  and l_shipdate < l_commitdate
  and l_receiptdate >= :3::date
  and l_receiptdate < dateadd(year, 1, :3::date)
group by
    l_shipmode
order by
    l_shipmode;
*/
  auto txnmanager = LHTransactions::TransactionManagerGeneric(
      this->level_config, this->orders_path, this->connector.get(), this->id);
  while (true) {
    bool started = false;
    while (!started) {
      started = txnmanager.begin_transaction_ycsb();
    }
    txnmanager.open_new_table(lineitem_path);

    auto orders_id = txnmanager.get_table_id(orders_path);
    auto lineitem_id = txnmanager.get_table_id(lineitem_path);

    txnmanager.read_table_simple(orders_id);
    std::string tmp;
    txnmanager.read_table_simple(lineitem_id);

    if (txnmanager.commit(true)) {
      return true;
    }

    txnmanager.abort();
  }
}

bool LHCAB::CABClient::q13() {
  /*select
    c_count,
    count(*) as custdist
from
    (
        select
            c_custkey,
            count(o_orderkey) c_count
        from
            :customer left outer join :orders on
                        c_custkey = o_custkey
                    and o_comment not like '%' || :1 || '%' || :2 || '%'
        group by
            c_custkey
    ) as c_orders
group by
    c_count
order by
    custdist desc,
    c_count desc;
*/
  auto txnmanager = LHTransactions::TransactionManagerGeneric(
      this->level_config, this->customer_path, this->connector.get(), this->id);
  while (true) {
    bool started = false;
    while (!started) {
      started = txnmanager.begin_transaction_ycsb();
    }
    txnmanager.open_new_table(orders_path);

    auto customer_id = txnmanager.get_table_id(customer_path);
    auto orders_id = txnmanager.get_table_id(orders_path);

    txnmanager.read_table_simple(customer_id);
    txnmanager.read_table_simple(orders_id);
    if (txnmanager.commit(true)) {
      return true;
    }

    txnmanager.abort();
  }
}

bool LHCAB::CABClient::q14() {
  /*select
            100.00 * sum(case
                             when p_type like 'PROMO%'
                                 then l_extendedprice * (1 - l_discount)
                             else 0
            end) / sum(l_extendedprice * (1 - l_discount)) as promo_revenue
from
    :lineitem,
    :part
where
        l_partkey = p_partkey
  and l_shipdate >= :1::date
  and l_shipdate < add_months(:1::date, 1);*/
  auto txnmanager = LHTransactions::TransactionManagerGeneric(
      this->level_config, this->lineitem_path, this->connector.get(), this->id);
  while (true) {
    bool started = false;
    while (!started) {
      started = txnmanager.begin_transaction_ycsb();
    }
    txnmanager.open_new_table(part_path);

    auto lineitem_id = txnmanager.get_table_id(lineitem_path);
    auto part_id = txnmanager.get_table_id(part_path);
    std::string tmp;
    txnmanager.read_table_simple(lineitem_id);
    txnmanager.read_table_simple(part_id);
    if (txnmanager.commit(true)) {
      return true;
    }

    txnmanager.abort();
  }
}

bool LHCAB::CABClient::q15() {
  /*with revenue as (
    select
        l_suppkey as supplier_no,
        sum(l_extendedprice * (1 - l_discount)) as total_revenue
    from
        :lineitem
    where
            l_shipdate >= :1::date
      and l_shipdate < add_months(:1::date, 3)
    group by
        l_suppkey)
select
    s_suppkey,
    s_name,
    s_address,
    s_phone,
    total_revenue
from
    :supplier,
    revenue
where
        s_suppkey = supplier_no
  and total_revenue = (
    select
        max(total_revenue)
    from
        revenue
)
order by
    s_suppkey;*/
  auto txnmanager = LHTransactions::TransactionManagerGeneric(
      this->level_config, this->lineitem_path, this->connector.get(), this->id);
  while (true) {
    bool started = false;
    while (!started) {
      started = txnmanager.begin_transaction_ycsb();
    }
    auto lineitem_id = txnmanager.get_table_id(lineitem_path);
    std::string tmp;
    txnmanager.read_table_simple(lineitem_id);

    txnmanager.open_new_table(supplier_path);
    auto supplier_id = txnmanager.get_table_id(supplier_path);

    txnmanager.read_table_simple(supplier_id);

    if (txnmanager.commit(true)) {
      return true;
    }

    txnmanager.abort();
  }
}

bool LHCAB::CABClient::q16() {
  /*select
    p_brand,
    p_type,
    p_size,
    count(distinct ps_suppkey) as supplier_cnt
from
    :partsupp,
    :part
where
        p_partkey = ps_partkey
  and p_brand <> :1
  and p_type not like :2 || '%'
  and p_size in (:3, :4, :5, :6, :7, :8, :9, :10)
  and ps_suppkey not in (
    select
        s_suppkey
    from
        :supplier
    where
            s_comment like '%Customer%Complaints%'
)
group by
    p_brand,
    p_type,
    p_size
order by
    supplier_cnt desc,
    p_brand,
    p_type,
    p_size;*/
  auto txnmanager = LHTransactions::TransactionManagerGeneric(
      this->level_config, this->supplier_path, this->connector.get(), this->id);
  while (true) {
    bool started = false;
    while (!started) {
      started = txnmanager.begin_transaction_ycsb();
    }
    auto supplier_id = txnmanager.get_table_id(supplier_path);

    txnmanager.read_table_simple(supplier_id);

    txnmanager.open_new_table(part_path);
    txnmanager.open_new_table(partsupp_path);
    auto part_id = txnmanager.get_table_id(part_path);
    auto partsupp_id = txnmanager.get_table_id(partsupp_path);

    txnmanager.read_table_simple(part_id);
    txnmanager.read_table_simple(partsupp_id);
    if (txnmanager.commit(true)) {
      return true;
    }

    txnmanager.abort();
  }
}

bool LHCAB::CABClient::q17() {
  /*select
        sum(l_extendedprice) / 7.0 as avg_yearly
from
    :lineitem,
    :part
where
        p_partkey = l_partkey
  and p_brand = :1
  and p_container = :2
  and l_quantity < (
    select
            0.2 * avg(l_quantity)
    from
        :lineitem
    where
            l_partkey = p_partkey
);*/
  auto txnmanager = LHTransactions::TransactionManagerGeneric(
      this->level_config, this->lineitem_path, this->connector.get(), this->id);
  while (true) {
    bool started = false;
    while (!started) {
      started = txnmanager.begin_transaction_ycsb();
    }
    auto lineitem_id = txnmanager.get_table_id(lineitem_path);
    std::string tmp;
    txnmanager.read_table_simple(lineitem_id);

    txnmanager.open_new_table(part_path);

    auto part_id = txnmanager.get_table_id(part_path);

    txnmanager.read_table_simple(part_id);
    txnmanager.read_table_simple(lineitem_id);
    if (txnmanager.commit(true)) {
      return true;
    }

    txnmanager.abort();
  }
}

bool LHCAB::CABClient::q18() {
  /*select
    c_name,
    c_custkey,
    o_orderkey,
    o_orderdate,
    o_totalprice,
    sum(l_quantity)
from
    :customer,
    :orders,
    :lineitem
where
        o_orderkey in (
        select
            l_orderkey
        from
            :lineitem
        group by
            l_orderkey having
                sum(l_quantity) > :1
    )
  and c_custkey = o_custkey
  and o_orderkey = l_orderkey
group by
    c_name,
    c_custkey,
    o_orderkey,
    o_orderdate,
    o_totalprice
order by
    o_totalprice desc,
    o_orderdate
limit 100;*/
  auto txnmanager = LHTransactions::TransactionManagerGeneric(
      this->level_config, this->lineitem_path, this->connector.get(), this->id);
  while (true) {
    bool started = false;
    while (!started) {
      started = txnmanager.begin_transaction_ycsb();
    }
    auto lineitem_id = txnmanager.get_table_id(lineitem_path);
    std::string tmp;
    txnmanager.read_table_simple(lineitem_id);

    txnmanager.open_new_table(customer_path);
    txnmanager.open_new_table(orders_path);

    auto customer_id = txnmanager.get_table_id(customer_path);
    auto orders_id = txnmanager.get_table_id(orders_path);

    txnmanager.read_table_simple(customer_id);
    txnmanager.read_table_simple(orders_id);
    txnmanager.read_table_simple(lineitem_id);
    if (txnmanager.commit(true)) {
      return true;
    }

    txnmanager.abort();
  }
}

bool LHCAB::CABClient::q19() {
  /*select
    sum(l_extendedprice* (1 - l_discount)) as revenue
from
    :lineitem,
    :part
where
    (
                p_partkey = l_partkey
            and p_brand = :1
            and p_container in ('SM CASE', 'SM BOX', 'SM PACK', 'SM PKG')
            and l_quantity >= :4 and l_quantity <= :4 + 10
            and p_size between 1 and 5
            and l_shipmode in ('AIR', 'AIR REG')
            and l_shipinstruct = 'DELIVER IN PERSON'
        )
   or
    (
                p_partkey = l_partkey
            and p_brand = :2
            and p_container in ('MED BAG', 'MED BOX', 'MED PKG', 'MED PACK')
            and l_quantity >= :5 and l_quantity <= :5 + 10
            and p_size between 1 and 10
            and l_shipmode in ('AIR', 'AIR REG')
            and l_shipinstruct = 'DELIVER IN PERSON'
        )
   or
    (
                p_partkey = l_partkey
            and p_brand = :3
            and p_container in ('LG CASE', 'LG BOX', 'LG PACK', 'LG PKG')
            and l_quantity >= :6 and l_quantity <= :6 + 10
            and p_size between 1 and 15
            and l_shipmode in ('AIR', 'AIR REG')
            and l_shipinstruct = 'DELIVER IN PERSON'
        );*/
  auto txnmanager = LHTransactions::TransactionManagerGeneric(
      this->level_config, this->lineitem_path, this->connector.get(), this->id);
  while (true) {
    bool started = false;
    while (!started) {
      started = txnmanager.begin_transaction_ycsb();
    }
    txnmanager.open_new_table(part_path);

    auto lineitem_id = txnmanager.get_table_id(lineitem_path);
    auto part_id = txnmanager.get_table_id(part_path);

    txnmanager.read_table_simple(part_id);
    std::string tmp;
    txnmanager.read_table_simple(lineitem_id);
    if (txnmanager.commit(true)) {
      return true;
    }

    txnmanager.abort();
  }
}

bool LHCAB::CABClient::q20() {
  /*select
    s_name,
    s_address
from
    :supplier,
    :nation
where
        s_suppkey in (
        select
            ps_suppkey
        from
            :partsupp
        where
                ps_partkey in (
                select
                    p_partkey
                from
                    :part
                where
                        p_name like :1 || '%'
            )
          and ps_availqty > (
            select
                    0.5 * sum(l_quantity)
            from
                :lineitem
            where
                    l_partkey = ps_partkey
              and l_suppkey = ps_suppkey
              and l_shipdate >= :2::date
              and l_shipdate < dateadd(year, 1, :2::date)
        )
    )
  and s_nationkey = n_nationkey
  and n_name = :3
order by
    s_name;*/
  auto txnmanager = LHTransactions::TransactionManagerGeneric(
      this->level_config, this->lineitem_path, this->connector.get(), this->id);
  while (true) {
    bool started = false;
    while (!started) {
      started = txnmanager.begin_transaction_ycsb();
    }
    auto lineitem_id = txnmanager.get_table_id(lineitem_path);
    std::string tmp;
    txnmanager.read_table_simple(lineitem_id);

    txnmanager.open_new_table(part_path);
    auto part_id = txnmanager.get_table_id(part_path);

    txnmanager.read_table_simple(part_id);

    txnmanager.open_new_table(partsupp_path);
    auto partsupp_id = txnmanager.get_table_id(partsupp_path);

    txnmanager.read_table_simple(partsupp_id);

    txnmanager.open_new_table(supplier_path);
    txnmanager.open_new_table(nation_path);

    auto supplier_id = txnmanager.get_table_id(supplier_path);
    auto nation_id = txnmanager.get_table_id(nation_path);

    txnmanager.read_table_simple(supplier_id);
    txnmanager.read_table_simple(nation_id);

    if (txnmanager.commit(true)) {
      return true;
    }

    txnmanager.abort();
  }
}

bool LHCAB::CABClient::q21() {
  /*select
    s_name,
    count(*) as numwait
from
    :supplier,
    :lineitem l1,
    :orders,
    :nation
where
        s_suppkey = l1.l_suppkey
  and o_orderkey = l1.l_orderkey
  and o_orderstatus = 'F'
  and l1.l_receiptdate > l1.l_commitdate
  and exists (
        select
            *
        from
            :lineitem l2
        where
                l2.l_orderkey = l1.l_orderkey
          and l2.l_suppkey <> l1.l_suppkey
    )
  and not exists (
        select
            *
        from
            :lineitem l3
        where
                l3.l_orderkey = l1.l_orderkey
          and l3.l_suppkey <> l1.l_suppkey
          and l3.l_receiptdate > l3.l_commitdate
    )
  and s_nationkey = n_nationkey
  and n_name = :1
group by
    s_name
order by
    numwait desc,
    s_name
limit 100;*/
  auto txnmanager = LHTransactions::TransactionManagerGeneric(
      this->level_config, this->lineitem_path, this->connector.get(), this->id);

  while (true) {
    bool started = false;
    while (!started) {
      started = txnmanager.begin_transaction_ycsb();
    }

    auto lineitem_id = txnmanager.get_table_id(lineitem_path);
    std::string tmp;
    txnmanager.read_table_simple(lineitem_id);
    txnmanager.read_table_simple(lineitem_id);

    txnmanager.open_new_table(supplier_path);
    txnmanager.open_new_table(orders_path);
    txnmanager.open_new_table(nation_path);

    auto supplier_id = txnmanager.get_table_id(supplier_path);
    auto orders_id = txnmanager.get_table_id(orders_path);
    auto nation_id = txnmanager.get_table_id(nation_path);

    txnmanager.read_table_simple(lineitem_id);
    txnmanager.read_table_simple(supplier_id);
    txnmanager.read_table_simple(orders_id);
    txnmanager.read_table_simple(nation_id);
    if (txnmanager.commit(true)) {
      return true;
    }

    txnmanager.abort();
  }
}

bool LHCAB::CABClient::q22() {
  /*select
    cntrycode,
    count(*) as numcust,
    sum(c_acctbal) as totacctbal
from
    (
        select
            substring(c_phone, 1, 2) as cntrycode,
            c_acctbal
        from
            :customer
        where
                substring(c_phone, 1, 2) in
                (:1, :2, :3, :4, :5, :6, :7)
          and c_acctbal > (
            select
                avg(c_acctbal)
            from
                :customer
            where
                    c_acctbal > 0.00
              and substring(c_phone, 1, 2) in
                  (:1, :2, :3, :4, :5, :6, :7)
        )
          and not exists (
                select
                    *
                from
                    :orders
                where
                        o_custkey = c_custkey
            )
    ) as custsale
group by
    cntrycode
order by
    cntrycode;*/
  auto txnmanager = LHTransactions::TransactionManagerGeneric(
      this->level_config, this->orders_path, this->connector.get(), this->id);
  while (true) {
    bool started = false;
    while (!started) {
      started = txnmanager.begin_transaction_ycsb();
    }

    auto orders_id = txnmanager.get_table_id(orders_path);

    txnmanager.read_table_simple(orders_id);

    txnmanager.open_new_table(customer_path);
    auto customer_id = txnmanager.get_table_id(customer_path);

    txnmanager.read_table_simple(customer_id);
    txnmanager.read_table_simple(customer_id);

    if (txnmanager.commit(true)) {
      return true;
    }

    txnmanager.abort();
  }
}

bool LHCAB::CABClient::q23() {
  /*begin;

:split:

insert into :orders (
    select o_orderkey + 8,
           o_custkey,
           o_orderstatus,
           (select sum(L_QUANTITY * P_RETAILPRICE * (1+L_TAX) * (1-L_DISCOUNT))
from :lineitem, :part where l_orderkey = o_orderkey and P_PARTKEY = L_PARTKEY),
o_orderdate, o_orderpriority, o_clerk, o_shippriority, o_comment from :orders
    where :1 <= o_orderkey and o_orderkey < :2
);

:split:

delete from :orders where :1 <= o_orderkey and o_orderkey < :2 and
mod(o_orderkey, 32) between :3 and :4;

:split:

commit;*/
  auto txnmanager = LHTransactions::TransactionManagerGeneric(
      {true, true, true}, this->lineitem_path, this->connector.get(), this->id);
  bool started = false;
  while (!started) {
    started = txnmanager.begin_transaction_ycsb();
  }

  txnmanager.open_new_table(this->part_path);

  auto lineitem_id = txnmanager.get_table_id(lineitem_path);
  auto part_id = txnmanager.get_table_id(part_path);
  std::string tmp;
  txnmanager.read_table_simple(lineitem_id);
  auto part = txnmanager.read_table(part_id);

  txnmanager.open_new_table(this->orders_path);
  auto orders_id = txnmanager.get_table_id(orders_path);

  std::string file;
  auto order_tbl = txnmanager.read_random_file_as_table(orders_id, file);
  bool commit = true;
  if (order_tbl) {
    commit = txnmanager.remove_file(orders_id, file);

    if (commit) {
      commit = txnmanager.add_file(orders_id, file, true);
    }
  } else {
    commit = false;
  }

  if (commit) {
    return txnmanager.commit(true);
  } else {
    return txnmanager.abort();
  }
}

bool LHCAB::CABClient::iRow(LHTransactions::TransactionManagerGeneric* manager,
                            std::string& table, std::string& content) {
  if (!manager) {
    return false;
  }
  auto tbl_id = manager->get_table_id(table);
  std::string key = "key";
  return manager->add_file(tbl_id, content, key);
}

bool LHCAB::CABClient::iCustomer() {
  auto txnmanager = LHTransactions::TransactionManagerGeneric(
      this->level_config, this->customer_path, this->connector.get(), this->id);
  bool started = false;
  while (!started) {
    started = txnmanager.begin_transaction_ycsb();
  }
  auto tbl_id = txnmanager.get_table_id(this->customer_path);
  std::string key = "key";
  txnmanager.add_file(tbl_id, this->content->customer, key);
  return txnmanager.commit() ? true : txnmanager.abort();
}

bool LHCAB::CABClient::iLineitem() {
  auto txnmanager = LHTransactions::TransactionManagerGeneric(
      this->level_config, this->lineitem_path, this->connector.get(), this->id);
  bool started = false;
  while (!started) {
    started = txnmanager.begin_transaction_ycsb();
  }
  auto tbl_id = txnmanager.get_table_id(this->lineitem_path);
  std::string key = "key";
  txnmanager.add_file(tbl_id, this->content->lineitem, key);
  return txnmanager.commit() ? true : txnmanager.abort();
}

bool LHCAB::CABClient::iNation() {
  auto txnmanager = LHTransactions::TransactionManagerGeneric(
      this->level_config, this->nation_path, this->connector.get(), this->id);
  bool started = false;
  while (!started) {
    started = txnmanager.begin_transaction_ycsb();
  }
  auto tbl_id = txnmanager.get_table_id(this->nation_path);
  std::string key = "key";
  txnmanager.add_file(tbl_id, this->content->nation, key);
  return txnmanager.commit() ? true : txnmanager.abort();
}

bool LHCAB::CABClient::iOrders() {
  auto txnmanager = LHTransactions::TransactionManagerGeneric(
      this->level_config, this->orders_path, this->connector.get(), this->id);
  bool started = false;
  while (!started) {
    started = txnmanager.begin_transaction_ycsb();
  }
  auto tbl_id = txnmanager.get_table_id(this->orders_path);
  std::string key = "key";
  txnmanager.add_file(tbl_id, this->content->orders, key);
  return txnmanager.commit() ? true : txnmanager.abort();
}

bool LHCAB::CABClient::iPart() {
  auto txnmanager = LHTransactions::TransactionManagerGeneric(
      this->level_config, this->part_path, this->connector.get(), this->id);
  bool started = false;
  while (!started) {
    started = txnmanager.begin_transaction_ycsb();
  }
  auto tbl_id = txnmanager.get_table_id(this->part_path);
  std::string key = "key";
  txnmanager.add_file(tbl_id, this->content->part, key);
  return txnmanager.commit() ? true : txnmanager.abort();
}

bool LHCAB::CABClient::iPartsupp() {
  auto txnmanager = LHTransactions::TransactionManagerGeneric(
      this->level_config, this->partsupp_path, this->connector.get(), this->id);
  bool started = false;
  while (!started) {
    started = txnmanager.begin_transaction_ycsb();
  }
  auto tbl_id = txnmanager.get_table_id(this->partsupp_path);

  std::string key = "key";
  txnmanager.add_file(tbl_id, this->content->partsupp, key);

  return txnmanager.commit() ? true : txnmanager.abort();
}

bool LHCAB::CABClient::iRegion() {
  auto txnmanager = LHTransactions::TransactionManagerGeneric(
      this->level_config, this->region_path, this->connector.get(), this->id);
  bool started = false;
  while (!started) {
    started = txnmanager.begin_transaction_ycsb();
  }
  auto tbl_id = txnmanager.get_table_id(this->region_path);

  std::string key = "key";
  txnmanager.add_file(tbl_id, this->content->region, key);

  return txnmanager.commit() ? true : txnmanager.abort();
}

bool LHCAB::CABClient::iSupplier() {
  auto txnmanager = LHTransactions::TransactionManagerGeneric(
      this->level_config, this->supplier_path, this->connector.get(), this->id);
  bool started = false;
  while (!started) {
    started = txnmanager.begin_transaction_ycsb();
  }
  auto tbl_id = txnmanager.get_table_id(this->supplier_path);

  std::string key = "key";
  txnmanager.add_file(tbl_id, this->content->supplier, key);
  return txnmanager.commit() ? true : txnmanager.abort();
}

bool LHCAB::CABClient::uTable(std::string& tbl_path) {
  auto txnmanager = LHTransactions::TransactionManagerGeneric(
      this->level_config, tbl_path, this->connector.get(), this->id);
  bool started = false;
  while (!started) {
    started = txnmanager.begin_transaction_ycsb();
  }
  auto tbl_id = txnmanager.get_table_id(tbl_path);
  std::string path;
  txnmanager.read_random_file_simple(tbl_id, path);
  try {
    if (!path.empty()) {
      txnmanager.remove_file(tbl_id, path);
      txnmanager.add_file(tbl_id, path, true);
    }
  } catch (...) {
    std::cerr << "insert failed" << std::endl;
    txnmanager.abort();
    return false;
  }
  return txnmanager.commit();
}

bool LHCAB::CABClient::cleanup() {
  auto txnmanager = LHTransactions::TransactionManagerGeneric(
      {true, true, false}, this->partsupp_path, this->connector.get(),
      this->id);
  bool started = false;
  while (!started) {
    started = txnmanager.begin_transaction_ycsb();
  }

  txnmanager.open_new_table(this->customer_path);
  txnmanager.open_new_table(this->lineitem_path);
  txnmanager.open_new_table(this->nation_path);
  txnmanager.open_new_table(this->orders_path);
  txnmanager.open_new_table(this->part_path);
  txnmanager.open_new_table(this->region_path);
  txnmanager.open_new_table(this->supplier_path);

  return txnmanager.commit(this->read_mb_count, this->write_mb_count);
}