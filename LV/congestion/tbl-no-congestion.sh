#!/bin/bash

if [ "$#" -lt 1 ]; then
  echo "Usage: $0 <YCSB option>"
  echo "Example: $0 LakeVilla"
  exit 1
fi

echo "loading tables using $1"

for i in $(seq 0 31); do
  ./ycsbc-lv \
  -threads 1 \
  -config ./lvconfig${i}.conf \
  -db $1 \
  -P /LakeVilla/src/YCSB-C/workloads/workloada.spec \
  -load true \
  -run false \
  > ./load_result_rlv_a${i}-32.txt 2>&1 &
done

echo "Started Loading done."
wait
echo "Loading done."
echo "Running."

for i in $(seq 0 31); do
  ./ycsbc-lv \
  -threads 1 \
  -config ./lvconfig${i}.conf \
  -db $1 \
  -P /LakeVilla/src/YCSB-C/workloads/workloada.spec \
  -load false \
  -run true \
  > ./run_result_rlv_a${i}-32.txt 2>&1 &
done

echo "Started Runs done."
wait
echo "Runs done."