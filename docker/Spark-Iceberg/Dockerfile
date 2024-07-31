# modified version of https://github.com/apache/iceberg/blob/3a584a28352e5f13ca128599f4d331aa5eeaa374/python/dev/Dockerfile
#
# Licensed to the Apache Software Foundation (ASF) under one or more
# contributor license agreements.  See the NOTICE file distributed with
# this work for additional information regarding copyright ownership.
# The ASF licenses this file to You under the Apache License, Version 2.0
# (the "License"); you may not use this file except in compliance with
# the License.  You may obtain a copy of the License at
#
#    http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language goversparkning permissions and
# limitations under the License.

FROM python:3.9-bullseye

RUN apt-get -qq update && \
    apt-get -qq install -y --no-install-recommends \
      sudo \
      curl \
      vim \
      unzip \
      openjdk-11-jdk \
      build-essential \
      software-properties-common \
      wget \
      rsync \
      netcat \
      ssh && \
    apt-get -qq clean && \
    rm -rf /var/lib/apt/lists/*

ENV SPARK_HOME=${SPARK_HOME:-"/opt/spark"}
ENV HADOOP_HOME=${HADOOP_HOME:-"/opt/spark"}
ENV HADOOP_CONF_DIR=${HADOOP_CONF_DIR:-"/opt/spark/conf/"}
ENV PYTHONPATH=$SPARK_HOME/python:$SPARK_HOME/python/lib/py4j-0.10.9.5-src.zip:$PYTHONPATH


RUN mkdir -p ${HADOOP_HOME} && mkdir -p ${SPARK_HOME} && mkdir -p /home/iceberg/spark-events
RUN mkdir /tmp/spark-events
WORKDIR ${SPARK_HOME}

ENV SPARK_VERSION=3.4.1
ENV HADOOP_VERSION=3.3.4
ENV SCALA_VERSION=2.12
ENV ICEBERG_VERSION=1.3.1
ENV AWS_SDK_VERSION=2.20.18

RUN curl -s https://archive.apache.org/dist/spark/spark-3.4.1/spark-3.4.1-bin-hadoop3.tgz -o spark-${SPARK_VERSION}-bin-hadoop3.tgz \
 && tar xzf spark-${SPARK_VERSION}-bin-hadoop3.tgz --directory /opt/spark --strip-components 1 \
 && rm -rf spark-${SPARK_VERSION}-bin-hadoop3.tgz

# Download iceberg spark runtime
RUN curl -s https://repo1.maven.org/maven2/org/apache/iceberg/iceberg-spark-runtime-3.4_2.12/${ICEBERG_VERSION}/iceberg-spark-runtime-3.4_2.12-${ICEBERG_VERSION}.jar -Lo iceberg-spark-runtime-3.4_2.12-${ICEBERG_VERSION}.jar \
 && mv iceberg-spark-runtime-3.4_2.12-${ICEBERG_VERSION}.jar /opt/spark/jars

# Download Java AWS SDK
RUN curl -s https://repo1.maven.org/maven2/software/amazon/awssdk/bundle/${AWS_SDK_VERSION}/bundle-${AWS_SDK_VERSION}.jar -Lo bundle-${AWS_SDK_VERSION}.jar \
 && mv bundle-${AWS_SDK_VERSION}.jar /opt/spark/jars

# Download URL connection client required for S3FileIO
RUN curl -s https://repo1.maven.org/maven2/software/amazon/awssdk/url-connection-client/${AWS_SDK_VERSION}/url-connection-client-${AWS_SDK_VERSION}.jar -Lo url-connection-client-${AWS_SDK_VERSION}.jar \
 && mv url-connection-client-${AWS_SDK_VERSION}.jar /opt/spark/jars



COPY spark-defaults.conf /opt/spark/conf
COPY hive-site.xml /opt/spark/conf

ENV PATH="/opt/spark/sbin:/opt/spark/bin:${PATH}"

RUN chmod u+x /opt/spark/sbin/* && \
    chmod u+x /opt/spark/bin/*

RUN alias pip=pip3
RUN pip install -q ipython

COPY entrypoint.sh .

RUN chmod 777 entrypoint.sh

EXPOSE 7077
EXPOSE 8080
EXPOSE 15002

ENTRYPOINT ["./entrypoint.sh"]