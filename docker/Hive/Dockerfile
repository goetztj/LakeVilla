FROM eclipse-temurin:17-jdk-jammy

RUN mkdir /etc/metastore

ENV HADOOP_HOME=/etc/metastore/hadoop-3.3.5
ENV METASTORE_DB_HOSTNAME=postgres
ENV HIVE_HOME=/etc/metastore/apache-hive-metastore-3.0.0-bin

WORKDIR /etc/metastore

RUN apt-get update && apt-get install -y ssh pdsh netcat

RUN wget https://dlcdn.apache.org/hadoop/common/hadoop-3.3.5/hadoop-3.3.5.tar.gz

RUN tar xfv ./hadoop-3.3.5.tar.gz

RUN wget https://dlcdn.apache.org/hive/hive-standalone-metastore-3.0.0/hive-standalone-metastore-3.0.0-bin.tar.gz

RUN tar xfv ./hive-standalone-metastore-3.0.0-bin.tar.gz

RUN wget https://dev.mysql.com/get/Downloads/Connector-J/mysql-connector-j-8.0.33.tar.gz

RUN tar xfv mysql-connector-j-8.0.33.tar.gz

RUN  cp ./mysql-connector-j-8.0.33/mysql-connector-j-8.0.33.jar $HIVE_HOME/lib/

RUN chmod 644 $HIVE_HOME/lib/mysql-connector-j-8.0.33.jar

RUN wget https://jdbc.postgresql.org/download/postgresql-42.6.0.jar

RUN wget https://repo1.maven.org/maven2/org/apache/hadoop/hadoop-aws/3.3.5/hadoop-aws-3.3.5.jar

RUN wget https://repo1.maven.org/maven2/com/amazonaws/aws-java-sdk-bundle/1.12.316/aws-java-sdk-bundle-1.12.316.jar

RUN cp ./hadoop-aws-3.3.5.jar $HADOOP_HOME/lib/
RUN cp ./aws-java-sdk-bundle-1.12.316.jar $HADOOP_HOME/lib/

RUN cp ./hadoop-aws-3.3.5.jar $HIVE_HOME/lib/
RUN cp ./aws-java-sdk-bundle-1.12.316.jar $HIVE_HOME/lib/

COPY ./init-hive.sh .

RUN cp ./postgresql-42.6.0.jar $HIVE_HOME/lib/


RUN chmod +x ./init-hive.sh

RUN chmod 644 $HIVE_HOME/lib/postgresql-42.6.0.jar

COPY ./metastore-site.xml $HIVE_HOME/conf/

EXPOSE 9083


CMD ["./init-hive.sh"]