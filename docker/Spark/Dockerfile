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
      maven \
      ssh && \
    apt-get -qq clean && \
    rm -rf /var/lib/apt/lists/*

ENV SPARK_HOME=${SPARK_HOME:-"/opt/spark"}
ENV HADOOP_HOME=${HADOOP_HOME:-"/opt/spark"}
ENV HADOOP_CONF_DIR=${HADOOP_CONF_DIR:-"/opt/spark/conf/"}
ENV PYTHONPATH=$SPARK_HOME/python:$SPARK_HOME/python/lib/py4j-0.10.9.5-src.zip:$PYTHONPATH


RUN mkdir -p ${HADOOP_HOME} && mkdir -p ${SPARK_HOME} && mkdir -p /home/delta/spark-events
RUN mkdir /tmp/spark-events
WORKDIR ${SPARK_HOME}

ENV SPARK_VERSION=3.5.0
ENV HADOOP_VERSION=3.3.4
ENV SCALA_VERSION=2.12

RUN curl -s https://archive.apache.org/dist/spark/spark-3.5.0/spark-3.5.0-bin-hadoop3.tgz -o spark-${SPARK_VERSION}-bin-hadoop3.tgz \
 && tar xzf spark-${SPARK_VERSION}-bin-hadoop3.tgz --directory /opt/spark --strip-components 1 \
 && rm -rf spark-${SPARK_VERSION}-bin-hadoop3.tgz


COPY spark-defaults.conf /opt/spark/conf
COPY hive-site.xml /opt/spark/conf

ENV PATH="/opt/spark/sbin:/opt/spark/bin:${PATH}"

RUN chmod u+x /opt/spark/sbin/* && \
    chmod u+x /opt/spark/bin/*

RUN alias pip=pip3
RUN pip install -q ipython
RUN pip install delta-spark==3.0.0

COPY entrypoint.sh .

RUN chmod 777 entrypoint.sh

EXPOSE 7077
EXPOSE 8080
EXPOSE 15002

ENTRYPOINT ["./entrypoint.sh"]