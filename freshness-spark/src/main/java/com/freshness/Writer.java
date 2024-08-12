package com.freshness;

public class Writer extends Thread {

    org.apache.spark.sql.SparkSession session;

    Lock lock;

    String table = "spark_catalog.freshnessSpark.table";

    int numRuns = 10;

    Writer(org.apache.spark.sql.SparkSession s, Lock l) {
        session = s;
        lock = l;
    }

    public void run() {
        System.out.println("writer running");

        int counter = 0;

        String partial_sql = "INSERT INTO " + table + " VALUES (";

        while (counter < numRuns) {
            while (!lock.lock()) {
                try {
                    wait(200);
                } catch (Exception e) {

                }
            }

            String full_sql = partial_sql + System.nanoTime() + "," + counter + ")";

            session.sql(full_sql);

            counter++;
        }

    }
}
