# LakeVilla - Trino

1. Uncomment the trino-coordinator and trino-worker services in docker-compose.yml
2. Comment Spark out (they can't run on the same machine concurrently)
3. Build
4. docker exec -it lakevilla-trino-coordinator-1 trino
5. Create table:


sudo docker run -d   --name trino-coordinator   --network lakevillaNet   -p 8080:8080   -v "$(pwd)/Trino-Delta/coordinator/config.properties:/etc/trino/config.properties"   -v "$(pwd)/Trino-Delta/catalogs:/etc/trino/catalog"   trinodb/trino:latest

sudo docker run -d   --name trino-worker-1   --network lakevillaNet   -v "$(pwd)/Trino-Delta/worker/config.properties:/etc/trino/config.properties"   -v "$(pwd)/Trino-Delta/catalogs:/etc/trino/catalog"   trinodb/trino:latest

CREATE SCHEMA delta.default;

CREATE TABLE delta.default.usertable (
    ycsb_key VARCHAR,
    field0 VARCHAR, field1 VARCHAR, field2 VARCHAR, field3 VARCHAR, field4 VARCHAR,
    field5 VARCHAR, field6 VARCHAR, field7 VARCHAR, field8 VARCHAR, field9 VARCHAR
);


mvn -pl site.ycsb:jdbc-binding -am clean package

python2 bin/ycsb load jdbc -P workloads/workloada -P jdbc/config/trino.properties  -cp jdbc/lib/trino-jdbc-481.jar
python2 bin/ycsb run jdbc -P workloads/workloada -P jdbc/config/trino.properties  -cp jdbc/lib/trino-jdbc-481.jar

