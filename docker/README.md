# Docker Setup

contains all docker files for a simple test setup of Spark 3.5.0 and Delta Lake 3.0.0 + Spark 3.4.1 and Iceberg 1.3.1

## Starting the Setup

Create a new tmux tab:

``
tmux
``

Initialize the docker compose script:

``
docker compose up
``

Wait until all files are downloaded and all containers started sucessfully. Afterwards, simply detach from the tmux tab by pressing CTRL + B and D. 

You have to create the 'warehouse' bucket in MinIO. Access [localhost:80](http://localhost/login) in your browser and login with 'user' and 'password'.

## Accessing the SQL interface 

Instruct Docker to attach to the spark-delta3 container and execute spark-sql:

``
docker exec -it spark-delta3 spark-sql
``

Spark will download additional dependencies; just wait.

Afterwards, you can just use SQL to modify your spark database (https://spark.apache.org/docs/3.5.0/sql-ref-syntax.html). 

Example: Create a simple delta lake table 'test' in namespace 'tmp' and insert some data:

1. Create a namespace:
``
create namespace tm;
``

2. Use the namespace with the spark catalog:
``
use spark_catalog.tmp;
``

3. Create a Delta Lake table:
``
create table test (x int, y int) using delta;
``

4. Insert data:
``
insert into test values (1,2);
``

5. Query the table:
``
select * from test;
``

## MinIO interface

Should be acessible at: http://atkemper4.in.tum.de:9001 - please let me know if this does not work

## Stopping and deleteing the containers

Reatach to the tmux tab:

``
tmux attach -t <tab_id>
``

Press CTRL + C to stop the containers.

Delete them with:
``
docker compose down
``