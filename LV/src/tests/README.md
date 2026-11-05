# LakeVilla tests

## Unit and protocol tests (no object store needed)

`lakevilla-tests` runs the transaction protocol of all level combinations
(`none`, `0`, `1`, `2`, `01`, `02`, `12`, `012`) against an in-memory object
store (`Connectors/MemoryConnector`) with S3 semantics (atomic PUT,
put-if-absent, sorted prefix listings) and fault injection.

```
cmake -S LV/src -B build -G Ninja -DLV_BUILD_TESTS=ON -DAWSSDK_ROOT_DIR=<aws install>
cmake --build build
ctest --test-dir build -L unit --output-on-failure
```

Sanitizer builds: add `-DLV_SANITIZERS="address;undefined"` or
`-DLV_SANITIZERS=thread` (use a separate build directory). For
ThreadSanitizer run the tests with `ARROW_DEFAULT_MEMORY_POOL=system`;
Arrow's bundled mimalloc is not instrumented and causes false reports.

## Integration tests (S3 / MinIO)

`lakevilla-s3-tests` checks the S3 connector (conditional writes, pagination,
binary data) and complete transactions against a real endpoint. The tests are
skipped unless `LV_TEST_S3_ENDPOINT` is set. With the docker-compose setup:

```
docker compose up -d minio mc
LV_TEST_S3_ENDPOINT=localhost:9000 LV_TEST_S3_USER=admin \
LV_TEST_S3_PASSWD=password LV_TEST_S3_BUCKET=warehouse \
  ctest --test-dir build -L integration --output-on-failure
```

All objects are written below `lv-tests/<random>/` and removed afterwards.

Two tests prepare checks of checkpoints with other engines and keep their
tables (paths inside the bucket, ending with `/`):

- `LV_TEST_CHECKPOINT_TABLE=<table>/`: `CheckpointExistingTable` writes a
  LakeVilla checkpoint of an existing table (e.g. one created by Spark).
- `LV_TEST_CHECKPOINT_COMMITS_TABLE=<new table>/`:
  `CommitsCheckpointTableForSpark` creates a table and commits ten inserts; the
  commit of version 10 writes a checkpoint.

Afterwards, remove the log versions before the checkpoint and read the table
with Spark (``SELECT * FROM delta.`s3a://<bucket>/<table>` ``).
