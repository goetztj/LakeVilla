# LakeVilla

This repository contains the artifacts of our LakeVilla (LV) paper.
We made use of [docker](https://docs.docker.com/engine/install/) to automate the deployment and reproducibility.
You will find the following content:
- YCSB-LH: submodule to the adapted YCSB benchmark for Spark (provided as fork of [YCSB](https://github.com/brianfrankcooper/YCSB))
- Hive: all files needed for the hive docker container
- LV: all files for LakeVilla, including binaries for the container
- Spark: All files for the Spark and Delta Lake container
- Spark-Iceberg: All files for the Spark and Iceberg container

## Usage

We created a fully dockerized replication of our experimental setup. Please remember that the paper used larger setups, especially for Spark.

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

All LakeVilla benchmarks are executed using the ''lakevilla'' container.

#### YCSB

The container provides the ycsbc-lv executable based on YCSB-C (https://github.com/basicthinker/YCSB-C/tree/master) adapted for LakeVilla. Use the following command to execute a single run of YCSB using LakeVilla level 0:

``docker exec -it lakevilla /bin/sh -c "./ycsbc-lv -config lvconfig.conf -db LakeVillalvl0 -threads 1 -P YCSB-C/workloads/workloada.spec" ``

The invocation is similar to the original YCSB-C program (https://github.com/basicthinker/YCSB-C/tree/master):
- ''-config'' defines the LakeVilla configuration file (see README in LV)
- ''-db'' defines the used LakeVilla version; we provide: LakeVillalvl0 (level 0), LakeVillalvl1 (levels 0-1), and LakeVillalvl2 (levels 0-2).
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
- LakeVillaBankingQuerylvl0: Multi-Query implementation using LakeVilla Level 0
- LakeVillaBankingTablelvl0: Multi-table implementation using LakeVilla Level 0
- LakeVillaBankingQuerylvl1: Multi-Query implementation using LakeVilla Levels 0-1
- LakeVillaBankingTablelvl1: Multi-table implementation using LakeVilla Levels 0-1
- LakeVillaBankingQuerylvl2: Multi-Query implementation using LakeVilla Levels 0-2  (currently in development for the new docker setup)
- LakeVillaBankingTablelvl2: Multi-table implementation using LakeVilla Levels 0-2 (currently in development for the new docker setup)


#### Freshness benchmarks

Invoke the ''LakeVilla'' executable with the config in the lakevilla container:

``docker exec -it lakevilla /bin/sh -c "./LakeVilla lvconfig.conf``

A prompt will appear asking for the version you want to execute:

- Freshness benchmark (lvl 0)
- Freshness benchmark (lvl 0) (simplified)
- Freshness benchmark (lvl 1)
- Freshness benchmark (lvl 1) (simplified)
- Freshness benchmark (lvl 2) - coming soon
- Freshness benchmark (lvl 2) (simplified)

The paper uses the simplified versions; the full versions are currently under development for this LakeVilla setup.

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

##### CAB: Initial Loading

You can execute the initial loading benchmark with:

```
docker exec -it LakeVilla ./cab-lv lvconfig.conf initial
```

This will execute the initial loading phase presented in the paper on the TPC-H data.

##### CAB: Mixed Workload

You can execute the mixed workload benchmark with the following:

```
docker exec -it LakeVilla ./cab-lv lvconfig.conf
```

This will execute the mixed workload presented in the paper on the TPC-H data. It uses the read streams generated by the original CAB benchmark (https://github.com/alexandervanrenen/cab).

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




