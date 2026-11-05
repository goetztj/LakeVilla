# LakeVilla / Spark Delta Lake compatibility harness

`run_compat.py` checks that LakeVilla (`TransactionManagerDeltaLake`) and
Spark 3.5.3 with Delta Lake 3.0.0 (the `Spark/` image of this repository) can
share Delta tables: both engines read and write the same tables on a private
MinIO instance, and after every step both must see the expected rows.

```
python3 compat/run_compat.py                 # build, run everything, tear down
python3 compat/run_compat.py -k interleaved  # scenarios whose name contains "interleaved"
python3 compat/run_compat.py --levels 01,012 # level combinations of the per-level scenarios
python3 compat/run_compat.py --keep          # leave the stack running (debugging)
python3 compat/run_compat.py --list          # list the scenarios
```

Requirements: Docker with compose v2 and Python 3.8+ (standard library only).
The first run builds the images (MinIO from source, the Spark image, the
build environment of the `lakevilla` image) and downloads the Delta/Hadoop
jars; later runs reuse the Docker build cache. A full run (all scenarios)
takes about 15 minutes.

## Setup

`docker-compose.yml` runs under its own project name (`--project`, default
`lv-compat`) without fixed container names or host ports, so it does not
interfere with the main `docker-compose.yml` or other containers on the host:

| Service | Content |
|---|---|
| `minio` | MinIO built from `MinIO/`; the harness creates the bucket `compat` |
| `spark` | the `Spark/` image; `spark/spark_server.py` keeps one local SparkSession (no Hive metastore, tables are addressed by path: ``delta.`s3a://compat/...` ``) and executes SQL sent by the harness |
| `lv` | the `deps` stage of `LV/Dockerfile` (pinned Arrow and AWS SDK) with `LV/src` mounted; every run compiles `compat-lv` from the current sources (CMake option `LV_BUILD_COMPAT`) |

Every run writes its tables below `s3://compat/run-<timestamp>-<id>/` and its
logs (image build, compilation, Spark output, `results.json`) to
`compat/logs/<run>/`. `docker compose -p lv-compat down` is called at the
end (unless `--keep`); the volumes with the Ivy cache and the CMake build
directory are kept (`docker compose -p lv-compat down -v` removes them).

## compat-lv

`LV/src/compat.cpp` runs one LakeVilla operation per call and prints JSON
lines (see the header of the file): `create`, `drop`, `read`, `checkpoint` and
`txn`, a transaction over one or more tables with the operations `read`,
`insert`, `update`/`delete` (rewrite the file that contains a key, keeping the
other rows), `remove-file` (`remove_file(key)`) and `lookup`
(`read_file_as_table(key)`). Options select the levels and the protocol
settings; `--pause` stops before the commit until the harness says `commit`,
`abort` or `crash` (the marker of levels 0/1 is held meanwhile), `--crash`
exits without committing.

LakeVilla finds a key's file through the min/max statistics of the first
column. Rows are inserted one file per row (as YCSB does).

## Scenarios

| Scenario | Levels | What is checked |
|---|---|---|
| `spark_table_read_by_lv` | 01 | all primitive types written by Spark are read by LakeVilla |
| `lv_table_read_by_spark` | 01 | `create_table` + inserts: Spark `SELECT`, `DESCRIBE DETAIL`, `count(*)` and data skipping on LakeVilla statistics |
| `mixed_writers_timestamp(_micros)` | 01 | a timestamp column written by both engines (Spark INT96 / TIMESTAMP_MICROS) |
| `timestamps_outside_nanosecond_range` | 01 | Spark INT96 timestamps of the years 1500 and 9999; LakeVilla rewrites such a file and Spark reads it |
| `interleaved_writes` | all | the engines take turns inserting, updating and deleting each other's rows |
| `history_and_time_travel` | 01 | `DESCRIBE HISTORY` and `VERSION AS OF` on LakeVilla commits |
| `multi_row_files` | 01 | LakeVilla row deletes, `remove_file(key)` and updates of keys between min and max of multi-row Spark files |
| `key_in_several_files` | 01 | a key in several Spark files: `read_file_as_table` returns and `remove_file(key)` removes all of its rows |
| `spark_dml_and_optimize` | 01 | Spark `UPDATE`/`DELETE`/`MERGE`/`OPTIMIZE` on a LakeVilla table; LakeVilla updates and deletes keys of the compacted file |
| `spark_checkpoint_read_by_lv` | 01 | LakeVilla reads and extends a table with a Spark checkpoint |
| `lv_checkpoint_read_by_spark` | 01 | Spark reads and extends a table with a LakeVilla checkpoint |
| `spark_reads_during_lv_txn` | all | Spark reads while a LakeVilla transaction (marker) is open |
| `spark_commits_during_lv_txn` | all | Spark commits while a LakeVilla transaction that read the table is open; the running Spark session and a fresh snapshot see the outcome, no lost update |
| `spark_checkpoint_during_lv_txn` | all | as above, Spark's commit is version 10 and writes a checkpoint (default interval) |
| `spark_checkpoint_after_lv_txn` | all | a Spark session with a stale snapshot writes the next checkpoint |
| `lv_auto_checkpoint` | all | LakeVilla's commit of version 10 writes a checkpoint; both engines read from it after the log before it was removed |
| `lv_checkpoint_partitioned`, `lv_checkpoint_timestamp_ntz` | 01 | LakeVilla checkpoints of a partitioned Spark table and of a `timestamp_ntz` table |
| `crashed_lv_client` | 01, 012 | a crashed LakeVilla client leaves a marker; Spark and the next LakeVilla client continue |
| `lv_read_with_foreign_commits` | 2 | documented LV[Read] behaviour without/with `GLOBAL_LOG_CHECK_AT_BEGIN` and `GLOBAL_LOG_REPAIR` |
| `multi_table_transaction` | all | a LakeVilla transfer between two Spark tables |
| `lv_create_and_drop` | 01 | `create_table`/`drop_table`, column comments, `NOT NULL` |
| `spark_schema_evolution` | 01 | `ALTER TABLE ADD COLUMNS` by Spark; LakeVilla reads and rewrites older files |
| `partitioned_table`, `column_mapping` | 01 | LakeVilla reads partition values and physical column names and refuses to write such tables |
| `long_string_keys` | 01 | keys longer than 32 characters in Spark files (truncated statistics) |
| `deletion_vectors` | 01 | known limitation (XFAIL) |

"all" means the combinations of `--levels` (default `none,01,2,012`). With
level 2 the harness sets `GLOBAL_LOG_CHECK_AT_BEGIN` and `GLOBAL_LOG_REPAIR`
(`GLOBAL_LOG_REPAIR_AFTER=1`): without them LV[Read] never sees commits of
other engines (README, "Protocol settings"), which
`lv_read_with_foreign_commits` checks separately.

## Results

Every check is reported as `PASS`, `FAIL`, `XFAIL` (a known limitation,
listed with its reason), `XPASS` (a known limitation no longer shows: update
the expectation) or `ERROR` (the scenario could not run). The exit code is 1 if
a check failed, passed unexpectedly or a scenario could not run.

## Findings (2026-10-28, after "Key-based operations on rows")

403 PASS, 0 FAIL, 8 XFAIL (51 scenario runs). Everything else passes for all level combinations: types,
`create_table` and `drop_table`, interleaved inserts/updates/deletes of both
engines, Spark DML/`OPTIMIZE`, checkpoints of both engines (also after the
log before them was removed, for plain, partitioned and `timestamp_ntz`
tables), time travel, multi-table transactions, crashed clients, the
documented LV[Read] behaviour, and tables whose files differ from each other
(INT96 and microsecond timestamps, `NOT NULL` columns, columns added by
Spark, partition columns, column mapping): LakeVilla reads every file as the
table schema (README, "Table schema").

1. **A Spark checkpoint loses a committed LakeVilla transaction**
   (levels with markers: `01`, `012`; `spark_checkpoint_during_lv_txn`). A
   marker reserves version N; Spark commits N+1 (the marker is an empty
   commit to Spark) and writes a checkpoint at N+1. LakeVilla then writes its
   commit into N and reports success, but every reader starts from the
   checkpoint at N+1, which does not contain it. This happens with Delta's
   default interval (Spark's commit of version 10 while LakeVilla holds
   version 9).
2. **A running Spark session does not see a committed LakeVilla
   transaction** (`01`, `012`; `spark_commits_during_lv_txn`) until a
   newer version appears. A fresh snapshot contains it, and later Spark
   commits and checkpoints of that session do as well
   (`spark_checkpoint_after_lv_txn`).

Without markers (`none`, `2`) LakeVilla claims the version after Spark's
commit (put-if-absent) and aborts or commits after validation; nothing is
lost.

XFAIL (known limitation): deletion vectors
(LakeVilla drops the file Spark re-added with a deletion vector, so remaining
rows disappear, and writes to such tables are not refused).