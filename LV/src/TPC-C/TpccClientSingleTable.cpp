#include "TpccClientSingleTable.hpp"

LHTPC::TpccClientSingleTable::TpccClientSingleTable(
    std::vector<bool> levels, LHTPC::TpccPaths paths,
    StorageConnector::MinIOConfig config2)
    : TpccClient(levels) {
  this->paths = std::move(paths);
  this->config = std::move(config2);
}

bool LHTPC::TpccClientSingleTable::executeOne() {
  std::random_device s;
  std::mt19937 generator{s()};

  std::uniform_int_distribution<> distr{1, 100};

  auto number = distr(generator);

  if (number < 4) {
    return this->stockLevel();
  } else if (number < 9) {
    return this->delivery();
  } else if (number < 13) {
    return this->orderStatus();
  } else if (number < 56) {
    return this->payment();
  } else {
    return this->newOrder();
  }
}

bool LHTPC::TpccClientSingleTable::stockLevel() {
  /*
  int slev()
{
EXEC SQL WHENEVER NOT FOUND GOTO sqlerr;
EXEC SQL WHENEVER SQLERROR GOTO sqlerr;
EXEC SQL SELECT d_next_o_id INTO :o_id
FROM district
WHERE d_w_id=:w_id AND d_id=:d_id;
EXEC SQL SELECT COUNT(DISTINCT (s_i_id)) INTO :stock_count
FROM order_line, stock
WHERE ol_w_id=:w_id AND
ol_d_id=:d_id AND ol_o_id<:o_id AND
ol_o_id>=:o_id-20 AND s_w_id=:w_id AND
s_i_id=ol_i_id AND s_quantity < :threshold;
EXEC SQL COMMIT WORK;
return(0);
sqlerr:
error();
}
  */

  auto txnmanager_district = LHTransactions::TransactionManagerGeneric(
      this->levels, paths.district, this->config, 0);

  txnmanager_district.begin_transaction_ycsb();

  std::string path = "";

  auto district_table = txnmanager_district.read_table(0);

  auto txnmanager_order_line = LHTransactions::TransactionManagerGeneric(
      this->levels, paths.order_line, this->config, 1);

  auto txnmanager_stock = LHTransactions::TransactionManagerGeneric(
      this->levels, paths.stock, this->config, 2);

  txnmanager_order_line.begin_transaction_ycsb();
  txnmanager_stock.begin_transaction_ycsb();

  std::string tmp1, tmp2;
  auto order_line_table =
      txnmanager_order_line.read_random_file_as_table(0, tmp1);
  auto stock_table = txnmanager_stock.read_random_file_as_table(0, tmp2);

  return txnmanager_district.commit(true) &&
         txnmanager_order_line.commit(true) && txnmanager_stock.commit(true);
}

bool LHTPC::TpccClientSingleTable::orderStatus() {
  /*
  int ostat()
{
EXEC SQL WHENEVER NOT FOUND GOTO sqlerr;
EXEC SQL WHENEVER SQLERROR GOTO sqlerr;
if (byname)
{
EXEC SQL SELECT count(c_id) INTO :namecnt
FROM customer
WHERE c_last=:c_last AND c_d_id=:d_id AND c_w_id=:w_id;
EXEC SQL DECLARE c_name CURSOR FOR
SELECT c_balance, c_first, c_middle, c_id
FROM customer
WHERE c_last=:c_last AND c_d_id=:d_id AND c_w_id=:w_id
ORDER BY c_first;
EXEC SQL OPEN c_name;
if (namecnt%2) namecnt++; / / Locate midpoint customer
for (n=0; n<namecnt/ 2; n++)
{
EXEC SQL FETCH c_name
INTO :c_balance, :c_first, :c_middle, :c_id;
}
EXEC SQL CLOSE c_name;
}
else {
EXEC SQL SELECT c_balance, c_first, c_middle, c_last
INTO :c_balance, :c_first, :c_middle, :c_last
FROM customer
WHERE c_id=:c_id AND c_d_id=:d_id AND c_w_id=:w_id;
}
EXEC SQL SELECT o_id, o_carrier_id, o_entry_d
INTO :o_id, :o_carrier_id, :entdate
FROM orders
ORDER BY o_id DESC;

EXEC SQL DECLARE c_line CURSOR FOR
SELECT ol_i_id, ol_supply_w_id, ol_quantity,
ol_amount, ol_delivery_d
FROM order_line
WHERE ol_o_id=:o_id AND ol_d_id=:d_id AND ol_w_id=:w_id;
EXEC SQL OPEN c_line;
EXEC SQL WHENEVER NOT FOUND CONTINUE;

i=0;
while (sql_notfound(FALSE))
{
i++;
EXEC SQL FETCH c_line
INTO :ol_i_id[i], :ol_supply_w_id[i], :ol_quantity[i],
:ol_amount[i], :ol_delivery_d[i];
}
EXEC SQL CLOSE c_line;
EXEC SQL COMMIT WORK;
return(0);
sqlerr:
error();
}
  */

  auto txnmanager_customer = LHTransactions::TransactionManagerGeneric(
      this->levels, paths.customer, this->config, 0);

  txnmanager_customer.begin_transaction_ycsb();

  std::string path = "";

  auto customer_table1 = txnmanager_customer.read_table(0);
  auto customer_table2 = txnmanager_customer.read_table(0);

  auto txnmanager_order = LHTransactions::TransactionManagerGeneric(
      this->levels, paths.order, this->config, 1);

  txnmanager_order.begin_transaction_ycsb();

  auto txnmanager_order_line = LHTransactions::TransactionManagerGeneric(
      this->levels, paths.order_line, this->config, 2);

  txnmanager_order_line.begin_transaction_ycsb();

  auto order_table = txnmanager_order.read_table(0);
  auto order_line_table = txnmanager_order_line.read_table(0);

  return txnmanager_customer.commit(true) && txnmanager_order.commit(true) &&
         txnmanager_order_line.commit(true);
}

bool LHTPC::TpccClientSingleTable::delivery() {
  /*
  int delivery()
{
EXEC SQL WHENEVER SQLERROR GOTO sqlerr;
gettimestamp(datetime);
>> For each district in warehouse <<
printf("W: %d\ n", w_id);
for (d_id=1; d_id<=DIST_PER_WARE; d_id++)
{
EXEC SQL WHENEVER NOT FOUND GOTO sqlerr;
EXEC SQL DECLARE c_no CURSOR FOR
>>SELECT no_o_id
FROM new_order
WHERE no_d_id = :d_id AND no_w_id = :w_id
ORDER BY no_o_id ASC;

EXEC SQL OPEN c_no;
EXEC SQL WHENEVER NOT FOUND continue;
EXEC SQL FETCH c_no INTO :no_o_id;
>>EXEC SQL DELETE FROM new_order WHERE CURRENT OF c_no;

EXEC SQL CLOSE c_no;
>>EXEC SQL SELECT o_c_id INTO :c_id FROM orders
WHERE o_id = :no_o_id AND o_d_id = :d_id AND
o_w_id = :w_id;
>>EXEC SQL UPDATE orders SET o_carrier_id = :o_carrier_id
WHERE o_id = :no_o_id AND o_d_id = :d_id AND
o_w_id = :w_id;
>>EXEC SQL UPDATE order_line SET ol_delivery_d = :datetime
WHERE ol_o_id = :no_o_id AND ol_d_id = :d_id AND
ol_w_id = :w_id;
>>EXEC SQL SELECT SUM(ol_amount) INTO :ol_total
FROM order_line
WHERE ol_o_id = :no_o_id AND ol_d_id = :d_id
AND ol_w_id = :w_id;
>>EXEC SQL UPDATE customer SET c_balance = c_balance + :ol_total
WHERE c_id = :c_id AND c_d_id = :d_id AND
c_w_id = :w_id;

EXEC SQL COMMIT WORK;
printf("D: %d, O: %d, time: %d \ n", d_id, o_id, tad);
}
EXEC SQL COMMIT WORK;
return(0);
sqlerr:
error();
}
  */

  auto txnmanager_new_order = LHTransactions::TransactionManagerGeneric(
      this->levels, paths.new_order, this->config, 0);

  txnmanager_new_order.begin_transaction_ycsb();

  // for (d_id=1; d_id<=DIST_PER_WARE; d_id++)
  std::random_device s;
  std::mt19937 generator{s()};

  std::uniform_int_distribution<> distr{1, 10};

  auto number = distr(generator);

  auto txnmanager_order = LHTransactions::TransactionManagerGeneric(
      this->levels, paths.order, this->config, 1);

  txnmanager_order.begin_transaction_ycsb();

  auto txnmanager_order_line = LHTransactions::TransactionManagerGeneric(
      this->levels, paths.order_line, this->config, 2);

  txnmanager_order_line.begin_transaction_ycsb();

  auto txnmanager_customer = LHTransactions::TransactionManagerGeneric(
      this->levels, paths.customer, this->config, 3);

  txnmanager_customer.begin_transaction_ycsb();

  for (int i = 0; i < number; i++) {
    std::string path = "";

    // SELECT
    auto new_order_table =
        txnmanager_new_order.read_random_file_as_table(0, path);

    if (path.empty()) {
      break;
    }

    // DELETE
    txnmanager_new_order.remove_file(0, path);
    txnmanager_new_order.add_file(0, path, true);

    std::string order_path, order_line_path, customer_path;

    // SELECT
    auto order_table =
        txnmanager_order.read_random_file_as_table(0, order_path);

    if (order_path.empty()) {
      break;
    }

    // UPDATE
    txnmanager_order.remove_file(0, order_path);
    txnmanager_order.add_file(0, order_path, true);

    // UPDATE2
    auto order_line_table =
        txnmanager_order_line.read_random_file_as_table(0, order_line_path);
    txnmanager_order_line.remove_file(0, order_line_path);
    txnmanager_order_line.add_file(0, order_line_path, true);

    // SELECT *
    auto order_line_table2 = txnmanager_order.read_table(0);

    // UPDATE3
    auto customer_table =
        txnmanager_customer.read_random_file_as_table(0, customer_path);
    txnmanager_customer.remove_file(0, customer_path);
    txnmanager_customer.add_file(0, customer_path, true);
  }
  return txnmanager_new_order.commit() && txnmanager_order.commit() &&
         txnmanager_order_line.commit() && txnmanager_customer.commit();
}

bool LHTPC::TpccClientSingleTable::payment() {
  auto txnmanager_warehouse = LHTransactions::TransactionManagerGeneric(
      this->levels, paths.warehouse, this->config, 0);

  txnmanager_warehouse.begin_transaction_ycsb();

  /*
  0 = warehouse
  1 = district
  2 = customer
  3 = history
  */

  std::string path = "";
  // EXEC SQL UPDATE warehouse SET w_ytd = w_ytd + :h_amount WHERE w_id=:w_id;
  auto warehouse_table =
      txnmanager_warehouse.read_random_file_as_table(0, path);
  txnmanager_warehouse.remove_file(0, path);
  txnmanager_warehouse.add_file(0, path, true);

  // EXEC SQL SELECT w_street_1, w_street_2, w_city, w_state, w_zip, w_name
  // INTO :w_street_1, :w_street_2, :w_city, :w_state, :w_zip, :w_name
  // FROM warehouse
  // WHERE w_id=:w_id;
  auto warehouse_table2 =
      txnmanager_warehouse.read_random_file_as_table(0, path);

  auto txnmanager_district = LHTransactions::TransactionManagerGeneric(
      this->levels, paths.district, this->config, 1);

  txnmanager_district.begin_transaction_ycsb();

  // EXEC SQL UPDATE district SET d_ytd = d_ytd + :h_amount
  // WHERE d_w_id=:w_id AND d_id=:d_id;
  std::string district_path = "";
  auto district_table =
      txnmanager_district.read_random_file_as_table(0, district_path);
  txnmanager_district.remove_file(0, district_path);
  txnmanager_district.add_file(0, district_path, true);

  // EXEC SQL SELECT d_street_1, d_street_2, d_city, d_state, d_zip, d_name
  // INTO :d_street_1, :d_street_2, :d_city, :d_state, :d_zip, :d_name
  // FROM district
  // WHERE d_w_id=:w_id AND d_id=:d_id;
  auto district_table2 =
      txnmanager_district.read_random_file_as_table(0, district_path);

  // if (byname)
  std::random_device s;
  std::mt19937 generator{s()};

  std::uniform_int_distribution<> distr{1, 100};

  auto number = distr(generator);

  auto txnmanager_customer = LHTransactions::TransactionManagerGeneric(
      this->levels, paths.customer, this->config, 2);

  txnmanager_customer.begin_transaction_ycsb();

  std::string customer_path;

  if (number < 51) {
    // EXEC SQL SELECT count(c_id) INTO :namecnt
    // FROM customer
    // WHERE c_last=:c_last AND c_d_id=:c_d_id AND c_w_id=:c_w_id;
    auto customer_table =
        txnmanager_customer.read_random_file_as_table(0, customer_path);

    // EXEC SQL DECLARE c_byname CURSOR FOR
    // SELECT c_first, c_middle, c_id,
    // c_street_1, c_street_2, c_city, c_state, c_zip,
    // c_phone, c_credit, c_credit_lim,
    // c_discount, c_balance, c_since
    // FROM customer
    // WHERE c_w_id=:c_w_id AND c_d_id=:c_d_id AND c_last=:c_last
    // ORDER BY c_first;

    auto customer_table2 =
        txnmanager_customer.read_random_file_as_table(0, customer_path);

  } else {
    /*EXEC SQL SELECT c_first, c_middle, c_last,
c_street_1, c_street_2, c_city, c_state, c_zip,
c_phone, c_credit, c_credit_lim,
c_discount, c_balance, c_since
INTO :c_first, :c_middle, :c_last,
:c_street_1, :c_street_2, :c_city, :c_state, :c_zip,
:c_phone, :c_credit, :c_credit_lim,
:c_discount, :c_balance, :c_since
FROM customer
WHERE c_w_id=:c_w_id AND c_d_id=:c_d_id AND c_id=:c_id;*/
    auto customer_table1 =
        txnmanager_customer.read_random_file_as_table(0, customer_path);
  }

  // if (strstr(c_credit, "BC") )
  number = distr(generator);

  if (number < 51) {
    /*EXEC SQL SELECT c_data INTO :c_data
FROM customer
WHERE c_w_id=:c_w_id AND c_d_id=:c_d_id AND c_id=:c_id;*/
    auto customer_table3 =
        txnmanager_customer.read_random_file_as_table(0, customer_path);

    /*EXEC SQL UPDATE customer
  SET c_balance = :c_balance, c_data = :c_new_data
  WHERE c_w_id = :c_w_id AND c_d_id = :c_d_id AND
  c_id = :c_id;*/
    txnmanager_customer.remove_file(0, customer_path);
    txnmanager_customer.add_file(0, customer_path, true);

  } else {
    /*EXEC SQL UPDATE customer SET c_balance = :c_balance
WHERE c_w_id = :c_w_id AND c_d_id = :c_d_id AND
c_id = :c_id;*/
    txnmanager_customer.remove_file(0, customer_path);
    txnmanager_customer.add_file(0, customer_path, true);
  }

  auto txnmanager_history = LHTransactions::TransactionManagerGeneric(
      this->levels, paths.history, this->config, 3);

  txnmanager_history.begin_transaction_ycsb();

  std::string history_path = "";
  /*EXEC SQL INSERT INTO history (h_c_d_id, h_c_w_id, h_c_id, h_d_id,
h_w_id, h_date, h_amount, h_data)
VALUES (:c_d_id, :c_w_id, :c_id, :d_id,
:w_id, :datetime, :h_amount, :h_data);*/
  std::string his_key = "history";
  auto history_table = generateHistory();
  txnmanager_history.add_file(0, history_table, his_key, true);

  return txnmanager_warehouse.commit() && txnmanager_district.commit() &&
         txnmanager_customer.commit() && txnmanager_history.commit();
}

bool LHTPC::TpccClientSingleTable::newOrder() {
  auto txnmanager_customer = LHTransactions::TransactionManagerGeneric(
      this->levels, paths.customer, this->config, 0);

  txnmanager_customer.begin_transaction_ycsb();

  /*
  0 = customer
  1 = warehouse
  2 = district
  3 = order
  4 = new_order
  5 = item
  6 = stock
  7 = order_line
  */

  std::string path, warehouse_path;

  auto txnmanager_warehouse = LHTransactions::TransactionManagerGeneric(
      this->levels, paths.warehouse, this->config, 1);

  txnmanager_warehouse.begin_transaction_ycsb();
  /*EXEC SQL SELECT c_discount, c_last, c_credit, w_tax
INTO :c_discount, :c_last, :c_credit, :w_tax
FROM customer, warehouse
WHERE w_id = :w_id AND c_w_id = w_id AND
c_d_id = :d_id AND c_id = :c_id;*/

  auto customer_table = txnmanager_customer.read_table(0);
  auto warehouse_table = txnmanager_warehouse.read_table(0);

  std::string district_path;
  auto txnmanager_district = LHTransactions::TransactionManagerGeneric(
      this->levels, paths.district, this->config, 2);

  txnmanager_district.begin_transaction_ycsb();
  /*EXEC SQL SELECT d_next_o_id, d_tax INTO :d_next_o_id, :d_tax
FROM district
WHERE d_id = :d_id AND d_w_id = :w_id;*/
  auto district_talbe =
      txnmanager_district.read_random_file_as_table(0, district_path);

  /*EXEC SQL UPDATE district SET d_next_o_id = :d_next_o_id + 1
  WHERE d_id = :d_id AND d_w_id = :w_id;
  o_id=d_next_o_id;*/
  txnmanager_district.remove_file(0, district_path);
  txnmanager_district.add_file(0, district_path, true);

  std::string order_path;
  auto txnmanager_order = LHTransactions::TransactionManagerGeneric(
      this->levels, paths.order, this->config, 3);

  txnmanager_order.begin_transaction_ycsb();
  /*EXEC SQL INSERT INTO ORDERS (o_id, o_d_id, o_w_id, o_c_id,
  o_entry_d, o_ol_cnt, o_all_local)
  VALUES (:o_id, :d_id, :w_id, :c_id,
  :datetime, :o_ol_cnt, :o_all_local);*/
  std::string order_key = "order";
  auto order_table = generateOrder();
  txnmanager_order.add_file(0, order_table, order_key, true);

  std::string new_order_path;
  auto txnmanager_new_order = LHTransactions::TransactionManagerGeneric(
      this->levels, paths.new_order, this->config, 4);

  txnmanager_new_order.begin_transaction_ycsb();

  /*EXEC SQL INSERT INTO NEW_ORDER (no_o_id, no_d_id, no_w_id)
  VALUES (:o_id, :d_id, :w_id);*/
  std::string new_order_key = "new_order";
  auto new_order_table = generateNewOrder();
  txnmanager_new_order.add_file(0, new_order_table, new_order_key, true);

  // for (ol_number=1; ol_number<=o_ol_cnt; ol_number++)
  std::random_device s;
  std::mt19937 generator{s()};

  std::uniform_int_distribution<> distr{1, 10};

  auto number = distr(generator);
  auto txnmanager_item = LHTransactions::TransactionManagerGeneric(
      this->levels, paths.item, this->config, 5);

  txnmanager_item.begin_transaction_ycsb();

  auto txnmanager_stock = LHTransactions::TransactionManagerGeneric(
      this->levels, paths.stock, this->config, 6);

  txnmanager_stock.begin_transaction_ycsb();

  auto txnmanager_order_line = LHTransactions::TransactionManagerGeneric(
      this->levels, paths.order_line, this->config, 7);

  txnmanager_order_line.begin_transaction_ycsb();

  for (int i = 0; i < number; i++) {
    /*EXEC SQL SELECT i_price, i_name , i_data
INTO :i_price, :i_name, :i_data
FROM item
WHERE i_id = :ol_i_id;
price[ol_number-1] = i_price;
strncpy(iname[ol_number-1],i_name,24);*/

    std::string item_path;

    auto item_table = txnmanager_item.read_random_file_as_table(0, item_path);

    /*EXEC SQL SELECT s_quantity, s_data,
  s_dist_01, s_dist_02, s_dist_03, s_dist_04, s_dist_05
  s_dist_06, s_dist_07, s_dist_08, s_dist_09, s_dist_10
  INTO :s_quantity, :s_data,
  :s_dist_01, :s_dist_02, :s_dist_03, :s_dist_04, :s_dist_05
  :s_dist_06, :s_dist_07, :s_dist_08, :s_dist_09, :s_dist_10
  FROM stock
  WHERE s_i_id = :ol_i_id AND s_w_id = :ol_supply_w_id;*/

    std::string stock_path;
    auto stock_table =
        txnmanager_stock.read_random_file_as_table(0, stock_path);

    /*EXEC SQL UPDATE stock SET s_quantity = :s_quantity
    WHERE s_i_id = :ol_i_id
    AND s_w_id = :ol_supply_w_id;
    ol_amount = ol_quantity * i_price * (1+w_tax+d_tax) * (1-c_discount);
    amt[ol_number-1]=ol_amount;
    total += ol_amount;*/
    txnmanager_stock.remove_file(0, stock_path);
    txnmanager_stock.add_file(0, stock_path, true);

    /*EXEC SQL INSERT
  INTO order_line (ol_o_id, ol_d_id, ol_w_id, ol_number,
  ol_i_id, ol_supply_w_id,
  ol_quantity, ol_amount, ol_dist_info)
  VALUES (:o_id, :d_id, :w_id, :ol_number,
  :ol_i_id, :ol_supply_w_id,
  :ol_quantity, :ol_amount, :ol_dist_info);*/
    std::string order_line_key = "order_line";
    auto order_line_table = generateOrderLine();
    txnmanager_order_line.add_file(0, order_line_table, order_line_key, true);
  }

  return txnmanager_customer.commit(true) &&
         txnmanager_warehouse.commit(true) && txnmanager_district.commit() &&
         txnmanager_order.commit() && txnmanager_new_order.commit() &&
         txnmanager_item.commit(true) && txnmanager_stock.commit() &&
         txnmanager_order_line.commit();
}
