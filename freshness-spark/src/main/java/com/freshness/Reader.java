package com.freshness;

import java.io.FileWriter;
import java.io.IOException;
import java.util.LinkedList;
import java.util.Queue;

import org.apache.spark.sql.Dataset;
import org.apache.spark.sql.Row;

public class Reader extends Thread {

    org.apache.spark.sql.SparkSession session;

    Lock lock;

    String table = "spark_catalog.freshnessSpark.table";

    Queue<Long> times1;
    Queue<Long> times2;

    int numRuns = 10;

    Reader(org.apache.spark.sql.SparkSession s, Lock l) {
        session = s;
        lock = l;
        times1 = new LinkedList<>();
        times2 = new LinkedList<>();
    }

    public void run() {
        System.out.println("reader running");
        lock.unlock();

        int counter = 0;

        while (counter < numRuns) {
            long time1 = System.nanoTime();
            Dataset<Row> data = session.table(table).where("d =" + counter).select("t");
            long time2 = System.nanoTime();

            if (!data.isEmpty()) {
                long l = data.head().getLong(0);

                times1.add(time1 - l);
                times2.add(time2 - l);
                System.out.println((counter+1) + "/" + numRuns);
                counter++;
                lock.unlock();
            }

        }

        try {
            FileWriter writer1 = new FileWriter("./times1.txt");
            FileWriter writer2 = new FileWriter("./times2.txt");

            counter = 0;

            while (!times1.isEmpty()) {
                long time = times1.poll();
                writer1.write(counter + ";" + time + "\n");
                counter++;
            }

            counter = 0;

            while (!times2.isEmpty()) {
                long time = times2.poll();
                writer2.write(counter + ";" + time + "\n");
                counter++;
            }

            writer1.close();
            writer2.close();
        } catch (IOException e) {
            System.out.println("error while writing");
            e.printStackTrace();
        }

    }
}
