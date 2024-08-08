#!/bin/bash
# based on https://github.com/bitsondatadev/trino-getting-started

export METASTORE_DB_HOSTNAME=${METASTORE_DB_HOSTNAME:-localhost}

echo "Waiting for database on ${METASTORE_DB_HOSTNAME} to launch on 5432 ..."
while ! nc -z ${METASTORE_DB_HOSTNAME} 5432; do
   sleep 1
done

echo "Database on ${METASTORE_DB_HOSTNAME}:5432 started"
echo "Init apache hive metastore on ${METASTORE_DB_HOSTNAME}:5432"

$HIVE_HOME/bin/schematool -dbType postgres -initSchema
$HIVE_HOME/bin/start-metastore
