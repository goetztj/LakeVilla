-- YCSB LakeVilla
CREATE NAMESPACE ycsb;

CREATE TABLE spark_catalog.ycsb.usertable (YCSB_KEY VARCHAR(255),FIELD0 String, FIELD1 String,FIELD2 String, FIELD3 String,FIELD4 String, FIELD5 String,FIELD6 String, FIELD7 String,FIELD8 String, FIELD9 String) USING delta;

-- YCSB Spark-Delta
CREATE NAMESPACE ycsbsparkdl;

CREATE TABLE spark_catalog.ycsbsparkdl.usertable (YCSB_KEY VARCHAR(255),FIELD0 String, FIELD1 String,FIELD2 String, FIELD3 String,FIELD4 String, FIELD5 String,FIELD6 String, FIELD7 String,FIELD8 String, FIELD9 String) USING delta;

-- Banking LakeVilla
CREATE NAMESPACE banking;
CREATE TABLE spark_catalog.banking.banking0 (Banking_ID string, balance string) USING delta;

INSERT INTO spark_catalog.banking.banking0 VALUES ("0", "500.0");

CREATE TABLE spark_catalog.banking.banking1 (Banking_ID string, balance string) USING delta;

INSERT INTO spark_catalog.banking.banking1 VALUES ("0", "500.0");

-- Freshness LakeVilla
CREATE NAMESPACE freshness;

CREATE TABLE spark_catalog.freshness.table (FRESH_KEY int) USING delta;

-- CAB LakeVilla
CREATE NAMESPACE t1;
CREATE NAMESPACE t2;
CREATE NAMESPACE t3;
CREATE NAMESPACE t4;
CREATE NAMESPACE t5;
CREATE NAMESPACE t6;
CREATE NAMESPACE t7;
CREATE NAMESPACE t8;
CREATE NAMESPACE t9;
CREATE NAMESPACE t10;
CREATE NAMESPACE t11;
CREATE NAMESPACE t12;
CREATE NAMESPACE t13;
CREATE NAMESPACE t14;
CREATE NAMESPACE t15;
CREATE NAMESPACE t16;

-- Generate and import TPC-H data!


