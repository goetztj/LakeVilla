#!/bin/bash
#

# modified version of https://github.com/apache/iceberg/blob/3a584a28352e5f13ca128599f4d331aa5eeaa374/python/dev/Dockerfile


# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.
#


echo "Waiting for metastore on ${METASTORE_HOSTNAME} to launch on ${METASTORE_PORT} ..."
while ! nc -z ${METASTORE_HOSTNAME} ${METASTORE_PORT}; do
   sleep 1
done

start-master.sh -i ${SPARK_HOST_NAME} -p ${SPARK_PORT} --webui-port ${SPARK_WEB_PORT} 
start-worker.sh spark://${SPARK_HOST_NAME}:${SPARK_PORT}
start-history-server.sh

./bin/spark-sql -f ./setup.sql

tail -f /dev/null