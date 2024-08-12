# LakeVilla - Spark freshness benchmark

This program is the driver for the freshness benchmark for Spark with Delta Lake 3.0 and Iceberg 1.4.2. 

## Configuration

### Credentials

Before you compile, please check all configurations in **src/main/java/com/freshness/Runner.java**. Set the URI and all credentials to your specific configuration.

### Decide on the format

Ensure that you use the correct pom.xml file for your open table format. Change the name of the file that suits your format to pom.xml:

- pom_delta.xml: For Spark + Delta Lake 3.0
- pom_iceberg.xml: For Spark + Iceberg 1.4.2

Remember to set the correct standard value in **src/main/java/com/freshness/Runner.java**!

### Compilation

You can compile this driver using Maven:

````mvn clean package````

### Running:

The **run.sh** script will start the benchmark with all necessary jars loaded. After the benchmark is finished, the results are available as follows:

- ./times1.txt: Duration between the write timestamp and the read query was issued (in nanoseconds)

- ./times2.txt: Duration between the write timestamp and the read query finished (in nanoseconds)
