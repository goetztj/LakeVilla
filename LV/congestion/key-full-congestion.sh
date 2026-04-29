#!/bin/bash

if [ "$#" -lt 1 ]; then
  echo "Usage: $0 <YCSB option>"
  echo "Example: $0 LakeVilla"
  exit 1
fi

echo "loading tables using $1"


./ycsbc-lv \
-threads 1 \
-config ./lvconfig39.conf \
-db $1 \
-P /LakeVilla/src/YCSB-C/workloads/workloada-interval-full.spec \
-load true \
-run false \
> ./load_result_rlv_a0-1-fullkey.txt 2>&1 &

echo "Loading done."
echo "Running."

./ycsbc-lv \
-threads 32 \
-config ./lvconfig39.conf \
-db $1 \
-P /LakeVilla/src/YCSB-C/workloads/workloada-interval-full.spec \
-load false \
-run true \
> ./run_result_rlv_a0-32-fullkey.txt 2>&1 &

echo "Runs done."