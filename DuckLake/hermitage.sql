-- setup (local)
INSTALL ducklake;
INSTALL postgres;
LOAD ducklake;
LOAD postgres;
ATTACH 'ducklake:postgres:dbname=ducklake_catalog host=localhost user=duck password=duckpass' AS my_ducklake (DATA_PATH 'data_files/');
USE my_ducklake;
-- Note: primary key not supported
create table test (id int, value int);
insert into test (id, value) values (1, 10), (2, 20);

-- G0: Write Cycles (passed?)
BEGIN; -- T1
BEGIN; -- T2
update test set value = 11 where id = 1; -- T1
update test set value = 12 where id = 1; -- T2
update test set value = 21 where id = 2; -- T1
commit; -- T1
select * from test; -- T1. Shows 1 => 11, 2 => 21
update test set value = 22 where id = 2; -- T2
commit; -- T2 -> failure
select * from test; -- T1. Shows 1 => 11, 2 => 21

-- G1a: aborted reads (pass)
begin; -- T1
begin; -- T2
update test set value = 101 where id = 1; -- T1
select * from test; -- T2. Still shows 1 => 10
abort;  -- T1
select * from test; -- T2. Still shows 1 => 10
commit; -- T2

-- G1b: intermediated reads (pass)
begin; -- T1
begin; -- T2
update test set value = 101 where id = 1; -- T1
select * from test; -- T2. Still shows 1 => 10
update test set value = 11 where id = 1; -- T1
commit; -- T1
select * from test; -- T2. Still shows 1 => 10
commit; -- T2

-- G1c: circular Information Flow (pass, table level granularity)
begin; -- T1
begin; -- T2
update test set value = 11 where id = 1; -- T1
update test set value = 22 where id = 2; -- T2
select * from test where id = 2; -- T1. Still shows 2 => 20
select * from test where id = 1; -- T2. Still shows 1 => 10
commit; -- T1
commit; -- T2 -> failure

-- OTV: observed transaction vanishes (pass?)
begin; -- T1
begin; -- T2
begin; -- T3
update test set value = 11 where id = 1; -- T1
update test set value = 19 where id = 2; -- T1
update test set value = 12 where id = 1; -- T2
commit; -- T1.
select * from test where id = 1; -- T3. Shows 1 => 11
update test set value = 18 where id = 2; -- T2
select * from test where id = 2; -- T3. Shows 2 => 19
commit; -- T2 -> failure
select * from test where id = 2; -- T3. Shows 2 => 19
select * from test where id = 1; -- T3. Shows 1 => 11
commit; -- T3

-- PMP: predicate-many-preceders (pass)
begin; -- T1
begin; -- T2
select * from test where value = 30; -- T1. Returns nothing
insert into test (id, value) values(3, 30); -- T2
commit; -- T2
select * from test where value % 3 = 0; -- T1. Still returns nothing
commit; -- T1

-- PMP: write prediactes (pass)
begin; -- T1
begin; -- T2
update test set value = value + 10; -- T1
delete from test where value = 20;  -- T2
commit; -- T1
commit;  -- T2 -> failure: TransactionContext Error: Failed to commit: Failed to commit DuckLake transaction. Transaction conflict - attempting to delete from file with index "42" - but another transaction has deleted from it


-- P4: Lost update (pass)
begin; -- T1
begin; -- T2
select * from test where id = 1; -- T1
select * from test where id = 1; -- T2
update test set value = 11 where id = 1; -- T1
update test set value = 11 where id = 1; -- T2
commit; -- T1
commit; -- T2 -> failure

-- G-single: read skew (pass)
begin; -- T1
begin; -- T2
select * from test where id = 1; -- T1. Shows 1 => 10
select * from test where id = 1; -- T2
select * from test where id = 2; -- T2
update test set value = 12 where id = 1; -- T2
update test set value = 18 where id = 2; -- T2
commit; -- T2
select * from test where id = 2; -- T1. Shows 2 => 20
commit; -- T1

-- G-single predicate (pass)
begin;  -- T1
begin;  -- T2
select * from test where value % 5 = 0; -- T1
update test set value = 12 where value = 10; -- T2
commit; -- T2
select * from test where value % 3 = 0; -- T1. Returns nothing
commit; -- T1

--G-single write predicate
begin; -- T1
begin; -- T2
select * from test where id = 1; -- T1. Shows 1 => 10
select * from test; -- T2
update test set value = 12 where id = 1; -- T2
update test set value = 18 where id = 2; -- T2
commit; -- T2
delete from test where value = 20; -- T1. 
commit; -- T1 -> failire: TransactionContext Error: Failed to commit: Failed to commit DuckLake transaction.Transaction conflict - attempting to delete from file with index "28" - but another transaction has deleted from it'

-- G2-item: write skew
begin; -- T1
begin; -- T2
select * from test where id in (1,2); -- T1
select * from test where id in (1,2); -- T2
update test set value = 11 where id = 1; -- T1
update test set value = 21 where id = 2; -- T2
commit; -- T1
commit; -- T2 -> Failure: TransactionContext Error: Failed to commit: Failed to commit DuckLake transaction. Transaction conflict - attempting to delete from file with index "32" - but another transaction has deleted from it

-- G2: Anti-dependency cycles (failure)
begin; -- T1
begin; -- T2
select * from test where value % 3 = 0; -- T1
select * from test where value % 3 = 0; -- T2
insert into test (id, value) values(3, 30); -- T1
insert into test (id, value) values(4, 42); -- T2
commit; -- T1
commit; -- T2
select * from test where value % 3 = 0; -- Either. Returns 3 => 30, 4 => 42

-- G2 Fekete (pass)
begin; -- T1
select * from test; -- T1. Shows 1 => 10, 2 => 20
begin; -- T2
update test set value = value + 5 where id = 2; -- T2
commit; -- T2
begin; -- T3
select * from test; -- T3. Shows 1 => 10, 2 => 25
commit; -- T3
update test set value = 0 where id = 1; -- T1. 
commit; -- T1 -> failure