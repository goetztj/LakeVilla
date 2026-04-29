#!/bin/bash

if [ "$#" -lt 1 ]; then
  echo "Usage: $0 <YCSB option>"
  echo "Example: $0 LakeVilla"
  exit 1
fi

echo "loading tables using $1"


./ycsbc-lv \
-threads 1 \
-config ./lvconfig38.conf \
-db $1 \
-P /LakeVilla/src/YCSB-C/workloads/workloada-interval-medium.spec \
-load true \
-run false \
> ./load_result_rlv_a0-1-mediumkey.txt 2>&1 &

echo "Loading done."
echo "Running."

./ycsbc-lv \
-threads 32 \
-config ./lvconfig38.conf \
-db $1 \
-P /LakeVilla/src/YCSB-C/workloads/workloada-interval-medium.spec \
-load false \
-run true \
> ./run_result_rlv_a0-32-mediumkey.txt 2>&1 &

echo "Runs done."