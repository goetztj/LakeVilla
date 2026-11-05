# LakeVilla

This repository contains the artifacts of our LakeVilla (LV) paper.
We made use of [docker](https://docs.docker.com/engine/install/) to automate the deployment and reproducibility.
You will find the following content:
- `LV`: all files for LakeVilla, including the source code, binaries and libraries for the container (see `LV/README.md`)
- `compat`: compatibility harness that checks LakeVilla against Spark / Delta Lake (see [Compatibility harness](#5-compatibility-harness-lakevilla-and-spark--delta-lake))
- `Spark`: all files for the Spark and Delta Lake container
- `Spark-Iceberg`: all files for the Spark and Iceberg container
- `Hive`: all files needed for the Hive metastore container
- `Trino-Delta`: all files for the Trino and Delta Lake containers
- `MinIO`: Dockerfile building the MinIO server and client from source (the official images were removed from Docker Hub)
- `freshness-spark`: driver of the freshness benchmark for Spark (see `freshness-spark/README.md`)
- `DuckLake`: Hermitage scenarios for DuckLake (`hermitage.sql`)
- `Proofs`: TLA+ specifications of LakeVilla's protocols (`Proofs/TLA`) and the appendix of the paper (`Proofs/Appendix`)
- `Results`: raw traces from the paper's experiments
- `YCSB-LH`: submodule of the adapted YCSB benchmark for Spark (fork of [YCSB](https://github.com/brianfrankcooper/YCSB))
- `benchbase`: submodule of the adapted Benchbase implementation for DuckLake (fork of [Benchbase](https://github.com/cmu-db/benchbase))
- `trino`: submodule of the proof-of-concept Trino integration (fork of [Trino](https://github.com/trinodb/trino))

## What is LakeVilla?

LakeVilla is a client-side library for OTFs. It offers read and write optimization to offer full, multi-table ACID transactions while preserving full OTF interoperability.

### Interoperability of LV and other solutions for OTF transactions

| Solution | Architecture | Client-side Requirements | Global Requirements | Deployment Scope | Client Compatibility |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **LakeVilla** | Client-side library (middleware) | Apache Arrow support | None (Builds on object store guarantees) | Existing and new OTF tables (no migration) | LV and unmodified legacy OTF clients |
| **DuckLake** | Relational Metadata Representation | DuckLake runtime | Centralized Database | Migrated and new DuckLake tables | DuckLake clients only |
| **Project Nessie** | Catalog service | Nessie runtime | Centralized Catalog | Existing and new OTF tables (catalog managed) | Nessie clients only |

## Usage

We created a fully dockerized replication of our experimental setup. Please remember that the paper used larger setups.

Additionally, always reset the object store to the initial state after a benchmark to guarantee correct execution. The following command will restore the initial state of the object store:

```
docker exec -it spark-delta3 spark-sql -f ./reset.sql
```

`reset.sql` recreates the tables of `Spark/setup.sql`; the CAB namespaces `t1` to `t16` are recreated empty, so import the TPC-H data again afterwards. The TPC-DS namespace `tpcds1000` is kept.

### 0. Getting the code

The LakeVilla image and the build need submodules. Clone the repository with all submodules:

```
git clone --recurse-submodules <repository URL>
```

or, in an existing clone, initialise the submodules LakeVilla needs (YCSB-LH, benchbase and trino are only needed for the Spark, DuckLake and Trino experiments):

```
git submodule update --init LV/src/YCSB-C LV/src/rapidjson LV/src/CAB/cab
```

Without `LV/src/YCSB-C`, building the `lakevilla` image fails (`COPY src/YCSB-C/workloads`).

### 1. Initialization

Run docker compose:

```
docker compose up
```

It will create the following containers:

- **lakevilla**: contains all LakeVilla binaries and scripts to execute the experiments (working directory `/LakeVilla`, sources mounted at `/LakeVilla/src`)
- **spark-delta3**: a single-node Spark setup for the Spark+Delta Lake experiments
- **spark-iceberg-hive**: a single-node Spark setup for the Spark+Iceberg experiments (currently disabled due to version conflicts)
- **hive**: a Hive metastore for the catalogs of spark-delta3 and spark-iceberg-hive
- **postgres**: the Postgres backend of the Hive metastore
- **minio**: a MinIO object store (built from source at release `RELEASE.2025-09-07T16-13-09Z`, see `MinIO/README.md`)
- **mc**: a MinIO client container that creates the buckets `warehouse` and `data` (the `warehouse` bucket is emptied on every start). Inspired by https://iceberg.apache.org/spark-quickstart/#docker-compose
- **trino-coordinator**: the Trino coordinator (commented out by default due to port conflicts with Spark; comment out Spark to use Trino instead)
- **trino-worker** (2 replicas): the Trino workers (commented out by default, see above)

spark-delta3 creates all tables for Spark+Delta Lake and LakeVilla (`Spark/setup.sql`), while spark-iceberg-hive creates the respective Iceberg tables.

The containers publish the host ports 80 and 9000 (MinIO), 7077, 8080, 10000 and 15002 (Spark) and 9083 (Hive); they must be free, or adjust the mappings in `docker-compose.yml`.

Please keep in mind that we do not provide a TPC-H generator. Hence, you must generate and import the data to execute the CAB benchmark. We provide instructions on how to do that with DuckDB down below.

The first `docker compose up` builds the MinIO images from source and the LakeVilla image (Apache Arrow, AWS SDK) and takes a while.

#### Binaries in the lakevilla container

The `lakevilla` image picks the binaries for the host architecture:

- x86_64: `LV/binaries/x86_64/`, built from this repository with `LV/build_binaries.sh`
- aarch64: `LV/binaries/aarch64/` if present, otherwise the original binaries used for the paper (`LV/binaries/`)

Set `LV_BINARIES=source` to compile LakeVilla inside the image instead (`LV_BINARIES=source docker compose build lakevilla`). The original aarch64 binaries were linked against an unpinned AWS SDK snapshot; if they fail with the pinned SDK, use `LV_BINARIES=source`. The file `/LakeVilla/BINARIES` in the container states which binaries are used.

### 2. Compilation

If you do not want to use the provided binaries, you can compile LakeVilla from source (the submodules of [Getting the code](#0-getting-the-code) are needed). `LV/setup_ubuntu.sh` installs the pinned dependencies (Apache Arrow 24 and aws-sdk-cpp 1.11.880 in `/LakeVilla/aws`; LakeVilla needs an SDK version with conditional writes) on Ubuntu 24.04; the lakevilla container already contains them. On the host:

```
cmake -S LV/src -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DAWSSDK_ROOT_DIR=/LakeVilla/aws/
cmake --build build
```

In the lakevilla container, the sources are mounted at `/LakeVilla/src`:

```
docker exec -it lakevilla bash -c "cmake -S /LakeVilla/src -B /tmp/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DAWSSDK_ROOT_DIR=/LakeVilla/aws && cmake --build /tmp/build"
```

Options: `-DLV_BUILD_TESTS=ON` (tests), `-DLV_BUILD_BENCHMARKS=OFF` (no drivers), `-DLV_BUILD_JNI=OFF` (no JDK needed), `-DLV_BUILD_COMPAT=OFF` (no `compat-lv`), `-DLV_SANITIZERS="address;undefined"`. `LV_BINARIES=source docker compose build lakevilla` compiles the binaries in the image instead, and `LV/build_binaries.sh` rebuilds `LV/binaries/<arch>/` (unit tests included).

#### Tests

```
cmake -S LV/src -B build -G Ninja -DLV_BUILD_TESTS=ON -DAWSSDK_ROOT_DIR=/LakeVilla/aws/
cmake --build build
ctest --test-dir build -L unit          # in-memory object store, no services needed
LV_TEST_S3_ENDPOINT=localhost:9000 ctest --test-dir build -L integration   # against the MinIO of docker compose
```

The integration tests use the bucket `warehouse` and the credentials `admin`/`password` (`LV_TEST_S3_BUCKET`, `LV_TEST_S3_USER`, `LV_TEST_S3_PASSWD`); in the lakevilla container the endpoint is `minio:9000`. See `LV/src/tests/README.md`.

To check LakeVilla against Spark / Delta Lake, run the [compatibility harness](#5-compatibility-harness-lakevilla-and-spark--delta-lake) (`python3 compat/run_compat.py`).

### 3. Execute a LakeVilla benchmark

All LakeVilla benchmarks are executed using the `lakevilla` container. Its configuration file is `/LakeVilla/lvconfig.conf` (from `LV/lvconfig.conf`, keys in `LV/README.md` and below). For legacy reasons, our drivers still use the old description of the LakeVilla mechanisms:

- Level 0 = Markers and sublogs
- Level 1 = Markers and global conflict detection
- Level 2 = Global Version Log

To activate the three LV variants from the paper, select the following level combinations:

- Level 0 + 1 = LV[Write]
- Level 2 = LV[Read]
- Level 0 + 1 + 2 = LV[Hybrid]

#### Protocol settings

In addition to the keys of `LV/README.md`, `lvconfig.conf` accepts the following protocol settings (all optional, defaults in brackets):

| Key | Meaning |
|---|---|
| `ISOLATION` [`serializable`] | `serializable`: a transaction aborts (or, with level 0, redoes its implicit operations) if a table it read was changed by a concurrent commit. `snapshot`: only write-write conflicts (a concurrent commit removed a file the transaction removes) abort. |
| `MARKER_LEASE_MS` [`30000`] | Lease of a marker (levels 0/1), measured in object-store time (`Date` header of the store's responses). A holder that cannot commit before the deadline moves to a later version and redoes. |
| `MARKER_GRACE_MS` [`100`] | Waiters take an expired marker over (conditional on its ETag) only after the deadline plus this time. |
| `MAX_REDOS` [`3`] | Marker shifts and redo rounds of one commit before the transaction aborts. |
| `GLOBAL_LOG_REPAIR` [`0`] | LV[Read] only: after `GLOBAL_LOG_REPAIR_AFTER` [`3`] failed claims of a version, advance the global log over commits it does not contain (other engines, crashed clients). |
| `GLOBAL_LOG_CHECK_AT_BEGIN` [`0`] | Level 2: at begin, commits of other engines behind the global log join the snapshot (one HEAD request per table). |
| `CHECKPOINTS` [`1`] | A commit of a version that is a multiple of the checkpoint interval writes a Delta checkpoint of the table (like Spark), see [Checkpoints](#checkpoints). |
| `CHECKPOINT_INTERVAL` [`0`] | Checkpoint interval in versions; `0` uses the table property `delta.checkpointInterval` (default 10). |

A crashed client therefore blocks the other writers of a table for at most `MARKER_LEASE_MS + MARKER_GRACE_MS`.

#### Creating and dropping tables

The library creates and drops Delta tables directly on the object store (`LakeVilla/TransactionManager/ManagerDeltaLake.hpp`):

```cpp
TransactionManagerDeltaLake tm(levels, path, &connector, txn_id);
auto schema = arrow::schema({arrow::field("id", arrow::int32(), false),
                             arrow::field("name", arrow::utf8())});
TransactionManagerDeltaLake::TableOptions options;   // optional
options.description = "customers";                   // COMMENT
options.properties = {{"team", "lv"}};               // TBLPROPERTIES
tm.create_table("wh/db.db/customers/", *schema, options);  // TableResult::Ok
tm.drop_table("wh/db.db/customers/");
```

- `create_table` writes the same version 0 as Spark 3.5 / Delta 3.0 for ``CREATE TABLE delta.`s3a://<bucket>/<path>` (...) USING DELTA [COMMENT ...] [TBLPROPERTIES (...)]`` (commitInfo, metaData, protocol; `DESCRIBE DETAIL` shows the same table). The write is a put-if-absent, so concurrent creations have one winner. It refuses an existing table (`AlreadyExists`) and a non-empty location (`NotEmpty`). Arrow types are mapped to Delta types as Spark does (see `Helpers/DeltaSchema.hpp`). Field metadata becomes Delta column metadata (`comment` = `COMMENT` of a column). `timestamp` without a time zone becomes `timestamp_ntz` with protocol (3, 7) and the `timestampNtz` feature. Not supported: partitioning, column mapping (names with spaces or any of `,;{}()=`), `delta.*` table properties. A table on the object store is not registered in a catalog (Hive metastore); Spark accesses it by path.
- `drop_table` deletes all objects of the table like Spark's `DROP TABLE` of a managed table, and removes the table's entry from the global log (level 2). It deletes the checkpoints and version 0 first, so an interrupted drop leaves no readable table; running it again completes it. A location without a Delta log is never deleted (`NotEmpty`). Stop transactions on the table before dropping it.

#### Checkpoints

LakeVilla reads and writes Delta checkpoints like Spark 3.5 / Delta 3.0:

- **Writing**: after a commit of version `v` with `v % interval == 0` (`delta.checkpointInterval`, default 10, or `CHECKPOINT_INTERVAL`), the committing client writes `<v>.checkpoint.parquet` and `_last_checkpoint`. The snapshot of `v` is rebuilt from the newest checkpoint and the log versions after it (all of them are final when the commit succeeded); markers and the global-log lock are released before. A failed checkpoint does not fail the commit. `create_checkpoint(table_id)` writes a checkpoint of the version a transaction read. The checkpoint keeps `partitionValues`, `tags` and `modificationTime` of the files, the tombstones within `delta.deletedFileRetentionDuration` (default one week), and the latest `txn` (streaming writers) and `domainMetadata` actions. It is written with a put-if-absent (a checkpoint of another client for the same version is kept), and `_last_checkpoint` only moves forward (conditional write). Tables with deletion vectors, row tracking, V2 checkpoints and other unsupported table features are not checkpointed.
- **Reading**: a snapshot starts from the newest complete checkpoint that is not newer than the snapshot. Levels 0/1 take it from the listing of `_delta_log/` (`_last_checkpoint` may be missing or behind); a table in the global log (level 2) uses `_last_checkpoint`, or lists the log if `_last_checkpoint` is newer than the global log's version or the redo snapshot. Multi-part checkpoints are used only if all parts exist.
- LakeVilla never deletes log versions (no `delta.logRetentionDuration` cleanup).

#### Table schema

The schema of a table's snapshot (`metaData.schemaString`, `partitionColumns`, `delta.columnMapping.mode`) defines what LakeVilla reads and writes, not the schema of the individual data files (`Helpers/DeltaSchema.hpp`):

- **Reading**: every data file (`read_table`, `read_file`, `read_file_as_table`, `read_random_file_as_table`, redo) is converted to the table schema. Columns are matched by name (case-insensitive, as in Delta; by physical name with column mapping) and returned with the schema's names and order. Columns missing in a file (added later) are null, partition columns take the file's `partitionValues`. Types are converted without loss of values: timestamps of other units or without time zone (Spark's INT96, read in microseconds) become `timestamp[us, UTC]` (Delta `timestamp`), finer timestamps are truncated as Spark does, integers, floats and decimals are widened, nested fields are matched by name. A file that does not fit (e.g. nulls in a `NOT NULL` column, a value that cannot be converted) fails the read (`nullptr`, reason on `stderr`). An empty table is a table with the schema and no rows. Tables without `metaData` are read as stored.
- **Writing**: `add_file(table_id, arrow_table, key)` and redo convert the rows to the table schema; rows with other columns, nulls in `NOT NULL` columns or values that cannot be converted are refused. Data files are written with microsecond timestamps. LakeVilla does not write `partitionValues` or physical column names: writes to tables with partition columns or column mapping are refused. `add_file` with encoded Parquet content is not checked against the schema.
- Data files use the proleptic Gregorian calendar (like Spark 3) and say so with the Parquet metadata key `org.apache.spark.version` = `3.0.0`: Spark refuses dates before 1582 and timestamps before 1900 in files without it (`datetimeRebaseModeInRead=EXCEPTION`). Files written by earlier LakeVilla versions lack the key; Spark reads them with `spark.sql.parquet.datetimeRebaseModeInRead=CORRECTED`.

#### Keys

`read_file`, `read_file_as_table` and `remove_file(table_id, key, ...)` work on the rows whose first column (of the table schema) equals the key, in all data files that have such rows:

- Candidate files are found through the min/max statistics of the key column (`stats` of the add actions, compared as numbers for integer keys, as dates for date keys and byte-wise for strings; Spark's truncated string statistics are taken into account). Files without statistics for the column are candidates as well. Candidates are read to find the rows; a file whose statistics show a single row with the key (every file LakeVilla writes per key) is used without reading it.
- `read_file` and `read_file_as_table` return the rows of the key (not the whole file); `remove_file(key)` removes them: a file that only holds rows of the key is removed, the other rows of a file are written to a new file (as Spark's `DELETE`). A file that `add_file(..., key, prev)` wrote for the same update (`prev`) is not removed. With implicit logging (level 0), the redo searches the key again in the newest snapshot and applies the update to its rows.
- Conflicts are detected per file, as in Delta: two transactions that change different keys of the same file conflict.

#### Known limitations

Limitations with tables that other engines (Spark) write; `compat/run_compat.py` reports deletion vectors as XFAIL:

- **Key column**: the key of `read_file`, `read_file_as_table` and `remove_file(key)` is always the first column of the table schema; another key column cannot be chosen.
- **Deletion vectors**: LakeVilla does not apply deletion vectors (`delta.enableDeletionVectors`). When Spark deletes rows by a deletion vector, it removes the data file and adds it again with the vector in the same commit; LakeVilla drops that file, so the remaining rows of the file are missing for LakeVilla. Writes to such tables are not refused, and LakeVilla does not checkpoint them.

#### YCSB

The container provides the ycsbc-lv executable based on YCSB-C (https://github.com/basicthinker/YCSB-C/tree/master) adapted for LakeVilla. Use the following command to execute a single run of YCSB using LakeVilla:

```
docker exec -it lakevilla /bin/sh -c "./ycsbc-lv -config lvconfig.conf -db LakeVilla -threads 1 -P YCSB-C/workloads/workloada.spec"
```

The invocation is similar to the original YCSB-C program (https://github.com/basicthinker/YCSB-C/tree/master):
- `-config` defines the LakeVilla configuration file (see `LV/README.md`); the table is `PathToYCSBTable`
- `-db` defines the used LakeVilla version; we provide: LakeVilla, LakeVillaWrite (LV[Write]), LakeVillaRead (LV[Read]), LakeVillaDL (LV[DL]), LakeVillaMulti (Multi-Query transactions), LakeVillaWriteMulti (LV[Write], Multi-Query transactions), LakeVillaReadMulti (LV[Read], Multi-Query transactions), and LakeVillalvl0/LakeVillalvl1/LakeVillalvl2 (single levels, used by run_bench.sh). The driver reports the number of successful operations; aborted transactions count as failed operations.
- `-threads` defines the number of concurrent threads to use
- `-P` defines the workload to execute
- `-load false` / `-run false` skip the load / run phase (default: both)

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

We use an adapted version of YCSB-C to execute our synthetic banking benchmark (tables `PathToBankingTable0/1`). An example invocation is:

```
docker exec -it lakevilla /bin/sh -c "./ycsbc-lv -config lvconfig.conf -db LakeVillaBankingQuerylvl0 -threads 1 -P additional_workloads/workloadbanking.spec"
```

We provide the following setups:
- LakeVillaBankingQuerylvl0: Multi-Query implementation using LakeVilla [R]
- LakeVillaBankingTablelvl0: Multi-table implementation using LakeVilla [R]
- LakeVillaBankingQuerylvl1: Multi-Query implementation using LakeVilla [R, CT]
- LakeVillaBankingTablelvl1: Multi-table implementation using LakeVilla Levels [R, CT]
- LakeVillaBankingQuerylvl2: Multi-Query implementation using LakeVilla Levels [R, CT, I]
- LakeVillaBankingTablelvl2: Multi-table implementation using LakeVilla Levels [R, CT, I]


#### Freshness benchmarks

Invoke the `ui-lv` executable with the config in the lakevilla container (table `PathToFreshTable`, `FRESHRUNS` runs):

```
docker exec -it lakevilla /bin/sh -c "./ui-lv lvconfig.conf"
```

A prompt will appear asking for the version you want to execute:

- `0`: Freshness benchmark (lvl 0)
- `1`: Freshness benchmark (lvl 0) (simplified)
- `2`: Freshness benchmark (lvl 1)
- `3`: Freshness benchmark (lvl 1) (simplified)
- `4`: Freshness benchmark (lvl 2) (not implemented)
- `5`: Freshness benchmark (lvl 2) (simplified)
- `6`: interactive driver (incomplete)

The paper uses the simplified versions. The other options are extended versions of the benchmark. The Spark counterpart is in `freshness-spark` (see `freshness-spark/README.md`).

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

This will generate the respective Parquet files. Afterward, upload each file to the mc container and the object store:

```
docker cp /<path>/<table>.parquet mc:/

docker exec -it mc /bin/bash

mc put /<table>.parquet minio/data/sf1/
```

Afterward, use the spark-delta3 container to import these Parquet files as tables into each of the namespaces `t1` to `t16` (`CAB_PATH` of `lvconfig.conf`; the example shows `t1`):

```
docker exec -it spark-delta3 spark-sql

Use spark_catalog.t1;

CREATE TEMPORARY VIEW temp_customer USING parquet OPTIONS (path "s3a://data/sf1/customer.parquet");

create table customer using delta as select * from temp_customer;

CREATE TEMPORARY VIEW temp_lineitem USING parquet OPTIONS (path "s3a://data/sf1/lineitem.parquet");

create table lineitem using delta as select * from temp_lineitem;

CREATE TEMPORARY VIEW temp_nation USING parquet OPTIONS (path "s3a://data/sf1/nation.parquet");

create table nation using delta as select * from temp_nation;

CREATE TEMPORARY VIEW temp_orders USING parquet OPTIONS (path "s3a://data/sf1/orders.parquet");

create table orders using delta as select * from temp_orders;

CREATE TEMPORARY VIEW temp_part USING parquet OPTIONS (path "s3a://data/sf1/part.parquet");

create table part using delta as select * from temp_part;

CREATE TEMPORARY VIEW temp_partsupp USING parquet OPTIONS (path "s3a://data/sf1/partsupp.parquet");

create table partsupp using delta as select * from temp_partsupp;

CREATE TEMPORARY VIEW temp_region USING parquet OPTIONS (path "s3a://data/sf1/region.parquet");

create table region using delta as select * from temp_region;

CREATE TEMPORARY VIEW temp_supplier USING parquet OPTIONS (path "s3a://data/sf1/supplier.parquet");

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

"num_readers" and "num_writers" define the number of concurrent read and write LV clients respectively. We only allow numbers larger than 0.

This will execute the concurrent read and write streams as shown in our paper with automatic feature switching. It uses the read streams generated by the original CAB benchmark (https://github.com/alexandervanrenen/cab).


##### CAB: table-scaling

You can execute our table-scaling experiment using:

```
docker exec -it lakevilla /bin/sh -c "./cab-lv table lvconfig.conf /LakeVilla/CAB/sample/ <num_tables> <base_path>"
```

"num_tables" defines the maximal number of tables that should be accessed from a single transaction.

"base_path" is the path all distinct tables share. E.g. "/wh/tpcds1000.db/customer".

This will execute the table-scaling experiment from our paper. 

#### Hermitage

You can invoke the automatic Hermitage check using (the scenarios run on the tables `wh/tmp.db/lock`, `lock2`, `snap1`, `snap2` and `snap3` of `Spark/setup.sql`; reset them with `reset.sql` before every run):

```
docker exec -it spark-delta3 spark-sql -f ./reset.sql
docker exec -it lakevilla /bin/sh -c "./hermitage-lv lvconfig.conf <lvconfig>"
```

We support the following feature combinations for lvconfig:
- "0" = only markers and sublogs
- "1" = only markers and dependency tracking
- "2" = LV[Read]
- "01" = LV[Write]
- "02" = LV[Hybrid] without global dependency tracking
- "12" = LV[Hybrid] without sublogs
- "012" = LV[Hybrid]

The program verifies the results of all Hermitage scenarios and reports the commit outcomes. For each scenario it prints "success" if the rows of the tables match the expected ones (independent of their order), otherwise "failure (manual comparison necessary)" with the rows it got and the expected rows.

The expected values assume that a conflicting transaction waits and redoes (LV[Write], "01"). Configurations that cannot redo abort the later writer instead (first committer wins; all with level 2: LV[Read], and LV[Hybrid] when the snapshot predates the other transaction's marker). As the scenarios run on the same tables, the following checks then start from a different state and report "failure (manual comparison necessary)". With the default `ISOLATION=serializable`, G2 aborts the second transaction for "012" (the anomaly is prevented). Current results: "01" 10/10; "012" 7/10 (OTV and PMP abort T2 and change the state of the following checks, G2 aborts T2).

#### TPC-DS

Similar to the CAB benchmark, users must generate and upload TPC-DS data to the object store. We used [the official generation tools from TPC](https://www.tpc.org/tpcds/).

The generator (`dsdgen`) writes one `<table>.dat` file per table (columns separated by `|`). Afterward, upload the files to the mc container and the object store:

```
docker cp /<path>/<table>.dat mc:/

docker exec -it mc /bin/bash

mc put /<table>.dat minio/data/tpcds/
```

Afterward, use the spark-delta3 container to import these files as tables under the namespace `tpcds1000` (the paths of `tpcds-lv`):

```
docker exec -it spark-delta3 spark-sql

Use spark_catalog.tpcds1000;

CREATE TEMPORARY VIEW tmp_call_center2 ( cc_call_center_sk INT, cc_call_center_id STRING, cc_rec_start_date DATE, cc_rec_end_date DATE, cc_closed_date_sk INT, cc_open_date_sk INT, cc_name STRING, cc_class STRING, cc_employees INT, cc_sq_ft INT, cc_hire_date DATE, cc_region STRING, cc_mkt_id INT, cc_mkt_class STRING, cc_mkt_desc STRING, cc_market_manager STRING, cc_division STRING, cc_company STRING, cc_address STRING, cc_city STRING, cc_state STRING, cc_zip STRING, cc_country STRING, cc_gmt_offset DECIMAL(15,2), cc_tax_percentage DECIMAL(15,2) ) USING csv OPTIONS (path "s3a://data/tpcds/call_center.dat", delimiter '|');

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

CREATE TEMPORARY VIEW tmp_web_page ( wp_web_page_sk INT NOT NULL, wp_web_site_sk INT, wp_access_date_sk INT, wp_access_time_sk INT, wp_rec_start_date DATE, wp_rec_end_date DATE, wp_creation_date_sk INT, wp_access_path STRING, wp_char_count INT, wp_link_count INT, wp_image_count INT, wp_max_ad_count INT, wp_theme STRING, wp_image_path STRING, wp_auth_required STRING, wp_pay_required STRING, wp_customer_sk INT) USING csv OPTIONS (path "s3a://data/tpcds/web_page.dat", delimiter '|');

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
- "02" = LV[Hybrid] without global dependency tracking
- "12" = LV[Hybrid] without sublogs
- "012" = LV[Hybrid]

num_threads describes the number of execution threads LV is allowed to use. Further, num_runs defines how often each query should be executed.

The program automatically runs the TPC-DS queries Q9, Q67, Q68 and Q90 and logs their loading times. At termination, the program shows all measured times and some calculated statistics.

### 4. Execute a Spark benchmark

The Spark benchmarks are executed using the `spark-delta3`, `spark-iceberg-hive` (currently disabled), `hive` and `postgres` containers and local driver programs.

#### YCSB

We provide a modified version of the original YCSB benchmark called YCSB-LH. The respective fork is linked as a submodule to this repository (`git submodule update --init YCSB-LH`).

##### Compile the code

Enter the YCSB-LH fork, adjust the settings, and compile the used binding.

For Delta Lake:

```
mvn -pl site.ycsb:sparkDelta-binding -am clean package
```

For Iceberg:

```
mvn -pl site.ycsb:sparkIceberg-binding -am clean package
```

##### Run the benchmark

Load the data, then run the workload, e.g. for Delta Lake:

```
bin/ycsb.sh load sparkDelta -P workloads/workloada -p spark.lakehouse=delta
bin/ycsb.sh run sparkDelta -P workloads/workloada -p spark.lakehouse=delta
```

For Iceberg, use the binding `sparkIceberg` with `-p spark.lakehouse=iceberg`. We provide additional information in the READMEs of the bindings (`YCSB-LH/sparkDelta/README.md`, `YCSB-LH/sparkIceberg/README.md`).

#### Freshness

The freshness benchmark for Spark (Delta Lake 3.0 and Iceberg 1.4.2) is in `freshness-spark`; see `freshness-spark/README.md` for its configuration, compilation and `run.sh`.

### 5. Compatibility harness: LakeVilla and Spark / Delta Lake

`compat/run_compat.py` checks that LakeVilla and Spark 3.5.3 with Delta Lake 3.0.0 (the `Spark` image) can share Delta tables: both engines read and write the same tables on a private MinIO instance, and after every step both must see the expected rows. The scenarios cover all column types, creating and dropping tables, interleaved inserts/updates/deletes of both engines, Spark DML and `OPTIMIZE`, checkpoints of both engines, time travel, keys inside multi-row files, Spark commits and checkpoints during LakeVilla transactions, crashed LakeVilla clients, multi-table transactions and the level combinations `none`, `01`, `2` and `012`.

Requirements: Docker with compose v2, Python 3.8 or newer (standard library only), network access on the first run (MinIO sources, Delta and Hadoop jars) and about 4 GB for the images. The harness uses its own compose project (`lv-compat`, `compat/docker-compose.yml`) without fixed container names or host ports, so it runs next to the containers of `docker compose up` and other projects.

```
python3 compat/run_compat.py                     # build the images, run all scenarios, stop the stack
python3 compat/run_compat.py --list              # list the scenarios
python3 compat/run_compat.py -k interleaved -k checkpoint   # only scenarios whose name contains these texts
python3 compat/run_compat.py --levels 01,012     # level combinations of the per-level scenarios
python3 compat/run_compat.py --no-build --keep   # reuse the images, leave the stack running afterwards
```

Options: `-k <text>` (repeatable), `--levels` (default `none,01,2,012`), `--default-levels` (levels of the other scenarios, default `01`), `--project` (compose project name, default `lv-compat`), `--no-build`, `--keep`, `--list`.

What a run does:

1. builds the images: MinIO from `MinIO/`, the Spark image of `Spark/` and the build environment of `LV/Dockerfile` (`deps` stage); the first build takes a while, later runs reuse the Docker build cache,
2. starts the stack and creates the bucket `compat`,
3. compiles `compat-lv` (`LV/src/compat.cpp`) from the current `LV/src` (the `rapidjson` submodule is initialised if needed), so the checks always test the sources of the checkout,
4. starts one Spark session (`compat/spark/spark_server.py`) and runs the scenarios; a full run takes about 15 minutes,
5. stops the stack (`docker compose -p lv-compat down`), unless `--keep`.

Every check is reported as `PASS`, `FAIL`, `XFAIL` (known limitation, printed with its reason), `XPASS` (a known limitation no longer shows: update its expectation) or `ERROR` (the scenario could not run); the exit code is 1 if a check failed, passed unexpectedly or a scenario could not run. The logs of a run (image build, compilation, Spark output, `results.json`) are in `compat/logs/<run>/`. Current result: 403 PASS, 0 FAIL, 8 XFAIL (deletion vectors).

Debugging: with `--keep` the stack stays up; the tables of a run are below `s3://compat/<run>/<scenario>-<levels>/` of the harness MinIO, e.g.:

```
docker compose -p lv-compat -f compat/docker-compose.yml exec lv /build/compat-lv --levels 01 read <run>/<scenario>-01/t/
docker compose -p lv-compat -f compat/docker-compose.yml down       # stop the stack
docker compose -p lv-compat -f compat/docker-compose.yml down -v    # also remove the caches (Delta jars, CMake build)
docker rmi lv-compat-minio lv-compat-spark lv-compat-lv             # remove the images
```

`compat/README.md` describes the scenarios, `compat-lv` and the findings in detail.




