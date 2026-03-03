#include "TPCDSClient.hpp"

LHTPCDS::TPCDSClient::TPCDSClient(LHTPCDS::TpcdsPaths& paths,
                                  StorageConnector::MinIOConfig config,
                                  uint32_t threads) {
  this->paths = paths;
  this->config = std::move(config);
  this->connector =
      std::make_unique<StorageConnector::MinIOConnector>(&(this->config));
  this->threads = threads;
}

bool LHTPCDS::TPCDSClient::executeAll(std::vector<bool>& levels,
                                      uint32_t runs) {
  bool success = true;

  std::cout << "------ Q9 ------" << std::endl;

  std::vector<double> times9;
  times9.reserve(runs);
  for (uint32_t i = 0; i < runs; i++) {
    auto start = std::chrono::high_resolution_clock::now();
    auto tmp = q9(levels);
    auto end = std::chrono::high_resolution_clock::now();
    success = success ? tmp : success;

    if (!tmp) {
      std::cerr << "error in Q9" << std::endl;
    }

    std::chrono::duration<double> duration = end - start;

    double time = duration.count();

    times9.push_back(time);

    std::cout << i << ": " << time << std::endl;
  }

  double max = times9[0];
  double min = times9[0];
  double avg = 0;
  for (auto& t : times9) {
    if (t > max) {
      max = t;
    }

    if (t < min) {
      min = t;
    }

    avg += (t / (1.0 * runs));
  }

  std::cout << "min: " << min << "s" << std::endl;
  std::cout << "max: " << max << "s" << std::endl;
  std::cout << "avg: " << avg << "s" << std::endl;
  std::cout << "first: " << times9[0] << "s" << std::endl;

  std::cout << "----------------" << std::endl;

  std::cout << "------ Q67 ------" << std::endl;

  std::vector<double> times67;
  times67.reserve(runs);
  for (uint32_t i = 0; i < runs; i++) {
    auto start = std::chrono::high_resolution_clock::now();
    auto tmp = q67(levels);
    auto end = std::chrono::high_resolution_clock::now();
    success = success ? tmp : success;

    if (!tmp) {
      std::cerr << "error in Q67" << std::endl;
    }

    std::chrono::duration<double> duration = end - start;

    double time = duration.count();
    times67.push_back(time);

    std::cout << i << ": " << time << std::endl;
  }

  max = times67[0];
  min = times67[0];
  avg = 0;
  for (auto& t : times67) {
    if (t > max) {
      max = t;
    }

    if (t < min) {
      min = t;
    }

    avg += (t / (1.0 * runs));
  }

  std::cout << "min: " << min << "s" << std::endl;
  std::cout << "max: " << max << "s" << std::endl;
  std::cout << "avg: " << avg << "s" << std::endl;
  std::cout << "first: " << times67[0] << "s" << std::endl;

  std::cout << "----------------" << std::endl;

  std::cout << "------ Q68 ------" << std::endl;

  std::vector<double> times68;
  times68.reserve(runs);
  for (uint32_t i = 0; i < runs; i++) {
    auto start = std::chrono::high_resolution_clock::now();
    auto tmp = q68(levels);
    auto end = std::chrono::high_resolution_clock::now();
    success = success ? tmp : success;

    if (!tmp) {
      std::cerr << "error in Q68" << std::endl;
    }

    std::chrono::duration<double> duration = end - start;

    double time = duration.count();

    times68.push_back(time);

    std::cout << i << ": " << time << std::endl;
  }

  max = times68[0];
  min = times68[0];
  avg = 0;
  for (auto& t : times68) {
    if (t > max) {
      max = t;
    }

    if (t < min) {
      min = t;
    }

    avg += (t / (1.0 * runs));
  }

  std::cout << "min: " << min << "s" << std::endl;
  std::cout << "max: " << max << "s" << std::endl;
  std::cout << "avg: " << avg << "s" << std::endl;
  std::cout << "first: " << times68[0] << "s" << std::endl;

  std::cout << "----------------" << std::endl;

  std::cout << "------ Q90 ------" << std::endl;

  std::vector<double> times90;
  times90.reserve(runs);
  for (uint32_t i = 0; i < runs; i++) {
    auto start = std::chrono::high_resolution_clock::now();
    auto tmp = q90(levels);
    auto end = std::chrono::high_resolution_clock::now();
    success = success ? tmp : success;

    if (!tmp) {
      std::cerr << "error in Q90" << std::endl;
    }

    std::chrono::duration<double> duration = end - start;
    double time = duration.count();

    times90.push_back(time);

    std::cout << i << ": " << time << std::endl;
  }

  max = times90[0];
  min = times90[0];
  avg = 0;
  for (auto& t : times90) {
    if (t > max) {
      max = t;
    }

    if (t < min) {
      min = t;
    }

    avg += (t / (1.0 * runs));
  }

  std::cout << "min: " << min << "s" << std::endl;
  std::cout << "max: " << max << "s" << std::endl;
  std::cout << "avg: " << avg << "s" << std::endl;
  std::cout << "first: " << times90[0] << "s" << std::endl;

  std::cout << "----------------" << std::endl;

  return success;
}

bool LHTPCDS::TPCDSClient::q9(std::vector<bool>& levels) {
  /*select case
            when (select count(*)
                  from store_sales
                  where ss_quantity between 1 and 20) > 157344
                then (select avg(ss_ext_discount_amt)
                      from store_sales
                      where ss_quantity between 1 and 20)
            else (select avg(ss_net_profit)
                  from store_sales
                  where ss_quantity between 1 and 20) end   bucket1,
        case
            when (select count(*)
                  from store_sales
                  where ss_quantity between 21 and 40) > 34708
                then (select avg(ss_ext_discount_amt)
                      from store_sales
                      where ss_quantity between 21 and 40)
            else (select avg(ss_net_profit)
                  from store_sales
                  where ss_quantity between 21 and 40) end  bucket2,
        case
            when (select count(*)
                  from store_sales
                  where ss_quantity between 41 and 60) > 253327
                then (select avg(ss_ext_discount_amt)
                      from store_sales
                      where ss_quantity between 41 and 60)
            else (select avg(ss_net_profit)
                  from store_sales
                  where ss_quantity between 41 and 60) end  bucket3,
        case
            when (select count(*)
                  from store_sales
                  where ss_quantity between 61 and 80) > 272224
                then (select avg(ss_ext_discount_amt)
                      from store_sales
                      where ss_quantity between 61 and 80)
            else (select avg(ss_net_profit)
                  from store_sales
                  where ss_quantity between 61 and 80) end  bucket4,
        case
            when (select count(*)
                  from store_sales
                  where ss_quantity between 81 and 100) > 428113
                then (select avg(ss_ext_discount_amt)
                      from store_sales
                      where ss_quantity between 81 and 100)
            else (select avg(ss_net_profit)
                  from store_sales
                  where ss_quantity between 81 and 100) end bucket5
 from reason
 where r_reason_sk = 1
 ;

 */
  auto txnmanager = LHTransactions::TransactionManagerGeneric(
      levels, paths.store_sales, this->connector.get(), 9);

  txnmanager.begin_transaction_ycsb();

  txnmanager.open_new_table(paths.reason);

  auto store_sales_id = txnmanager.get_table_id(paths.store_sales);
  auto reason_id = txnmanager.get_table_id(paths.reason);

  std::string tmp = "";
  txnmanager.read_partial_table_simple(store_sales_id, 2, this->threads);
  txnmanager.read_random_file_simple(reason_id, tmp);

  return txnmanager.commit(true);
}

bool LHTPCDS::TPCDSClient::q67(std::vector<bool>& levels) {
  /*select *
from (select i_category
         , i_class
         , i_brand
         , i_product_name
         , d_year
         , d_qoy
         , d_moy
         , s_store_id
         , sumsales
         , rank() over (partition by i_category order by sumsales desc) rk
    from (select i_category
               , i_class
               , i_brand
               , i_product_name
               , d_year
               , d_qoy
               , d_moy
               , s_store_id
               , sum(coalesce(ss_sales_price * ss_quantity, 0)) sumsales
          from store_sales
             , date_dim
             , store
             , item
          where ss_sold_date_sk = d_date_sk
            and ss_item_sk = i_item_sk
            and ss_store_sk = s_store_sk
            and d_month_seq between 1178 and 1178 + 11
          group by rollup (i_category, i_class, i_brand, i_product_name, d_year,
d_qoy, d_moy, s_store_id)) dw1) dw2 where rk <= 100 order by i_category ,
i_class , i_brand , i_product_name , d_year , d_qoy , d_moy , s_store_id ,
sumsales , rk limit 100;
*/

  /*store_sales
                 , date_dim
                 , store
                 , item*/
  auto txnmanager = LHTransactions::TransactionManagerGeneric(
      levels, paths.store_sales, this->connector.get(), 9);

  txnmanager.begin_transaction_ycsb();

  txnmanager.open_new_table(paths.date_dim);
  txnmanager.open_new_table(paths.store);
  txnmanager.open_new_table(paths.item);

  auto store_sales_id = txnmanager.get_table_id(paths.store_sales);
  auto date_dim_id = txnmanager.get_table_id(paths.date_dim);
  auto store_id = txnmanager.get_table_id(paths.store);
  auto item_id = txnmanager.get_table_id(paths.item);

  txnmanager.read_partial_table_simple(store_sales_id, 1, this->threads);
  txnmanager.read_table_simple(date_dim_id, this->threads);
  txnmanager.read_table_simple(store_id, this->threads);
  txnmanager.read_table_simple(item_id, this->threads);

  return txnmanager.commit(true);
}

bool LHTPCDS::TPCDSClient::q68(std::vector<bool>& levels) {
  /*
  select c_last_name
   , c_first_name
   , ca_city
   , bought_city
   , ss_ticket_number
   , extended_price
   , extended_tax
   , list_price
from (select ss_ticket_number
         , ss_customer_sk
         , ca_city                 bought_city
         , sum(ss_ext_sales_price) extended_price
         , sum(ss_ext_list_price)  list_price
         , sum(ss_ext_tax)         extended_tax
    from store_sales
       , date_dim
       , store
       , household_demographics
       , customer_address
    where store_sales.ss_sold_date_sk = date_dim.d_date_sk
      and store_sales.ss_store_sk = store.s_store_sk
      and store_sales.ss_hdemo_sk = household_demographics.hd_demo_sk
      and store_sales.ss_addr_sk = customer_address.ca_address_sk
      and date_dim.d_dom between 1 and 2
      and (household_demographics.hd_dep_count = 6 or
           household_demographics.hd_vehicle_count = 2)
      and date_dim.d_year in (2000, 2000 + 1, 2000 + 2)
      and store.s_city in ('Pleasant Hill', 'Oak Grove')
    group by ss_ticket_number
           , ss_customer_sk
           , ss_addr_sk, ca_city) dn
 , customer
 , customer_address current_addr
where ss_customer_sk = c_customer_sk
and customer.c_current_addr_sk = current_addr.ca_address_sk
and current_addr.ca_city <> bought_city
order by c_last_name
     , ss_ticket_number
limit 100;
  */

  /*store_sales
        , date_dim
        , store
        , household_demographics
        , customer_address*/

  auto txnmanager = LHTransactions::TransactionManagerGeneric(
      levels, paths.store_sales, this->connector.get(), 9);

  txnmanager.begin_transaction_ycsb();

  txnmanager.open_new_table(paths.date_dim);
  txnmanager.open_new_table(paths.store);
  txnmanager.open_new_table(paths.household_demographics);
  txnmanager.open_new_table(paths.customer_address);

  auto store_sales_id = txnmanager.get_table_id(paths.store_sales);
  auto date_dim_id = txnmanager.get_table_id(paths.date_dim);
  auto store_id = txnmanager.get_table_id(paths.store);
  auto household_demographics_id =
      txnmanager.get_table_id(paths.household_demographics);
  auto customer_address_id = txnmanager.get_table_id(paths.customer_address);

  txnmanager.head_table_simple(store_sales_id, this->threads);
  txnmanager.read_table_simple(date_dim_id, this->threads);
  std::string tmp;
  txnmanager.read_random_file_simple(store_id, tmp);
  for (int i = 0; i < 20; i++) {
    txnmanager.read_random_file_simple(store_sales_id, tmp);
  }

  txnmanager.read_table_simple(household_demographics_id, this->threads);

  txnmanager.read_table_simple(customer_address_id, this->threads);

  return txnmanager.commit(true);
}

bool LHTPCDS::TPCDSClient::q90(std::vector<bool>& levels) {
  /*
  select cast(amc as decimal(15, 4)) / cast(pmc as decimal(15, 4)) am_pm_ratio
from (select count(*) amc
    from web_sales,
         household_demographics,
         time_dim,
         web_page
    where ws_sold_time_sk = time_dim.t_time_sk
      and ws_ship_hdemo_sk = household_demographics.hd_demo_sk
      and ws_web_page_sk = web_page.wp_web_page_sk
      and time_dim.t_hour between 10 and 10 + 1
      and household_demographics.hd_dep_count = 7
      and web_page.wp_char_count between 5000 and 5200) at,
   (select count(*) pmc
    from web_sales,
         household_demographics,
         time_dim,
         web_page
    where ws_sold_time_sk = time_dim.t_time_sk
      and ws_ship_hdemo_sk = household_demographics.hd_demo_sk
      and ws_web_page_sk = web_page.wp_web_page_sk
      and time_dim.t_hour between 20 and 20 + 1
      and household_demographics.hd_dep_count = 7
      and web_page.wp_char_count between 5000 and 5200) pt
order by am_pm_ratio
limit 100;
  */

  /*
  web_sales,
          household_demographics,
          time_dim,
          web_page

  */

  auto txnmanager = LHTransactions::TransactionManagerGeneric(
      levels, paths.web_sales, this->connector.get(), 9);

  txnmanager.begin_transaction_ycsb();

  txnmanager.open_new_table(paths.time_dim);
  txnmanager.open_new_table(paths.web_page);
  txnmanager.open_new_table(paths.household_demographics);

  auto web_sales_id = txnmanager.get_table_id(paths.web_sales);
  auto time_dim_id = txnmanager.get_table_id(paths.time_dim);
  auto web_page_id = txnmanager.get_table_id(paths.web_page);
  auto household_demographics_id =
      txnmanager.get_table_id(paths.household_demographics);

  txnmanager.read_table_simple(time_dim_id, this->threads);
  txnmanager.read_table_simple(web_page_id, this->threads);

  txnmanager.read_table_simple(household_demographics_id, this->threads);

  return txnmanager.commit(true);
}
