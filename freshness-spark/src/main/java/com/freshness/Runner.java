package com.freshness;

import org.apache.spark.sql.SparkSession;

public class Runner {

    public static void main(String[] args) {
        String surl = "spark://localhost:7077";
        String lakehouse = "delta";
        String metastore = "thrift://localhost:9083";

        String objectStoreUri = "http://localhost:9000";
        String objectStoreUser = "user";
        String objectStorePwd = "password";

        System.out.println("connecting to " + surl);
        org.apache.spark.sql.SparkSession session;

        try {

            if (lakehouse.equals("iceberg")) {
                System.out.println("using iceberg");
                SparkSession.Builder builder = SparkSession.builder().appName("YCSB - Freshbench")
                        .master(surl)
                        .config("hive.metastore.uris", metastore).enableHiveSupport()
                        .config("spark.driver.bindAddress", "localhost")
                        .config("spark.sql.extensions",
                                "org.apache.iceberg.spark.extensions.IcebergSparkSessionExtensions")
                        .config("spark.sql.catalog.spark_catalog", "org.apache.iceberg.spark.SparkCatalog")
                        .config("spark.sql.catalog.spark_catalog.type", "hive")
                        .config("spark.sql.catalog.spark_catalog.uri", metastore)
                        .config("spark.sql.catalog.spark_catalog.io-impl", "org.apache.iceberg.aws.s3.S3FileIO")
                        .config("spark.sql.catalog.spark_catalog.warehouse", "s3a://warehouse/wh/")
                        .config("spark.sql.catalog.spark_catalog.s3.endpoint", objectStoreUri)
                        .config("spark.sql.defaultCatalog", "spark_catalog")
                        .config("spark.sql.catalogImplementation", "hive")
                        .config("spark.driver.memory", "16g")
                        .config("spark.executor.memory", "8g")
                        .config("spark.memory.offHeap.enabled", "true")
                        .config("spark.memory.offHeap.size", "16g")
                        .config("spark.executorEnv.SPARK_PUBLIC_DNS", "localhost")
                        .config("spark.executorEnv.SPARK_LOCAL_IP", "localhost")
                        .config("spark.files.useFetchCache", "false")
                        .config("spark.sql.catalog.spark_catalog.cache-enabled", "false")
                        .config("spark.sql.catalog.spark_catalog.cache.expiration-interval-ms", "0");

                session = builder.getOrCreate();
                System.out.println(session.catalog().currentCatalog());
            } else {
                System.out.println("using Delta Lake");
                SparkSession.Builder builder = SparkSession.builder().appName("YCSB - Freshbench")
                        .master(surl).config("hive.metastore.uris", metastore).enableHiveSupport()
                        .config("spark.sql.extensions", "io.delta.sql.DeltaSparkSessionExtension")
                        .config("spark.sql.catalog.spark_catalog", "org.apache.spark.sql.delta.catalog.DeltaCatalog")
                        .config("spark.sql.catalog.spark_catalog.warehouse", "s3a://warehouse/wh/")
                        .config("spark.sql.warehouse.dir", "s3a://warehouse/wh/")
                        .config("spark.hadoop.fs.s3a.endpoint", objectStoreUri)
                        .config("spark.hadoop.fs.s3a.access.key", objectStoreUser)
                        .config("spark.hadoop.fs.s3a.secret.key", objectStorePwd)
                        .config("spark.eventLog.dir", "/spark-delta/spark-events")
                        .config("spark.history.fs.logDirectory", "/spark-delta/spark-events")
                        .config("spark.sql.catalogImplementation", "hive")
                        .config("spark.eventLog.enabled", "true")
                        .config("spark.databricks.delta.retentionDurationCheck.enabled", "false")
                        .config("spark.sql.defaultCatalog", "spark_catalog")
                        .config("spark.driver.memory", "16g")
                        .config("spark.executorEnv.SPARK_PUBLIC_DNS", "localhost")
                        .config("spark.executorEnv.SPARK_LOCAL_IP", "localhost")
                        .config("spark.driver.bindAddress", "localhost")
                        .config("spark.executor.memory", "8g")
                        .config("spark.memory.offHeap.enabled", "true")
                        .config("spark.memory.offHeap.size", "16g")
                        .config("spark.files.useFetchCache", "false")
                        .config("spark.sql.catalog.spark_catalog.cache-enabled", "false")
                        .config("spark.sql.catalog.spark_catalog.cache.expiration-interval-ms", "0")
                        .config("spark.databricks.io.cache.enabled", "false");

                session = builder.getOrCreate();
            }
            System.out.println(">>> Spark Master at: " + session.sparkContext().master());
        } catch (Exception e) {
            System.out.println("could not establish connection to Spark!");
            return;
        }

        Lock l = new Lock();
        Reader r = new Reader(session, l);
        Writer w = new Writer(session, l);

        r.start();
        w.start();
        System.out.println("all started");
        try {
            r.join();
            w.join();
        } catch (Exception e) {
            System.out.println("got Exception " + e);
        }
        System.out.println("done");
        session.close();
    }
}
