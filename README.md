# LakeVilla

This repository contains the artifacts of our LakeVilla (LV) paper.
We made use of [docker](https://docs.docker.com/engine/install/) to automate the deployment and reproducibility.
You will find the following content:
- YCSB-LH: submodule to the adapted YCSB benchmark for Spark (provided as fork of [YCSB](https://github.com/brianfrankcooper/YCSB))
- benchbase: submodule to the adapted Benchbase implementation for DuckLake (provided as fork of [Benchbase](https://github.com/cmu-db/benchbase))
- Hive: all files needed for the hive docker container
- LV: all files for LakeVilla, including binaries and libraries for the container
- Spark: All files for the Spark and Delta Lake container
- Spark-Iceberg: All files for the Spark and Iceberg container

## Usage

We created a fully dockerized replication of our experimental setup. Please remember that the paper used larger setups.

Additionally, always reset the object store to the initial state after a benchmark to guarantee correct execution. The following command will restore the initial state of the object store:

````docker exec -it spark-delta3 spark-sql -f ./reset.sql````

### 1. Inititalization

Run docker-compose:

````docker compose up````

It will create the following containers:

- **lakevilla**: contains all LakeVilla binaries and scripts to execute the experiments
- **spark-delta3**: A single-node spark setup for the Spark+Delta Lake experiments
- **spark-iceberg-hive**: A single-node spark setup for the Spark+Iceberg experiments (currently disabled due to version conflicts).
- **hive**: A HIVE metastore for the catalogs of spark-delta3 and spark-iceberg-hive
- **postgres**: The Postgres backend for the HIVE catalog
- **minio**: A minio object store. 
- **mc**: A minio client container that creates the bucket ''warehouse''. Inspired by (https://iceberg.apache.org/spark-quickstart/#docker-compose)

spark-delta3 will create all tables for Spark+Delta Lake and LakeVilla, while spark-iceberg-hive create the respective iceberg tables. 

Please keep in mind that we do not provide a tpc-h generator. Hence, you must generate and import the data to execute the CAB benchmark. We provide instructions on how to do that with DuckDB down below.

### 2. Execute a LakeVilla benchmark

All LakeVilla benchmarks are executed using the ''lakevilla'' container. For legacy reasons, our drivers still use the old description of the LakeVilla mechanisms:

- Level 0 = Markers and sublogs
- Level 1 = Markers and global conflict detection
- Level 2 = Global Version Log

To activate the three LV variants from the paper, select the following level combinations:

- Level 0 + 1 = LV[Write]
- Level 2 = LV[Read]
- Level 0 + 1 + 2 = LV[Hybrid]

#### YCSB

The container provides the ycsbc-lv executable based on YCSB-C (https://github.com/basicthinker/YCSB-C/tree/master) adapted for LakeVilla. Use the following command to execute a single run of YCSB using LakeVilla level 0:

``docker exec -it lakevilla /bin/sh -c "./ycsbc-lv -config lvconfig.conf -db LakeVillalvl0 -threads 1 -P YCSB-C/workloads/workloada.spec" ``

The invocation is similar to the original YCSB-C program (https://github.com/basicthinker/YCSB-C/tree/master):
- ''-config'' defines the LakeVilla configuration file (see README in LV)
- ''-db'' defines the used LakeVilla version; we provide: LakeVillalvl0, LakeVillalvl1 (LV[Write]), LakeVillalvl2 (LV[Hybrid] and LV[Read]), and LakeVillaDL (LV[DL]).
- ''-threads'' defines the number of concurrent threads to use
- ''P'' defines the workload to execute

The provided workloads are:
- YCSB-C/workloads/workloada.spec
- YCSB-C/workloads/workloadb.spec
- YCSB-C/workloads/workloadc.spec
- YCSB-C/workloads/workloadd.spec
- YCSB-C/workloads/workloade.spec (unsupported due to missing features in LakeVilla)
- YCSB-C/workloads/workloadf.spec
- additional_workloads/workloadcustom1.spec (Read/update/insert ratio: 50/25/25)
- additional_workloads/workloadcustom2.spec (Read/insert ratio: 50/50)


#### Banking

We use an adapted version of YCSB-C to execute our synthetical banking benchmark. An example invocation is:

```docker exec -it lakevilla /bin/sh -c "./ycsbc-lv -config lvconfig.conf -db LakeVillaBankingQuerylvl0 -threads 1 -P additional_workloads/workloadbanking.spec" ```

We provide the following setups:
- LakeVillaBankingQuerylvl0: Multi-Query implementation using LakeVilla [R]
- LakeVillaBankingTablelvl0: Multi-table implementation using LakeVilla [R]
- LakeVillaBankingQuerylvl1: Multi-Query implementation using LakeVilla [R, CT]
- LakeVillaBankingTablelvl1: Multi-table implementation using LakeVilla Levels [R, CT]
- LakeVillaBankingQuerylvl2: Multi-Query implementation using LakeVilla Levels [R, CT, I]
- LakeVillaBankingTablelvl2: Multi-table implementation using LakeVilla Levels [R, CT, I]


#### Freshness benchmarks

Invoke the ''LakeVilla'' executable with the config in the lakevilla container:

``docker exec -it lakevilla /bin/sh -c "./LakeVilla lvconfig.conf"``

A prompt will appear asking for the version you want to execute:

- Freshness benchmark (lvl 0)
- Freshness benchmark (lvl 0) (simplified)
- Freshness benchmark (lvl 1)
- Freshness benchmark (lvl 1) (simplified)
- Freshness benchmark (lvl 2)
- Freshness benchmark (lvl 2) (simplified)

The paper uses the simplified versions. The other options are extended versions of the benchmark.

#### CAB benchmarks

Please be aware that you must generate and import the TPC-H data before executing this benchmark!

##### TPC-H generation using DuckDB

Open DuckDB and execute the following lines for sf 1:

```
INSTALL tpch;

LOAD tpch;

CALL dbgen(sf = 1);

COPY region TO '/<path>/region.parquet' (FORMAT PARQUET);

COPY nation TO '/<path>/nation.parquet' (FORMAT PARQUET); 

COPY supplier TO '/<path>/supplier.parquet' (FORMAT PARQUET); 

COPY customer TO '/<path>/customer.parquet' (FORMAT PARQUET); 

COPY part TO '/<path>/part.parquet' (FORMAT PARQUET); 

COPY partsupp TO '/<path>/partsupp.parquet' (FORMAT PARQUET);

COPY orders TO '/<path>/orders.parquet' (FORMAT PARQUET); 

COPY lineitem TO '/<path>/lineitem.parquet' (FORMAT PARQUET);
```

This will generate the respective parquet files. Afterward, upload the file to the mc container and the object store: 

```
docker cp /<path>/<table>.parquet mc:/

docker exec -it mc /bin/bash

mc put /<table>.parquet minio/data/sf1/
``` 

Afterward, use the spark-delta3 container to import these parquet files as tables for the needed namespaces (t1-t16):

```
docker exec -it spark-delta3 spark-sql

Use spark_catalog.t1;

CREATE TEMPORARY VIEW temp_customer USING parquet OPTIONS (path "s3a://data/customer.parquet");

create table customer using delta as select * from temp_customer;

CREATE TEMPORARY VIEW temp_lineitem USING parquet OPTIONS (path "s3a://data/lineitem.parquet");

create table lineitem using delta as select * from temp_lineitem;

CREATE TEMPORARY VIEW temp_nation USING parquet OPTIONS (path "s3a://data/nation.parquet");

create table nation using delta as select * from temp_nation;

CREATE TEMPORARY VIEW temp_orders USING parquet OPTIONS (path "s3a://data/orders.parquet");

create table orders using delta as select * from temp_orders;

CREATE TEMPORARY VIEW temp_part USING parquet OPTIONS (path "s3a://data/part.parquet");

create table part using delta as select * from temp_part;

CREATE TEMPORARY VIEW temp_partsupp USING parquet OPTIONS (path "s3a://data/partsupp.parquet");

create table partsupp using delta as select * from temp_partsupp;

CREATE TEMPORARY VIEW temp_region USING parquet OPTIONS (path "s3a://data/region.parquet");

create table region using delta as select * from temp_region;

CREATE TEMPORARY VIEW temp_supplier USING parquet OPTIONS (path "s3a://data/supplier.parquet");

create table supplier using delta as select * from temp_supplier;
```

##### CAB sequential

You can execute our sequential streams using the following command:

```
docker exec -it lakevilla /bin/sh -c "./cab-lv <type> lvconfig.conf /LakeVilla/CAB/sample/"
```

We support the following "types":
- "read": read stream
- "write": write stream 

This will execute the sequential read and write streams as shown in our paper with automatic feature switching. It uses the read streams generated by the original CAB benchmark (https://github.com/alexandervanrenen/cab).

##### CAB: concurrent

You can execute our concurrent streams using the following command:

```
docker exec -it lakevilla /bin/sh -c "./cab-lv mixed lvconfig.conf /LakeVilla/CAB/sample/ <num_readers> <num_writers>"
```

"num_readers" and "num_writes" define the number of concurrent read and write LV clients respectively. We only allow numbers larger than 0.

This will execute the concurrent read and write streams as shown in our paper with automatic feature switching. It uses the read streams generated by the original CAB benchmark (https://github.com/alexandervanrenen/cab).


##### CAB: table-scaling

You can execute our table-scaling experiment using:

```
docker exec -it lakevilla /bin/sh -c "./cab-lv table lvconfig.conf /LakeVilla/CAB/sample/ <num_tables> <base_path>"
```

"num_tables" defines the maximal number of tables that should be accessed from a single transaction.

"base_path" is the path all distinct tables share. E.g. "/wh/tpcds1000.db/".

This will execute the table-sacling experiment from our paper. 

#### Hermitage

You can invoke the automatic hermitage check using: 

```
docker exec -it lakevilla /bin/sh -c "./hermitage-lv lvconfig.conf <lvconfig>"
```

We support the following feature combinations for lvconfig:
- "0" = only markers and sublogs
- "1" = only markers and dependency tracking
- "2" = LV[Read]
- "01" = LV[Write]
- "02" = LV[Hybrid] without global dependecy tracking
- "12" = LV[Hybrid] without sublogs
- "012" = LV[Hybrid]

The programm will automatically try to verify the results of all hermitage scenarios. If the comparison was successfull, it will return "sucess". In some cases, Apache Arrow encodes parquet files differently (usally so me adittional arrow wraped around the values). Here, the individual check will fail and print "failure (manual comparison necessary)" and the received and expected arrow parquet structure. A experiment is still sucessfull if both contain the same values.

#### TPC-DS

Similar to the CAB benchmark, users must generate and upload TPC-DS data to the object store. We used [the offical generation tools from TPC](https://www.tpc.org/tpcds/).

Afterward, upload the files to the mc container and the object store: 

```
docker cp /<path>/<table>.parquet mc:/

docker exec -it mc /bin/bash

mc put /<table>.parquet minio/data/tpcds/
``` 

Afterward, use the spark-delta3 container to import these parquet files as tables under the namespace tpcds1000:

```
docker exec -it spark-delta3 spark-sql

Use spark_catalog.tpcds1000;

CREATE TEMPORARY VIEW tmp_call_center2 ( cc_call_center_sk INT, cc_call_center_id STRING, cc_rec_start_date DATE, cc_rec_end_date DATE, cc_closed_date_sk INT, cc_open_date_sk INT, cc_name STRING, cc_class STRING, cc_employees INT, cc_sq_ft INT, cc_hire_date DATE, cc_region STRING, cc_mkt_id INT, cc_mkt_class STRING, cc_mkt_desc STRING, cc_market_manager STRING, cc_division STRING, cc_company STRING, cc_address STRING, cc_city STRING, cc_state STRING, cc_zip STRING, cc_country STRING, cc_gmt_offset DECIMAL(15,2), cc_tax_percentage DECIMAL(15,2) ) USING csv OPTIONS (path "s3a://data/tpcds/call_center.dat", delimiter ',');

create table call_center using delta as select * from tmp_call_center2;


CREATE TEMPORARY VIEW tmp_store_sales ( ss_sold_date_sk INT, ss_sold_time_sk INT, ss_item_sk INT, ss_customer_sk INT, ss_cdemo_sk INT, ss_hdemo_sk INT, ss_addr_sk INT, ss_store_sk INT, ss_promo_sk INT, ss_ticket_number BIGINT, ss_quantity INT, ss_wholesale_cost DECIMAL(7, 2), ss_list_price DECIMAL(7, 2), ss_sales_price DECIMAL(7, 2), ss_ext_discount_amt DECIMAL(7, 2), ss_ext_sales_price DECIMAL(7, 2), ss_ext_wholesale_cost DECIMAL(7, 2), ss_ext_list_price DECIMAL(7, 2), ss_ext_tax DECIMAL(7, 2), ss_coupon_amt DECIMAL(7, 2), ss_net_paid DECIMAL(7, 2), ss_net_paid_inc_tax DECIMAL(7, 2), ss_net_profit DECIMAL(7, 2) ) USING csv OPTIONS (path "s3a://data/tpcds/store_sales.dat", delimiter '|');

create table store_sales using delta as select * from tmp_store_sales;




CREATE TEMPORARY VIEW tmp_reason ( r_reason_sk INT, r_reason_id STRING, r_reason_desc STRING ) USING csv OPTIONS (path "s3a://data/tpcds/reason.dat", delimiter '|');

create table reason using delta as select * from tmp_reason;


CREATE TEMPORARY VIEW tmp_date_dim ( d_date_sk INT, d_date_id STRING, d_date DATE, d_month_seq INT, d_week_seq INT, d_quarter_seq INT, d_year INT, d_dow INT, d_moy INT, d_dom INT, d_qoy INT, d_fy_year INT, d_fy_quarter_seq INT, d_fy_week_seq INT, d_day_name STRING, d_quarter_name STRING, d_holiday STRING, d_weekend STRING, d_store_sales STRING, d_web_sales STRING, d_cty_sales STRING, d_dow_sales STRING, d_dow_web_sales STRING ) USING csv OPTIONS (path "s3a://data/tpcds/date_dim.dat", delimiter '|');

create table date_dim using delta as select * from tmp_date_dim;


CREATE TEMPORARY VIEW tmp_store ( s_store_sk INT, s_store_id STRING, s_address STRING, s_city STRING, s_state STRING, s_zip STRING, s_country STRING ) USING csv OPTIONS (path "s3a://data/tpcds/store.dat", delimiter '|');

create table store using delta as select * from tmp_store;



CREATE TEMPORARY VIEW tmp_item ( i_item_sk INT, i_item_id STRING, i_name STRING, i_category STRING, i_brand STRING, i_color STRING, i_type STRING, i_size STRING, i_unit_price DECIMAL(15,2), i_retail_price DECIMAL(15,2), i_class STRING, i_category_code STRING, i_quantity_on_hand INT, i_quantity_ordered INT, i_quantity_shipped INT, i_quantity_backordered INT, i_quantity_sold INT, i_quantity_returned INT ) USING csv OPTIONS (path "s3a://data/tpcds/item.dat", delimiter '|');

create table item using delta as select * from tmp_item;



CREATE TEMPORARY VIEW tmp_customer ( c_customer_sk INT, c_customer_id STRING, c_current_cdemo_sk INT, c_current_hdemo_sk INT, c_current_addr_sk INT, c_first_shipto_date_sk INT, c_first_sales_date_sk INT, c_salutation STRING, c_first_name STRING, c_last_name STRING, c_preferred_cust_flag STRING, c_birth_day INT, c_birth_month INT, c_birth_year INT, c_birth_country STRING, c_login STRING, c_email_address STRING, c_last_review_date_sk INT ) USING csv OPTIONS (path "s3a://data/tpcds/customer.dat", delimiter '|');

create table customer using delta as select * from tmp_customer;



CREATE TEMPORARY VIEW tmp_customer_address ( ca_address_sk INT, ca_address_id STRING, ca_street_number STRING, ca_street_name STRING, ca_street_type STRING, ca_suite_number STRING, ca_city STRING, ca_county STRING, ca_state STRING, ca_zip STRING, ca_country STRING, ca_gmt_offset DECIMAL(15,2), ca_location_type STRING ) USING csv OPTIONS (path "s3a://data/tpcds/customer_address.dat", delimiter '|');

create table customer_address using delta as select * from tmp_customer_address;



CREATE TEMPORARY VIEW tmp_household_demographics ( hd_demo_sk INT, hd_income_band_sk INT, hd_buy_potential String, hd_dep_count int, hd_vehicle_count INT ) USING csv OPTIONS (path "s3a://data/tpcds/household_demographics.dat", delimiter '|');

create table household_demographics using delta as select * from tmp_household_demographics;



CREATE TEMPORARY VIEW tmp_web_sales ( ws_sold_date_sk INT, ws_sold_time_sk INT, ws_ship_date_sk INT, ws_item_sk INT NOT NULL, ws_bill_customer_sk INT, ws_bill_cdemo_sk INT, ws_bill_hdemo_sk INT, ws_bill_addr_sk INT, ws_ship_customer_sk INT, ws_ship_cdemo_sk INT, ws_ship_hdemo_sk INT, ws_ship_addr_sk INT, ws_web_page_sk INT, ws_web_site_sk INT, ws_ship_mode_sk INT, ws_warehouse_sk INT, ws_promo_sk INT, ws_order_number BIGINT NOT NULL, ws_quantity INT, ws_wholesale_cost DECIMAL(7, 2), ws_list_price DECIMAL(7, 2), ws_sales_price DECIMAL(7, 2), ws_ext_discount_amt DECIMAL(7, 2), ws_ext_sales_price DECIMAL(7, 2), ws_ext_wholesale_cost DECIMAL(7, 2), ws_ext_list_price DECIMAL(7, 2), ws_ext_tax DECIMAL(7, 2), ws_coupon_amt DECIMAL(7, 2), ws_ext_ship_cost DECIMAL(7, 2), ws_net_paid DECIMAL(7, 2), ws_net_paid_inc_tax DECIMAL(7, 2), ws_net_paid_inc_ship DECIMAL(7, 2), ws_net_paid_inc_ship_tax DECIMAL(7, 2), ws_net_profit DECIMAL(7, 2) ) USING csv OPTIONS (path "s3a://data/tpcds/web_sales.dat", delimiter '|');

create table web_sales using delta as select * from tmp_web_sales;



CREATE TEMPORARY VIEW tmp_time_dim ( t_time_sk INT, t_time_id STRING, t_time DATE, t_hour INT, t_minute INT, t_second INT, t_am_pm STRING, t_shift STRING, t_sub_shift STRING, t_meal_time STRING, t_work_shift STRING, t_international_time STRING, t_start_of_day STRING, t_end_of_day STRING ) USING csv OPTIONS (path "s3a://data/tpcds/time_dim.dat", delimiter '|');

create table time_dim using delta as select * from tmp_time_dim;

CREATE TEMPORARY VIEW tmp_web_page ( wp_web_page_sk INT NOT NULL, wp_web_site_sk INT, wp_access_date_sk INT, wp_access_time_sk INT, wp_rec_start_date DATE, wp_rec_end_date DATE, wp_creation_date_sk INT, wp_access_path STRING, wp_char_count INT, wp_link_count INT, wp_image_count INT, wp_max_ad_count INT, wp_theme STRING, wp_image_path STRING, wp_auth_required STRING, wp_pay_required STRING, wp_customer_sk INT) USING csv OPTIONS (path "s3a://data/tpcds/1000/web_page.dat", delimiter '|');

create table web_page using delta as select * from tmp_web_page;

```


Now, you can use our TPC-DS driver for LakeVilla: 

```
docker exec -it lakevilla /bin/sh -c "./tpcds-lv lvconfig.conf <lvconfig> <num_threads> <num_runs>"
```

We support the following feature combinations for lvconfig:
- "0" = only markers and sublogs
- "1" = only markers and dependency tracking
- "2" = LV[Read]
- "01" = LV[Write]
- "02" = LV[Hybrid] without global dependecy tracking
- "12" = LV[Hybrid] without sublogs
- "012" = LV[Hybrid]

num_threads describe the number of execution threads LV is allowed to use. Furthre, num_runs defines how often each query should be executed.

The programm automatically runs TPC-DS's Q9, Q67, Q68, and Q90 and log their loading times. At termination, the program will show all measured times and some calcualted statistics.

### 3. Execute a Spark benchmark

The Spark benchmarks are executed using the ''spark-delta3'', ''spark-iceberg-hive'', ''hive'', and ''postgres'' containers and local driver programs.

#### YCSB

We provide a modified version of the original YCSB benchmark called YCSB-LH. The respective fork is linked as a submodule to this repository.

##### Compile the code

Enter the YCSB-LH fork, adjust the settings, and compile the used binding.

For Delta Lake:

```
mvn -pl site.ycsb:spark-delta-binding -am clean package
```

For Iceberg:

```
mvn -pl site.ycsb:spark-iceberg-binding -am clean package
```

##### Run the benchmark

e.g.:
```
bin/ycsb.sh run spark-delta -P workloads/workloada -p spark.lakehouse=delta
```

We provide additional information in the respective READMEs.




