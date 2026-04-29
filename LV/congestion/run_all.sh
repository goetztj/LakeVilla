#!/bin/bash

if [ "$#" -lt 1 ]; then
  echo "Usage: $0 <YCSB option>"
  echo "Example: $0 LakeVilla"
  exit 1
fi

echo "key-no using $1"
./key-no-congestion.sh $1

echo "key-medium using $1"
./key-medium-congestion.sh $1

echo "key-full using $1"
./key-full-congestion.sh $1

echo "tbl-no using $1"
./tbl-no-congestion.sh $1

echo "tbl-single using $1"
./tbl-single-congestion.sh $1

echo "tbl-medium using $1"
./tbl-medium-congestion.sh $1