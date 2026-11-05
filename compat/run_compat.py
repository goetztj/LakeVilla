#!/usr/bin/env python3
"""Compatibility harness: LakeVilla (ManagerDeltaLake) and Spark 3.5 / Delta 3.0.

Starts a private stack (MinIO, Spark, LakeVilla build environment) under its
own docker compose project, compiles compat-lv from LV/src, and runs scenarios
in which both engines read and write the same Delta tables. After every step
both engines must see the expected rows.

Results: PASS, FAIL, XFAIL (known limitation, see compat/README.md), XPASS (a
known limitation no longer shows: update the expectation) and ERROR (the
scenario could not run). The exit code is 1 if any FAIL, XPASS or ERROR
occurred.

usage: compat/run_compat.py [-k FILTER] [--levels none,01,2,012] [--keep]
                            [--no-build] [--project NAME] [--list]
"""
import argparse
import datetime
import json
import os
import queue
import re
import subprocess
import sys
import threading
import time
import traceback
import uuid
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
BUCKET = "compat"
DEFAULT_LEVELS = ["none", "01", "2", "012"]
BUILD_DIR = "/build"

COLORS = {"PASS": "\033[32m", "FAIL": "\033[31m", "ERROR": "\033[31m",
          "XFAIL": "\033[33m", "XPASS": "\033[35m"}


def log(msg):
    print(msg, flush=True)


# ---------------------------------------------------------------------------
# docker compose stack
# ---------------------------------------------------------------------------

class Stack:
    def __init__(self, project, logdir):
        self.project = project
        self.logdir = logdir

    def compose(self, *args, check=True, capture=True, timeout=None):
        cmd = ["docker", "compose", "-p", self.project,
               "-f", str(HERE / "docker-compose.yml"), *args]
        result = subprocess.run(cmd, cwd=HERE, text=True, timeout=timeout,
                                stdout=subprocess.PIPE if capture else None,
                                stderr=subprocess.STDOUT if capture else None)
        if check and result.returncode != 0:
            out = result.stdout[-4000:] if capture else ""
            raise RuntimeError(f"{' '.join(cmd)} failed:\n{out}")
        return result

    def build(self):
        log("building images (cached layers are reused) ...")
        result = self.compose("build", check=False, timeout=7200)
        (self.logdir / "build.log").write_text(result.stdout)
        if result.returncode != 0:
            raise RuntimeError("image build failed, see "
                               f"{self.logdir / 'build.log'}:\n"
                               + result.stdout[-3000:])

    def up(self):
        log(f"starting stack (project {self.project}) ...")
        self.compose("up", "-d", "--wait", timeout=600)
        self.compose("exec", "-T", "minio", "sh", "-c",
                     "mc alias set local http://localhost:9000 admin password"
                     f" >/dev/null && mc mb --ignore-existing local/{BUCKET}")

    def compile_lv(self):
        log("compiling compat-lv from LV/src ...")
        script = (
            f"cmake -S /src -B {BUILD_DIR} -G Ninja -DCMAKE_BUILD_TYPE=Release"
            " -DAWSSDK_ROOT_DIR=/LakeVilla/aws -DLV_BUILD_BENCHMARKS=OFF"
            " -DLV_BUILD_JNI=OFF -DLV_BUILD_TESTS=OFF -DLV_BUILD_COMPAT=ON"
            f" && cmake --build {BUILD_DIR} --target compat-lv")
        result = self.compose("exec", "-T", "lv", "bash", "-c", script,
                              check=False, timeout=3600)
        (self.logdir / "compile.log").write_text(result.stdout)
        if result.returncode != 0:
            raise RuntimeError("compiling compat-lv failed:\n"
                               + result.stdout[-3000:])

    def ls(self, prefix):
        """object keys below prefix (relative to the bucket)"""
        result = self.compose("exec", "-T", "minio", "mc", "ls", "--recursive",
                              f"local/{BUCKET}/{prefix}", check=False)
        keys = []
        for line in result.stdout.splitlines():
            parts = line.split()
            if parts and "STANDARD" in parts:
                keys.append(prefix + parts[-1])
        return keys

    def rm(self, keys):
        if keys:
            self.compose("exec", "-T", "minio", "mc", "rm", "--quiet",
                         *[f"local/{BUCKET}/{k}" for k in keys])

    def down(self):
        log(f"stopping stack (project {self.project}) ...")
        self.compose("down", "--remove-orphans", check=False, timeout=300)


# ---------------------------------------------------------------------------
# Spark (compat/spark/spark_server.py in the spark container)
# ---------------------------------------------------------------------------

class SparkError(Exception):
    pass


class Spark:
    def __init__(self, stack):
        self.stack = stack
        self.stderr = open(stack.logdir / "spark.log", "w")
        self.proc = None
        self.lines = queue.Queue()

    def start(self):
        log("starting Spark session (downloads the Delta jars on first use) ...")
        cmd = ["docker", "compose", "-p", self.stack.project,
               "-f", str(HERE / "docker-compose.yml"), "exec", "-T", "spark",
               "python3", "-u", "/compat/spark_server.py"]
        self.proc = subprocess.Popen(cmd, cwd=HERE, text=True, bufsize=1,
                                     stdin=subprocess.PIPE,
                                     stdout=subprocess.PIPE,
                                     stderr=self.stderr)
        threading.Thread(target=self._reader, daemon=True).start()
        ready = self._answer(timeout=900)
        if not ready.get("ready"):
            raise RuntimeError(f"Spark did not start: {ready}")
        log(f"Spark {ready['version']} ready")

    def _reader(self):
        for line in self.proc.stdout:
            if line.startswith("@@"):
                self.lines.put(json.loads(line[2:]))
        self.lines.put(None)

    def _answer(self, timeout):
        try:
            answer = self.lines.get(timeout=timeout)
        except queue.Empty:
            raise RuntimeError("Spark did not answer in time")
        if answer is None:
            raise RuntimeError("Spark server exited, see spark.log")
        return answer

    def _request(self, request, timeout):
        self.proc.stdin.write(json.dumps(request) + "\n")
        self.proc.stdin.flush()
        answer = self._answer(timeout)
        if not answer["ok"]:
            raise SparkError(answer["error"])
        return answer["rows"]

    def sql(self, statement, timeout=600):
        return self._request({"sql": statement}, timeout)

    def clear_cache(self):
        """drop the cached Delta snapshots of the session"""
        self._request({"clear_cache": True}, 60)

    def stop(self):
        if self.proc and self.proc.poll() is None:
            self.proc.stdin.close()
            try:
                self.proc.wait(timeout=60)
            except subprocess.TimeoutExpired:
                self.proc.kill()
        self.stderr.close()


# ---------------------------------------------------------------------------
# LakeVilla (compat-lv in the lv container)
# ---------------------------------------------------------------------------

class LvResult:
    def __init__(self, events, stderr):
        self.events = events
        self.stderr = stderr
        results = [e for e in events if e.get("event") == "result"]
        self.result = results[-1] if results else {"status": "error",
                                                   "error": "no result"}
        self.status = self.result["status"]

    def rows(self, event="rows"):
        for e in self.events:
            if e.get("event") == event:
                return e["rows"]
        return None

    def event(self, name):
        for e in self.events:
            if e.get("event") == name:
                return e
        return None

    def describe(self):
        text = f"status={self.status}"
        if self.result.get("error"):
            text += f" error={self.result['error']}"
        tail = [l for l in self.stderr.splitlines() if l.strip()][-6:]
        if tail and self.status not in ("committed", "ok"):
            text += " | stderr: " + " / ".join(tail)
        return text


def parse_events(stdout):
    events = []
    for line in stdout.splitlines():
        line = line.strip()
        if line.startswith("{"):
            try:
                events.append(json.loads(line))
            except json.JSONDecodeError:
                pass
    return events


class PausedTxn:
    """a compat-lv transaction waiting before its commit (--pause)"""

    def __init__(self, proc, logfile):
        self.proc = proc
        self.logfile = logfile
        self.events = []
        self.out = queue.Queue()
        threading.Thread(target=self._reader, daemon=True).start()

    def _reader(self):
        for line in self.proc.stdout:
            self.out.put(line)
        self.out.put(None)

    def wait_paused(self, timeout=120):
        deadline = time.time() + timeout
        while time.time() < deadline:
            try:
                line = self.out.get(timeout=max(0.1, deadline - time.time()))
            except queue.Empty:
                break
            if line is None:
                break
            self.events.extend(parse_events(line))
            if self.events and self.events[-1].get("event") == "paused":
                return True
        return False

    def finish(self, action="commit", timeout=300):
        try:
            self.proc.stdin.write(action + "\n")
            self.proc.stdin.flush()
        except BrokenPipeError:
            pass
        self.proc.wait(timeout=timeout)
        while True:
            line = self.out.get(timeout=30)
            if line is None:
                break
            self.events.extend(parse_events(line))
        self.logfile.seek(0)
        return LvResult(self.events, self.logfile.read())


class Lv:
    def __init__(self, stack):
        self.stack = stack

    def _cmd(self, options, args):
        return ["docker", "compose", "-p", self.stack.project,
                "-f", str(HERE / "docker-compose.yml"), "exec", "-T", "lv",
                f"{BUILD_DIR}/compat-lv", *options, *args]

    def run(self, options, *args, timeout=300):
        result = subprocess.run(self._cmd(options, args), cwd=HERE, text=True,
                                capture_output=True, timeout=timeout)
        return LvResult(parse_events(result.stdout), result.stderr)

    def start_paused(self, options, ops):
        logfile = open(self.stack.logdir / f"paused-{uuid.uuid4().hex}.log",
                       "w+")
        proc = subprocess.Popen(
            self._cmd(options + ["--pause"], ["txn", json.dumps(ops)]),
            cwd=HERE, text=True, bufsize=1, stdin=subprocess.PIPE,
            stdout=subprocess.PIPE, stderr=logfile)
        return PausedTxn(proc, logfile)


# ---------------------------------------------------------------------------
# checks
# ---------------------------------------------------------------------------

def norm_value(v):
    if isinstance(v, float):
        return round(v, 9)
    if isinstance(v, str):
        # decimals: "1.5" and "1.50" are equal
        if re.fullmatch(r"-?\d+\.\d+", v):
            return v.rstrip("0").rstrip(".") if "." in v else v
    return v


def norm_rows(rows):
    """rows as a sorted list of tuples of (column, value) pairs; column names
    are compared case-insensitively"""
    if rows is None:
        return None
    out = [tuple(sorted((k.lower(), norm_value(v)) for k, v in r.items()))
           for r in rows]
    return sorted(out, key=lambda t: json.dumps(t, default=str))


def diff_rows(actual, expected):
    if actual is None:
        return "no rows (read failed)"
    missing = [r for r in expected if r not in actual]
    extra = [r for r in actual if r not in expected]
    return f"missing={missing[:5]} extra={extra[:5]}"


class Scenario:
    def __init__(self, name, func, per_levels, only_levels, doc):
        self.name = name
        self.func = func
        self.per_levels = per_levels
        self.only_levels = only_levels
        self.doc = doc


SCENARIOS = []


def scenario(per_levels=False, only_levels=None):
    """per_levels: run for every LakeVilla level combination (--levels);
    only_levels: run only for these combinations (subset of --levels)"""
    def wrap(func):
        SCENARIOS.append(Scenario(func.__name__, func, per_levels, only_levels,
                                  (func.__doc__ or "").strip().splitlines()[0]))
        return func
    return wrap


class Context:
    def __init__(self, run, scenario_name, levels):
        self.run = run
        self.spark = run.spark
        self.lv_client = run.lv
        self.stack = run.stack
        self.levels = levels
        self.label = f"{scenario_name}[{levels}]"
        self.prefix = f"{run.run_id}/{scenario_name}-{levels}/"
        # level 2 shares tables with Spark only with these settings (README,
        # "Protocol settings"); scenarios that test the defaults clear it
        self.interop = True

    # ---- tables ---------------------------------------------------------
    def table(self, name):
        return self.prefix + name + "/"

    @staticmethod
    def uri(table):
        return f"delta.`s3a://{BUCKET}/{table.rstrip('/')}`"

    # ---- LakeVilla ------------------------------------------------------
    def lv_options(self, levels=None, extra=()):
        levels = levels or self.levels
        options = ["--levels", levels, "--global-log", self.prefix + "_global/"]
        if self.interop and "2" in levels:
            options += ["--check-at-begin", "--repair", "--repair-after", "1"]
        return options + list(extra)

    def lv(self, *args, levels=None, extra=()):
        return self.lv_client.run(self.lv_options(levels, extra), *args)

    def lv_txn(self, ops, levels=None, extra=()):
        return self.lv("txn", json.dumps(ops), levels=levels, extra=extra)

    def lv_paused(self, ops, levels=None, extra=()):
        return self.lv_client.start_paused(self.lv_options(levels, extra), ops)

    def lv_read(self, table, levels=None, extra=()):
        result = self.lv("read", table, levels=levels, extra=extra)
        return result.rows() if result.status == "ok" else None, result

    # ---- Spark ----------------------------------------------------------
    def sql(self, statement):
        return self.spark.sql(statement)

    def spark_read(self, table, where=""):
        return self.sql(f"SELECT * FROM {self.uri(table)} {where}")

    # ---- checks ---------------------------------------------------------
    def check(self, name, ok, detail="", xfail=None):
        self.run.record(self.label, name, bool(ok), detail, xfail)
        return bool(ok)

    def check_rows(self, name, actual, expected, xfail=None, detail=""):
        a, e = norm_rows(actual), norm_rows(expected)
        ok = a is not None and a == e
        return self.check(name, ok, detail if ok else
                          (detail + " " if detail else "") + diff_rows(a, e),
                          xfail)

    def check_lv_committed(self, name, result, xfail=None):
        return self.check(name, result.status == "committed",
                          result.describe(), xfail)

    def check_both(self, step, table, expected, lv_xfail=None,
                   spark_xfail=None, levels=None, extra=()):
        """both engines see exactly the expected rows"""
        try:
            spark_rows = self.spark_read(table)
            spark_detail = ""
        except SparkError as e:
            spark_rows, spark_detail = None, f"Spark error: {e}"
        self.check_rows(f"{step}: Spark reads expected rows", spark_rows,
                        expected, spark_xfail, spark_detail)
        lv_rows, result = self.lv_read(table, levels=levels, extra=extra)
        self.check_rows(f"{step}: LakeVilla reads expected rows", lv_rows,
                        expected, lv_xfail,
                        "" if lv_rows is not None else result.describe())


def lit(v):
    """SQL literal of a Python value (strings, numbers, booleans, None)"""
    if v is None:
        return "NULL"
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, (int, float)):
        return repr(v)
    return "'" + str(v).replace("'", "\\'") + "'"


def spark_insert(ctx, table, rows, columns, one_file=False):
    values = ", ".join("(" + ", ".join(lit(r.get(c)) for c in columns) + ")"
                       for r in rows)
    if one_file:
        # all rows in one data file (Spark otherwise writes one per task)
        ctx.sql(f"INSERT INTO {ctx.uri(table)} SELECT /*+ COALESCE(1) */ * "
                f"FROM VALUES {values} AS v({', '.join(columns)})")
    else:
        ctx.sql(f"INSERT INTO {ctx.uri(table)} VALUES {values}")


SIMPLE_SCHEMA_SQL = "id STRING, v BIGINT, note STRING"
SIMPLE_SCHEMA_LV = [{"name": "id", "type": "string"},
                    {"name": "v", "type": "long"},
                    {"name": "note", "type": "string"}]
SIMPLE_COLUMNS = ["id", "v", "note"]


def row(id_, v, note=None):
    return {"id": id_, "v": v, "note": note}


def spark_create_simple(ctx, table, properties=""):
    ctx.sql(f"CREATE TABLE {ctx.uri(table)} ({SIMPLE_SCHEMA_SQL}) USING DELTA"
            + (f" TBLPROPERTIES ({properties})" if properties else ""))


def lv_create_simple(ctx, table):
    result = ctx.lv("create", table, json.dumps(SIMPLE_SCHEMA_LV))
    if result.status != "ok":
        raise RuntimeError(f"LakeVilla create_table failed: {result.describe()}")


def checkpoint_versions(ctx, table):
    """versions of the classic checkpoints in the table's log"""
    keys = ctx.stack.ls(table + "_delta_log/")
    return sorted(int(k.rsplit("/", 1)[-1].split(".")[0]) for k in keys
                  if k.endswith(".checkpoint.parquet"))


def remove_log_before(ctx, table, version):
    """removes the commits (and checksums) before a checkpoint, as a log
    cleanup of Spark would; readers must start from the checkpoint"""
    keys = ctx.stack.ls(table + "_delta_log/")
    old = [k for k in keys
           if re.search(r"/\d{20}\.(json|crc)$", k)
           and int(k.rsplit("/", 1)[-1].split(".")[0]) < version]
    ctx.stack.rm(old)
    return len(old)


def model(*rows):
    return {r["id"]: dict(r) for r in rows}


# documented limitations
XFAIL_SPARK_CHECKPOINT = (
    "known bugs: a Spark checkpoint written while a marker is held "
    "loses the LakeVilla commit")
XFAIL_STALE_SPARK_SNAPSHOT = (
    "known bugs: a Spark session that read the marker keeps a "
    "snapshot without the LakeVilla commit")


def has_markers(levels):
    return "0" in levels or "1" in levels


# ---------------------------------------------------------------------------
# scenarios: reading tables of the other engine
# ---------------------------------------------------------------------------

TYPES_SQL = ("id STRING, i INT, l BIGINT, s SMALLINT, b TINYINT, f FLOAT, "
             "d DOUBLE, flag BOOLEAN, day DATE, dec DECIMAL(10,2)")
TYPES_LV = [{"name": "id", "type": "string"}, {"name": "i", "type": "integer"},
            {"name": "l", "type": "long"}, {"name": "s", "type": "short"},
            {"name": "b", "type": "byte"}, {"name": "f", "type": "float"},
            {"name": "d", "type": "double"}, {"name": "flag", "type": "boolean"},
            {"name": "day", "type": "date"},
            {"name": "dec", "type": "decimal(10,2)"}]
TYPES_ROWS = [
    {"id": "k1", "i": 1, "l": 10000000000, "s": 7, "b": -3, "f": 1.5,
     "d": 2.25, "flag": True, "day": "2024-02-29", "dec": "12.50"},
    {"id": "k2", "i": -2, "l": -1, "s": -32768, "b": 127, "f": -0.25,
     "d": 1e100, "flag": False, "day": "1970-01-01", "dec": "-0.01"},
    {"id": "k3", "i": None, "l": None, "s": None, "b": None, "f": None,
     "d": None, "flag": None, "day": None, "dec": None},
]


def types_sql_values(r):
    def one(c):
        v = r[c]
        if v is None:
            return "NULL"
        if c == "day":
            return f"DATE'{v}'"
        if c == "dec":
            return f"CAST('{v}' AS DECIMAL(10,2))"
        if c in ("s", "b", "f"):
            spark_type = {"s": "SMALLINT", "b": "TINYINT", "f": "FLOAT"}[c]
            return f"CAST({lit(v)} AS {spark_type})"
        return lit(v)
    return "(" + ", ".join(one(c["name"]) for c in TYPES_LV) + ")"


@scenario()
def spark_table_read_by_lv(ctx):
    """Spark creates and fills a table with all primitive types; LakeVilla reads it"""
    t = ctx.table("types")
    ctx.sql(f"CREATE TABLE {ctx.uri(t)} ({TYPES_SQL}) USING DELTA")
    for r in TYPES_ROWS:
        ctx.sql(f"INSERT INTO {ctx.uri(t)} VALUES {types_sql_values(r)}")
    ctx.check_both("Spark-written types", t, TYPES_ROWS)


@scenario()
def lv_table_read_by_spark(ctx):
    """LakeVilla creates (create_table) and fills a table; Spark reads it"""
    t = ctx.table("types")
    result = ctx.lv("create", t, json.dumps(TYPES_LV), "created by LakeVilla")
    ctx.check("create_table succeeds", result.status == "ok", result.describe())
    result = ctx.lv_txn([{"op": "insert", "table": t, "rows": TYPES_ROWS}])
    ctx.check_lv_committed("LakeVilla insert commits", result)
    ctx.check_both("LakeVilla-written types", t, TYPES_ROWS)

    detail = ctx.sql(f"DESCRIBE DETAIL {ctx.uri(t)}")[0]
    ctx.check("DESCRIBE DETAIL: format delta", detail["format"] == "delta",
              str(detail["format"]))
    ctx.check("DESCRIBE DETAIL: comment", detail["description"] ==
              "created by LakeVilla", str(detail["description"]))
    ctx.check("DESCRIBE DETAIL: one file per row",
              detail["numFiles"] == len(TYPES_ROWS), str(detail["numFiles"]))
    ctx.check("DESCRIBE DETAIL: protocol (1, 2)",
              (detail["minReaderVersion"], detail["minWriterVersion"]) ==
              (1, 2), f"{detail['minReaderVersion']}, "
              f"{detail['minWriterVersion']}")
    count = ctx.sql(f"SELECT count(*) AS c FROM {ctx.uri(t)}")[0]["c"]
    ctx.check("count(*) (answered from LakeVilla statistics)",
              count == len(TYPES_ROWS), str(count))
    rows = ctx.spark_read(t, "WHERE id = 'k2'")
    ctx.check_rows("data skipping with LakeVilla statistics", rows,
                   [TYPES_ROWS[1]])
    rows = ctx.spark_read(t, "WHERE l > 0")
    ctx.check_rows("filter on a long column", rows, [TYPES_ROWS[0]])


TS_SQL = "id STRING, ts TIMESTAMP"
TS_LV = [{"name": "id", "type": "string"}, {"name": "ts", "type": "timestamp"}]


@scenario()
def mixed_writers_timestamp(ctx):
    """Spark and LakeVilla both write a TIMESTAMP column of one table"""
    t = ctx.table("ts")
    ctx.sql(f"CREATE TABLE {ctx.uri(t)} ({TS_SQL}) USING DELTA")
    ctx.sql(f"INSERT INTO {ctx.uri(t)} VALUES "
            "('s1', TIMESTAMP'2024-01-01 12:00:00.123456')")
    expected = [{"id": "s1", "ts": "2024-01-01 12:00:00.123456"}]
    ctx.check_both("Spark row only", t, expected)
    lv_row = {"id": "l1", "ts": "1999-12-31 23:59:59.000001"}
    result = ctx.lv_txn([{"op": "insert", "table": t, "rows": [lv_row]}])
    ctx.check_lv_committed("LakeVilla insert commits", result)
    ctx.check_both("Spark (INT96) and LakeVilla (TIMESTAMP_MICROS) files", t,
                   expected + [lv_row])


@scenario()
def mixed_writers_timestamp_micros(ctx):
    """as mixed_writers_timestamp, Spark writes TIMESTAMP_MICROS instead of INT96"""
    t = ctx.table("ts")
    ctx.sql("SET spark.sql.parquet.outputTimestampType=TIMESTAMP_MICROS")
    try:
        ctx.sql(f"CREATE TABLE {ctx.uri(t)} ({TS_SQL}) USING DELTA")
        ctx.sql(f"INSERT INTO {ctx.uri(t)} VALUES "
                "('s1', TIMESTAMP'2024-01-01 12:00:00.123456')")
    finally:
        ctx.sql("SET spark.sql.parquet.outputTimestampType=INT96")
    lv_row = {"id": "l1", "ts": "1999-12-31 23:59:59.000001"}
    result = ctx.lv_txn([{"op": "insert", "table": t, "rows": [lv_row]}])
    ctx.check_lv_committed("LakeVilla insert commits", result)
    ctx.check_both("Spark and LakeVilla TIMESTAMP_MICROS files", t,
                   [{"id": "s1", "ts": "2024-01-01 12:00:00.123456"}, lv_row])


@scenario()
def timestamps_outside_nanosecond_range(ctx):
    """Spark (INT96) timestamps before 1677 and after 2262; LakeVilla rewrites"""
    t = ctx.table("ts")
    ctx.sql(f"CREATE TABLE {ctx.uri(t)} ({TS_SQL}, note STRING) USING DELTA")
    rows = [{"id": "old", "ts": "1500-01-01 00:00:00.000001", "note": None},
            {"id": "far", "ts": "9999-12-31 23:59:59.999999", "note": None},
            {"id": "now", "ts": "2024-02-29 12:00:00.123456", "note": None}]
    # Spark refuses to write INT96 values before 1900 unless they are written
    # in the proleptic Gregorian calendar (CORRECTED), as Arrow reads them
    ctx.sql("SET spark.sql.parquet.int96RebaseModeInWrite=CORRECTED")
    try:
        for r in rows:
            ctx.sql(f"INSERT INTO {ctx.uri(t)} VALUES ('{r['id']}', "
                    f"TIMESTAMP'{r['ts']}', NULL)")
    finally:
        ctx.sql("SET spark.sql.parquet.int96RebaseModeInWrite=EXCEPTION")
    ctx.check_both("INT96 timestamps of Spark", t, rows)
    # LakeVilla rewrites the INT96 file of a row (row update): Spark must be
    # able to read the new file (microseconds, not nanoseconds)
    result = ctx.lv_txn([{"op": "update", "table": t, "key": "old",
                          "set": {"note": "lv"}}])
    ctx.check_lv_committed("LakeVilla rewrites an INT96 file", result)
    if result.status == "committed":
        rows[0]["note"] = "lv"
    # Spark only knows the calendar of files that carry Spark's version in
    # their metadata; for other files it refuses values before 1582/1900
    ctx.check_both("after the rewrite", t, rows)
    ctx.sql("SET spark.sql.parquet.datetimeRebaseModeInRead=CORRECTED")
    try:
        try:
            spark_rows, detail = ctx.spark_read(t), ""
        except SparkError as e:
            spark_rows, detail = None, str(e)
        ctx.check_rows("Spark with datetimeRebaseModeInRead=CORRECTED reads "
                       "LakeVilla's values", spark_rows, rows, detail=detail)
    finally:
        ctx.sql("SET spark.sql.parquet.datetimeRebaseModeInRead=EXCEPTION")


# ---------------------------------------------------------------------------
# scenarios: interleaved writes
# ---------------------------------------------------------------------------

@scenario(per_levels=True)
def interleaved_writes(ctx):
    """Spark and LakeVilla take turns inserting, updating and deleting rows"""
    t = ctx.table("t")
    spark_create_simple(ctx, t)
    state = model()

    for k in ("k1", "k2"):
        spark_insert(ctx, t, [row(k, 1, "spark")], SIMPLE_COLUMNS)
        state[k] = row(k, 1, "spark")
    ctx.check_both("after Spark inserts", t, list(state.values()))

    result = ctx.lv_txn([{"op": "insert", "table": t,
                          "rows": [row("k3", 1, "lv")]}])
    ctx.check_lv_committed("LakeVilla insert commits", result)
    if result.status == "committed":
        state["k3"] = row("k3", 1, "lv")
    ctx.check_both("after LakeVilla insert", t, list(state.values()))

    spark_insert(ctx, t, [row("k4", 1, "spark")], SIMPLE_COLUMNS)
    state["k4"] = row("k4", 1, "spark")

    result = ctx.lv_txn([{"op": "update", "table": t, "key": "k1",
                          "set": {"v": 2, "note": "lv-update"}}])
    ctx.check_lv_committed("LakeVilla updates a Spark row", result)
    if result.status == "committed":
        state["k1"] = row("k1", 2, "lv-update")
    ctx.check_both("after LakeVilla update of a Spark row", t,
                   list(state.values()))

    ctx.sql(f"UPDATE {ctx.uri(t)} SET v = 3, note = 'spark-update' "
            "WHERE id = 'k3'")
    state["k3"] = row("k3", 3, "spark-update")
    ctx.check_both("after Spark update of a LakeVilla row", t,
                   list(state.values()))

    result = ctx.lv_txn([{"op": "delete", "table": t, "key": "k2"}])
    ctx.check_lv_committed("LakeVilla deletes a Spark row", result)
    if result.status == "committed":
        state.pop("k2")
    ctx.check_both("after LakeVilla delete", t, list(state.values()))

    ctx.sql(f"DELETE FROM {ctx.uri(t)} WHERE id = 'k3'")
    state.pop("k3")
    ctx.check_both("after Spark delete of a row LakeVilla wrote", t,
                   list(state.values()))

    # LakeVilla updates the row Spark rewrote (key lookup in a Spark file)
    result = ctx.lv_txn([{"op": "update", "table": t, "key": "k4",
                          "set": {"v": 5}}])
    ctx.check_lv_committed("LakeVilla updates a row again", result)
    if result.status == "committed":
        state["k4"]["v"] = 5
    ctx.check_both("final state", t, list(state.values()))


@scenario()
def history_and_time_travel(ctx):
    """Spark's DESCRIBE HISTORY and VERSION AS OF on LakeVilla commits"""
    t = ctx.table("t")
    lv_create_simple(ctx, t)
    snapshots = []
    state = model()
    for i in range(3):
        r = row(f"k{i}", i)
        result = ctx.lv_txn([{"op": "insert", "table": t, "rows": [r]}])
        ctx.check_lv_committed(f"LakeVilla commit {i}", result)
        state[r["id"]] = r
        version = result.result.get("versions", {}).get(t)
        snapshots.append((version, list(state.values())))

    history = ctx.sql(f"DESCRIBE HISTORY {ctx.uri(t)}")
    versions = sorted(h["version"] for h in history)
    ctx.check("history lists every version", versions ==
              list(range(len(versions))) and len(versions) >= 4,
              f"versions={versions}")
    for version, expected in snapshots:
        try:
            rows = ctx.sql(f"SELECT * FROM {ctx.uri(t)} VERSION AS OF "
                           f"{version}")
        except SparkError as e:
            ctx.check(f"VERSION AS OF {version}", False, str(e))
            continue
        ctx.check_rows(f"VERSION AS OF {version} (LakeVilla commit)", rows,
                       expected)


@scenario()
def multi_row_files(ctx):
    """LakeVilla row and file operations on multi-row files written by Spark"""
    t = ctx.table("t")
    spark_create_simple(ctx, t)
    rows = [row("a1", 1), row("a2", 2), row("a3", 3)]
    spark_insert(ctx, t, rows, SIMPLE_COLUMNS, one_file=True)
    files = ctx.sql(f"DESCRIBE DETAIL {ctx.uri(t)}")[0]["numFiles"]
    ctx.check("Spark wrote the three rows into one file", files == 1,
              f"numFiles={files}")

    # the first key of the file is its min value: found by the statistics
    result = ctx.lv_txn([{"op": "delete", "table": t, "key": "a1"}])
    ctx.check_lv_committed("LakeVilla deletes the file's minimum key",
                           result)
    ctx.check_both("other rows of the file are kept", t, rows[1:])

    rows = [row("b1", 1), row("b2", 2), row("b3", 3)]
    t2 = ctx.table("t2")
    spark_create_simple(ctx, t2)
    spark_insert(ctx, t2, rows, SIMPLE_COLUMNS, one_file=True)
    result = ctx.lv_txn([{"op": "remove-file", "table": t2, "key": "b3"}])
    ctx.check_lv_committed("LakeVilla remove_file(maximum key)", result)
    ctx.check_both("remove_file(key) keeps the other rows of the file", t2,
                   rows[:2])

    # a key between min and max of a file
    t3 = ctx.table("t3")
    spark_create_simple(ctx, t3)
    rows = [row("c1", 1), row("c2", 2), row("c3", 3)]
    spark_insert(ctx, t3, rows, SIMPLE_COLUMNS, one_file=True)
    result = ctx.lv_txn([{"op": "lookup", "table": t3, "key": "c2"}])
    ctx.check_rows("read_file_as_table returns the row of a middle key",
                   result.rows("lookup"), [rows[1]], detail=result.describe())
    result = ctx.lv_txn([{"op": "update", "table": t3, "key": "c2",
                          "set": {"v": 20}}])
    ctx.check_lv_committed("LakeVilla updates a key between min and max of "
                           "a file", result)
    if result.status == "committed":
        rows[1]["v"] = 20
    ctx.check_both("after the update of the middle key", t3, rows)


@scenario()
def key_in_several_files(ctx):
    """rows with the same key in several Spark files"""
    t = ctx.table("t")
    spark_create_simple(ctx, t)
    rows = [row("d", 1), row("d", 2), row("e", 3)]
    for r in rows:
        spark_insert(ctx, t, [r], SIMPLE_COLUMNS)
    result = ctx.lv_txn([{"op": "lookup", "table": t, "key": "d"}])
    ctx.check_rows("read_file_as_table returns the rows of all files",
                   result.rows("lookup"), rows[:2], detail=result.describe())
    result = ctx.lv_txn([{"op": "remove-file", "table": t, "key": "d"}])
    ctx.check_lv_committed("LakeVilla removes the key", result)
    ctx.check_both("all rows of the key are removed", t, rows[2:])


@scenario()
def spark_dml_and_optimize(ctx):
    """Spark UPDATE/DELETE/MERGE/OPTIMIZE on a LakeVilla table"""
    t = ctx.table("t")
    lv_create_simple(ctx, t)
    rows = [row(f"k{i}", i) for i in range(6)]
    result = ctx.lv_txn([{"op": "insert", "table": t, "rows": rows}])
    ctx.check_lv_committed("LakeVilla insert commits", result)
    state = model(*rows)

    ctx.sql(f"UPDATE {ctx.uri(t)} SET v = v + 100 WHERE id IN ('k1', 'k2')")
    for k in ("k1", "k2"):
        state[k]["v"] += 100
    ctx.sql(f"DELETE FROM {ctx.uri(t)} WHERE id = 'k3'")
    state.pop("k3")
    ctx.sql(f"MERGE INTO {ctx.uri(t)} AS target USING (SELECT 'k4' AS id, "
            "40 AS v, 'merged' AS note UNION ALL SELECT 'k9', 9, 'merged') "
            "AS source ON target.id = source.id WHEN MATCHED THEN UPDATE SET "
            "* WHEN NOT MATCHED THEN INSERT *")
    state["k4"] = row("k4", 40, "merged")
    state["k9"] = row("k9", 9, "merged")
    ctx.check_both("after Spark UPDATE/DELETE/MERGE", t, list(state.values()))

    result = ctx.lv_txn([{"op": "update", "table": t, "key": "k9",
                          "set": {"v": 99}}])
    ctx.check_lv_committed("LakeVilla updates a row MERGE inserted", result)
    if result.status == "committed":
        state["k9"]["v"] = 99

    ctx.sql(f"OPTIMIZE {ctx.uri(t)}")
    files = ctx.sql(f"DESCRIBE DETAIL {ctx.uri(t)}")[0]["numFiles"]
    ctx.check("OPTIMIZE compacts into one file", files == 1,
              f"numFiles={files}")
    ctx.check_both("after OPTIMIZE", t, list(state.values()))
    result = ctx.lv_txn([{"op": "insert", "table": t,
                          "rows": [row("k10", 10)]}])
    ctx.check_lv_committed("LakeVilla insert after OPTIMIZE", result)
    if result.status == "committed":
        state["k10"] = row("k10", 10)
    # keys inside the compacted file
    result = ctx.lv_txn([{"op": "update", "table": t, "key": "k5",
                          "set": {"v": 55}},
                         {"op": "delete", "table": t, "key": "k1"}])
    ctx.check_lv_committed("LakeVilla updates and deletes keys of the "
                           "compacted file", result)
    if result.status == "committed":
        state["k5"]["v"] = 55
        state.pop("k1")
    ctx.check_both("final state", t, list(state.values()))


# ---------------------------------------------------------------------------
# scenarios: checkpoints
# ---------------------------------------------------------------------------

@scenario()
def spark_checkpoint_read_by_lv(ctx):
    """LakeVilla reads and extends a table with a Spark checkpoint"""
    t = ctx.table("t")
    spark_create_simple(ctx, t)
    state = model()
    for i in range(12):
        r = row(f"k{i:02d}", i)
        spark_insert(ctx, t, [r], SIMPLE_COLUMNS)
        state[r["id"]] = r
    keys = ctx.stack.ls(t + "_delta_log/")
    ctx.check("Spark wrote a checkpoint (version 10)",
              any(k.endswith("00000000000000000010.checkpoint.parquet")
                  for k in keys), f"{len(keys)} log objects")
    ctx.check_both("snapshot from the Spark checkpoint", t,
                   list(state.values()))
    removed = remove_log_before(ctx, t, 10)
    ctx.spark.clear_cache()
    ctx.check_both(f"from the Spark checkpoint only ({removed} log objects "
                   "removed)", t, list(state.values()))
    result = ctx.lv_txn([{"op": "update", "table": t, "key": "k03",
                          "set": {"v": 33}},
                         {"op": "insert", "table": t,
                          "rows": [row("k99", 99)]}])
    ctx.check_lv_committed("LakeVilla commit after the checkpoint", result)
    if result.status == "committed":
        state["k03"]["v"] = 33
        state["k99"] = row("k99", 99)
    ctx.check_both("after LakeVilla commit", t, list(state.values()))


@scenario()
def lv_checkpoint_read_by_spark(ctx):
    """Spark reads and extends a table with a LakeVilla checkpoint"""
    t = ctx.table("t")
    lv_create_simple(ctx, t)
    state = model()
    for i in range(3):
        r = row(f"k{i}", i)
        ctx.lv_txn([{"op": "insert", "table": t, "rows": [r]}])
        state[r["id"]] = r
    ctx.lv_txn([{"op": "delete", "table": t, "key": "k0"}])
    state.pop("k0")
    result = ctx.lv("checkpoint", t)
    ctx.check("LakeVilla writes a checkpoint", result.status == "ok",
              result.describe())
    keys = ctx.stack.ls(t + "_delta_log/")
    ctx.check("checkpoint and _last_checkpoint exist",
              any(k.endswith(".checkpoint.parquet") for k in keys) and
              any(k.endswith("_last_checkpoint") for k in keys),
              ", ".join(k.rsplit("/", 1)[-1] for k in keys))
    ctx.check_both("snapshot from the LakeVilla checkpoint", t,
                   list(state.values()))
    spark_insert(ctx, t, [row("s1", 7)], SIMPLE_COLUMNS)
    state["s1"] = row("s1", 7)
    ctx.check_both("after Spark insert", t, list(state.values()))


@scenario(per_levels=True)
def lv_auto_checkpoint(ctx):
    """LakeVilla's commit of version 10 writes a checkpoint (default interval)"""
    t = ctx.table("t")
    spark_create_simple(ctx, t)
    state = model()
    # versions 1-4: Spark, versions 5-10: LakeVilla
    for i in range(4):
        r = row(f"s{i}", i, "spark")
        spark_insert(ctx, t, [r], SIMPLE_COLUMNS)
        state[r["id"]] = r
    last = None
    for i in range(6):
        r = row(f"l{i}", i, "lv")
        result = ctx.lv_txn([{"op": "insert", "table": t, "rows": [r]}])
        if not ctx.check_lv_committed(f"LakeVilla commit {i + 1} of 6",
                                      result):
            return
        state[r["id"]] = r
        last = result.result.get("versions", {}).get(t)
    ctx.check("the last LakeVilla commit is version 10", last == 10,
              f"version {last}")
    checkpoints = checkpoint_versions(ctx, t)
    ctx.check("LakeVilla wrote the checkpoint of version 10",
              10 in checkpoints, f"checkpoints: {checkpoints}")
    ctx.check("_last_checkpoint exists",
              any(k.endswith("_last_checkpoint")
                  for k in ctx.stack.ls(t + "_delta_log/")))
    ctx.check_both("with the full log", t, list(state.values()))

    removed = remove_log_before(ctx, t, 10)
    ctx.spark.clear_cache()
    ctx.check_both(f"from the checkpoint only ({removed} log objects "
                   "removed)", t, list(state.values()))
    detail = ctx.sql(f"DESCRIBE DETAIL {ctx.uri(t)}")[0]
    ctx.check("Spark: numFiles from the LakeVilla checkpoint",
              detail["numFiles"] == len(state), str(detail["numFiles"]))

    result = ctx.lv_txn([{"op": "update", "table": t, "key": "s1",
                          "set": {"v": 100}}])
    ctx.check_lv_committed("LakeVilla updates a Spark row found through the "
                           "checkpoint statistics", result)
    if result.status == "committed":
        state["s1"]["v"] = 100
    spark_insert(ctx, t, [row("s9", 9, "spark")], SIMPLE_COLUMNS)
    state["s9"] = row("s9", 9, "spark")
    ctx.check_both("after commits of both engines", t, list(state.values()))


@scenario()
def lv_checkpoint_partitioned(ctx):
    """LakeVilla checkpoint of a partitioned Spark table (partitionValues)"""
    t = ctx.table("t")
    ctx.sql(f"CREATE TABLE {ctx.uri(t)} (id STRING, v BIGINT, p STRING) "
            "USING DELTA PARTITIONED BY (p)")
    rows = [{"id": f"k{i}", "v": i, "p": "ab"[i % 2]} for i in range(4)]
    for r in rows:
        spark_insert(ctx, t, [r], ["id", "v", "p"])
    result = ctx.lv("checkpoint", t)
    ctx.check("LakeVilla writes a checkpoint of the partitioned table",
              result.status == "ok", result.describe())
    checkpoints = checkpoint_versions(ctx, t)
    if not ctx.check("checkpoint exists", bool(checkpoints),
                     f"checkpoints: {checkpoints}"):
        return
    remove_log_before(ctx, t, checkpoints[-1])
    ctx.spark.clear_cache()
    try:
        spark_rows, detail = ctx.spark_read(t), ""
    except SparkError as e:
        spark_rows, detail = None, str(e)
    ctx.check_rows("Spark reads partition values from the LakeVilla "
                   "checkpoint", spark_rows, rows, detail=detail)
    try:
        spark_rows = ctx.spark_read(t, "WHERE p = 'b'")
        detail = ""
    except SparkError as e:
        spark_rows, detail = None, str(e)
    ctx.check_rows("partition pruning with the LakeVilla checkpoint",
                   spark_rows, [r for r in rows if r["p"] == "b"],
                   detail=detail)


@scenario()
def lv_checkpoint_timestamp_ntz(ctx):
    """LakeVilla table with TIMESTAMP_NTZ (table feature) checkpointed at v10"""
    t = ctx.table("t")
    schema = [{"name": "id", "type": "string"},
              {"name": "ts", "type": "timestamp_ntz"}]
    result = ctx.lv("create", t, json.dumps(schema))
    ctx.check("create_table with timestamp_ntz", result.status == "ok",
              result.describe())
    state = []
    for i in range(10):
        r = {"id": f"k{i}", "ts": f"2024-01-0{i % 9 + 1} 10:00:00.00000{i}"}
        result = ctx.lv_txn([{"op": "insert", "table": t, "rows": [r]}])
        ctx.check_lv_committed(f"LakeVilla commit {i + 1}", result)
        state.append(r)
    checkpoints = checkpoint_versions(ctx, t)
    ctx.check("checkpoint of version 10", 10 in checkpoints,
              f"checkpoints: {checkpoints}")
    remove_log_before(ctx, t, 10)
    ctx.spark.clear_cache()
    ctx.check_both("from the checkpoint only", t, state)
    detail = ctx.sql(f"DESCRIBE DETAIL {ctx.uri(t)}")[0]
    ctx.check("protocol (3, 7) with timestampNtz",
              (detail["minReaderVersion"], detail["minWriterVersion"]) ==
              (3, 7) and "timestampNtz" in (detail.get("tableFeatures") or
                                            []),
              f"{detail['minReaderVersion']}, {detail['minWriterVersion']}, "
              f"{detail.get('tableFeatures')}")
    r = {"id": "s", "ts": "2030-01-01 00:00:00.000000"}
    ctx.sql(f"INSERT INTO {ctx.uri(t)} VALUES ('s', "
            "TIMESTAMP_NTZ'2030-01-01 00:00:00')")
    ctx.check_both("after a Spark insert", t, state + [r])


# ---------------------------------------------------------------------------
# scenarios: concurrency and protocol
# ---------------------------------------------------------------------------

@scenario(per_levels=True)
def spark_reads_during_lv_txn(ctx):
    """Spark reads while a LakeVilla transaction is open (marker placed)"""
    t = ctx.table("t")
    spark_create_simple(ctx, t)
    spark_insert(ctx, t, [row("k1", 1)], SIMPLE_COLUMNS)
    txn = ctx.lv_paused([{"op": "read", "table": t},
                         {"op": "insert", "table": t,
                          "rows": [row("lv", 2)]}])
    if not ctx.check("LakeVilla transaction reaches the pause",
                     txn.wait_paused(), "compat-lv did not pause"):
        txn.finish("abort")
        return
    try:
        rows = ctx.spark_read(t)
        detail = ""
    except SparkError as e:
        rows, detail = None, str(e)
    ctx.check_rows("Spark reads the committed state during the transaction",
                   rows, [row("k1", 1)], detail=detail)
    try:
        ctx.sql(f"DESCRIBE HISTORY {ctx.uri(t)}")
        ok, detail = True, ""
    except SparkError as e:
        ok, detail = False, str(e)
    ctx.check("DESCRIBE HISTORY with an open LakeVilla transaction", ok,
              detail)
    result = txn.finish("commit")
    ctx.check_lv_committed("LakeVilla commits", result)
    ctx.check_both("after the commit", t, [row("k1", 1), row("lv", 2)])


@scenario(per_levels=True)
def spark_commits_during_lv_txn(ctx):
    """Spark commits while a LakeVilla transaction that read the table is open"""
    t = ctx.table("t")
    spark_create_simple(ctx, t)
    spark_insert(ctx, t, [row("k1", 1)], SIMPLE_COLUMNS)
    txn = ctx.lv_paused([{"op": "read", "table": t},
                         {"op": "insert", "table": t,
                          "rows": [row("lv", 2)]}])
    if not ctx.check("LakeVilla transaction reaches the pause",
                     txn.wait_paused(), "compat-lv did not pause"):
        txn.finish("abort")
        return
    try:
        spark_insert(ctx, t, [row("spark", 3)], SIMPLE_COLUMNS)
        spark_ok, detail = True, ""
    except SparkError as e:
        spark_ok, detail = False, str(e)
    ctx.check("Spark commits while the LakeVilla transaction is open",
              spark_ok, detail)
    result = txn.finish("commit")
    expected = [row("k1", 1)] + ([row("spark", 3)] if spark_ok else [])
    if result.status == "committed":
        expected.append(row("lv", 2))
    ctx.check("LakeVilla commits or aborts cleanly",
              result.status in ("committed", "aborted"), result.describe())
    log(f"      LakeVilla outcome after the concurrent Spark commit: "
        f"{result.describe()}")
    # with markers, LakeVilla commits into the version its marker reserved,
    # below Spark's commit: a Spark session that already read the marker
    # version may keep a snapshot without the LakeVilla commit
    try:
        rows, detail = ctx.spark_read(t), ""
    except SparkError as e:
        rows, detail = None, str(e)
    ctx.check_rows("the running Spark session sees the LakeVilla commit",
                   rows, expected, detail=detail,
                   xfail=(XFAIL_STALE_SPARK_SNAPSHOT
                          if has_markers(ctx.levels) else None))
    ctx.spark.clear_cache()
    ctx.check_both("no lost update: a fresh Spark snapshot and LakeVilla "
                   "agree", t, expected)

    # the next LakeVilla transaction is not blocked
    result = ctx.lv_txn([{"op": "insert", "table": t,
                          "rows": [row("after", 4)]}])
    ctx.check_lv_committed("next LakeVilla transaction commits", result)
    if result.status == "committed":
        expected.append(row("after", 4))
    ctx.check_both("final state", t, expected)


@scenario(per_levels=True)
def spark_checkpoint_during_lv_txn(ctx):
    """Spark's commit of version 10 (default checkpoint) during a LakeVilla txn"""
    t = ctx.table("t")
    spark_create_simple(ctx, t)
    # with markers LakeVilla reserves version 9 and Spark commits version 10;
    # without markers Spark commits version 10 before LakeVilla's commit
    inserts = 8 if has_markers(ctx.levels) else 9
    expected = []
    for i in range(inserts):
        r = row(f"k{i}", i)
        spark_insert(ctx, t, [r], SIMPLE_COLUMNS)
        expected.append(r)
    txn = ctx.lv_paused([{"op": "insert", "table": t,
                          "rows": [row("lv", 2)]}])
    if not ctx.check("LakeVilla transaction reaches the pause",
                     txn.wait_paused(), "compat-lv did not pause"):
        txn.finish("abort")
        return
    spark_insert(ctx, t, [row("spark", 3)], SIMPLE_COLUMNS)
    expected.append(row("spark", 3))
    checkpoints = checkpoint_versions(ctx, t)
    ctx.check("Spark wrote the checkpoint of version 10", 10 in checkpoints,
              f"checkpoints: {checkpoints}")
    result = txn.finish("commit")
    ctx.check("LakeVilla commits or aborts cleanly",
              result.status in ("committed", "aborted"), result.describe())
    log(f"      LakeVilla outcome: {result.describe()}")
    if result.status == "committed":
        expected.append(row("lv", 2))
    ctx.spark.clear_cache()
    xfail = XFAIL_SPARK_CHECKPOINT if has_markers(ctx.levels) else None
    ctx.check_both("no lost update: a committed LakeVilla transaction "
                   "survives Spark's checkpoint", t, expected,
                   lv_xfail=xfail, spark_xfail=xfail)


@scenario(per_levels=True)
def spark_checkpoint_after_lv_txn(ctx):
    """a Spark session with a stale snapshot writes the next checkpoint"""
    t = ctx.table("t")
    # versions: 0 create, 1 Spark, 2 LakeVilla marker (levels 0/1), 3 Spark
    # during the transaction, 4 Spark again: checkpoint
    spark_create_simple(ctx, t, "'delta.checkpointInterval' = '4'")
    spark_insert(ctx, t, [row("k1", 1)], SIMPLE_COLUMNS)
    txn = ctx.lv_paused([{"op": "insert", "table": t,
                          "rows": [row("lv", 2)]}])
    if not ctx.check("LakeVilla transaction reaches the pause",
                     txn.wait_paused(), "compat-lv did not pause"):
        txn.finish("abort")
        return
    spark_insert(ctx, t, [row("spark", 3)], SIMPLE_COLUMNS)
    result = txn.finish("commit")
    log(f"      LakeVilla outcome: {result.describe()}")
    expected = [row("k1", 1), row("spark", 3)]
    if result.status == "committed":
        expected.append(row("lv", 2))
    # the same Spark session (no cache reset) commits again
    for i in range(2):
        spark_insert(ctx, t, [row(f"s{i}", 4 + i)], SIMPLE_COLUMNS)
        expected.append(row(f"s{i}", 4 + i))
    checkpoints = checkpoint_versions(ctx, t)
    ctx.check("Spark wrote a checkpoint after the LakeVilla commit",
              any(v >= 4 for v in checkpoints), f"checkpoints: {checkpoints}")
    ctx.spark.clear_cache()
    ctx.check_both("no lost update: the checkpoint contains the LakeVilla "
                   "commit", t, expected)


@scenario(only_levels=["01", "012"])
def crashed_lv_client(ctx):
    """a LakeVilla client crashes with a marker; Spark and LakeVilla continue"""
    t = ctx.table("t")
    spark_create_simple(ctx, t)
    spark_insert(ctx, t, [row("k1", 1)], SIMPLE_COLUMNS)
    lease = ["--lease-ms", "3000", "--grace-ms", "100"]
    result = ctx.lv_txn([{"op": "insert", "table": t,
                          "rows": [row("lost", 0)]}],
                        extra=lease + ["--crash"])
    ctx.check("LakeVilla client crashed before its commit",
              result.status == "crashed", result.describe())
    ctx.check_both("crashed transaction is invisible", t, [row("k1", 1)])
    try:
        spark_insert(ctx, t, [row("spark", 2)], SIMPLE_COLUMNS)
        ok, detail = True, ""
    except SparkError as e:
        ok, detail = False, str(e)
    ctx.check("Spark commits next to the stale marker", ok, detail)
    expected = [row("k1", 1)] + ([row("spark", 2)] if ok else [])
    time.sleep(3.5)
    result = ctx.lv_txn([{"op": "insert", "table": t,
                          "rows": [row("lv", 3)]}], extra=lease)
    ctx.check_lv_committed("next LakeVilla transaction commits after the "
                           "lease", result)
    if result.status == "committed":
        expected.append(row("lv", 3))
    ctx.check_both("final state", t, expected)


@scenario(only_levels=["2"])
def lv_read_with_foreign_commits(ctx):
    """LV[Read] (global log) and Spark commits: repair and check at begin"""
    ctx.interop = False
    t = ctx.table("t")
    lv_create_simple(ctx, t)
    result = ctx.lv_txn([{"op": "insert", "table": t,
                          "rows": [row("lv1", 1)]}])
    ctx.check_lv_committed("LakeVilla commit (global log entry)", result)
    spark_insert(ctx, t, [row("spark", 2)], SIMPLE_COLUMNS)
    expected = [row("lv1", 1), row("spark", 2)]

    rows, _ = ctx.lv_read(t)
    ctx.check_rows("documented: LV[Read] does not see the Spark commit",
                   rows, [row("lv1", 1)])
    rows, _ = ctx.lv_read(t, extra=["--check-at-begin"])
    ctx.check_rows("GLOBAL_LOG_CHECK_AT_BEGIN: the Spark commit is visible",
                   rows, expected)

    result = ctx.lv_txn([{"op": "insert", "table": t,
                          "rows": [row("lv2", 3)]}])
    ctx.check("documented: without repair the commit aborts",
              result.status == "aborted", result.describe())
    result = ctx.lv_txn([{"op": "insert", "table": t,
                          "rows": [row("lv3", 4)]}],
                        extra=["--repair", "--repair-after", "1"])
    ctx.check_lv_committed("GLOBAL_LOG_REPAIR: the commit succeeds", result)
    if result.status == "committed":
        expected.append(row("lv3", 4))
    ctx.check_both("final state", t, expected)


@scenario(per_levels=True)
def multi_table_transaction(ctx):
    """a LakeVilla transaction over two Spark tables (transfer)"""
    a, b = ctx.table("a"), ctx.table("b")
    for t in (a, b):
        spark_create_simple(ctx, t)
        spark_insert(ctx, t, [row("acc", 100)], SIMPLE_COLUMNS)
    result = ctx.lv_txn([
        {"op": "read", "table": a}, {"op": "read", "table": b},
        {"op": "update", "table": a, "key": "acc", "set": {"v": 90}},
        {"op": "update", "table": b, "key": "acc", "set": {"v": 110}}])
    ctx.check_lv_committed("transfer commits", result)
    ctx.check_both("table a", a, [row("acc", 90)])
    ctx.check_both("table b", b, [row("acc", 110)])
    total = ctx.sql(f"SELECT sum(v) AS s FROM (SELECT v FROM {ctx.uri(a)} "
                    f"UNION ALL SELECT v FROM {ctx.uri(b)})")[0]["s"]
    ctx.check("Spark: the sum over both tables is unchanged", total == 200,
              str(total))


# ---------------------------------------------------------------------------
# scenarios: table management
# ---------------------------------------------------------------------------

@scenario()
def lv_create_and_drop(ctx):
    """create_table/drop_table of LakeVilla and Spark's view of them"""
    t = ctx.table("t")
    schema = [{"name": "id", "type": "string", "nullable": False,
               "comment": "the key"},
              {"name": "v", "type": "long"}]
    result = ctx.lv("create", t, json.dumps(schema), "lv table")
    ctx.check("create_table", result.status == "ok", result.describe())
    result = ctx.lv("create", t, json.dumps(schema))
    ctx.check("create_table refuses an existing table",
              result.status == "already_exists", result.describe())
    columns = {r["col_name"]: r for r in
               ctx.sql(f"DESCRIBE TABLE {ctx.uri(t)}")}
    ctx.check("Spark sees the columns and the column comment",
              "id" in columns and columns["id"].get("comment") == "the key"
              and columns.get("v", {}).get("data_type") == "bigint",
              str(list(columns.values())[:2]))
    try:
        ctx.sql(f"INSERT INTO {ctx.uri(t)} VALUES (NULL, 1)")
        ok = False
    except SparkError:
        ok = True
    ctx.check("Spark enforces NOT NULL of a LakeVilla table", ok)
    spark_insert(ctx, t, [{"id": "s", "v": 1}], ["id", "v"])
    result = ctx.lv_txn([{"op": "insert", "table": t,
                          "rows": [{"id": "l", "v": 2}]}])
    ctx.check_lv_committed("LakeVilla insert", result)
    ctx.check_both("rows of both engines", t,
                   [{"id": "s", "v": 1}, {"id": "l", "v": 2}])

    result = ctx.lv("drop", t)
    ctx.check("drop_table", result.status == "ok", result.describe())
    ctx.check("no objects are left", ctx.stack.ls(t) == [],
              str(ctx.stack.ls(t)[:5]))
    try:
        ctx.spark_read(t)
        ok, detail = False, "table still readable"
    except SparkError as e:
        ok, detail = True, str(e)[:120]
    ctx.check("Spark no longer finds the table", ok, detail)
    spark_create_simple(ctx, t)
    spark_insert(ctx, t, [row("new", 1)], SIMPLE_COLUMNS)
    ctx.check_both("the location can be reused", t, [row("new", 1)])


@scenario()
def spark_schema_evolution(ctx):
    """Spark adds a column (ALTER TABLE ADD COLUMNS) to a table with data"""
    t = ctx.table("t")
    spark_create_simple(ctx, t)
    spark_insert(ctx, t, [row("k1", 1)], SIMPLE_COLUMNS)
    ctx.sql(f"ALTER TABLE {ctx.uri(t)} ADD COLUMNS (extra STRING)")
    ctx.sql(f"INSERT INTO {ctx.uri(t)} VALUES ('k2', 2, NULL, 'x')")
    expected = [dict(row("k1", 1), extra=None),
                dict(row("k2", 2), extra="x")]
    ctx.check_both("files with and without the new column", t, expected)
    result = ctx.lv_txn([{"op": "insert", "table": t,
                          "rows": [dict(row("k3", 3), extra="lv")]}])
    ctx.check_lv_committed("LakeVilla writes with the evolved schema", result)
    expected.append(dict(row("k3", 3), extra="lv"))
    # the file of k1 predates the column: the rewrite adds it
    result = ctx.lv_txn([{"op": "update", "table": t, "key": "k1",
                          "set": {"extra": "added"}}])
    ctx.check_lv_committed("LakeVilla updates a row of an older file", result)
    if result.status == "committed":
        expected[0]["extra"] = "added"
    ctx.check_both("after LakeVilla's writes", t, expected)


# ---------------------------------------------------------------------------
# scenarios: known limitations and features LakeVilla only reads
# ---------------------------------------------------------------------------

@scenario()
def long_string_keys(ctx):
    """keys longer than 32 characters (Spark truncates string statistics)"""
    t = ctx.table("t")
    spark_create_simple(ctx, t)
    key = "k" * 40
    spark_insert(ctx, t, [row(key, 1)], SIMPLE_COLUMNS)
    result = ctx.lv_txn([{"op": "update", "table": t, "key": key,
                          "set": {"v": 2}}])
    ctx.check_lv_committed("LakeVilla finds a 40-character key in a Spark "
                           "file (truncated statistics)", result)
    ctx.check_both("after the update", t, [row(key, 2 if result.status ==
                                               "committed" else 1)])
    lv_key = "l" * 40
    result = ctx.lv_txn([{"op": "insert", "table": t,
                          "rows": [row(lv_key, 1)]}])
    result = ctx.lv_txn([{"op": "update", "table": t, "key": lv_key,
                          "set": {"v": 2}}])
    ctx.check_lv_committed("LakeVilla finds a 40-character key in its own "
                           "file", result)


@scenario()
def deletion_vectors(ctx):
    """a Spark DELETE that writes a deletion vector"""
    t = ctx.table("t")
    spark_create_simple(ctx, t, "'delta.enableDeletionVectors' = 'true'")
    rows = [row("k1", 1), row("k2", 2), row("k3", 3)]
    spark_insert(ctx, t, rows, SIMPLE_COLUMNS, one_file=True)
    ctx.sql(f"DELETE FROM {ctx.uri(t)} WHERE id = 'k2'")
    expected = [rows[0], rows[2]]
    ctx.check_both("rows deleted by a deletion vector", t, expected,
                   lv_xfail="deletion vectors are not supported (LakeVilla "
                            "drops the file Spark re-added with a deletion "
                            "vector, so remaining rows disappear)")
    result = ctx.lv_txn([{"op": "insert", "table": t,
                          "rows": [row("k4", 4)]}])
    ctx.check("LakeVilla refuses to write to a table with deletion vectors",
              result.status != "committed", result.describe(),
              xfail="writes to tables with unsupported features are not "
                    "refused")


@scenario()
def partitioned_table(ctx):
    """a Spark table partitioned by a column"""
    t = ctx.table("t")
    ctx.sql(f"CREATE TABLE {ctx.uri(t)} (id STRING, v BIGINT, p STRING) "
            "USING DELTA PARTITIONED BY (p)")
    rows = [{"id": "k1", "v": 1, "p": "a"}, {"id": "k2", "v": 2, "p": "b"}]
    for r in rows:
        spark_insert(ctx, t, [r], ["id", "v", "p"])
    ctx.check_both("partition values from the log", t, rows)
    result = ctx.lv_txn([{"op": "insert", "table": t,
                          "rows": [{"id": "k3", "v": 3, "p": "c"}]}])
    ctx.check("LakeVilla refuses to write a partitioned table",
              result.status == "aborted", result.describe())
    ctx.check_both("unchanged after the refused write", t, rows)


@scenario()
def column_mapping(ctx):
    """a Spark table with column mapping (mode name) and a renamed column"""
    t = ctx.table("t")
    spark_create_simple(ctx, t, "'delta.columnMapping.mode' = 'name', "
                        "'delta.minReaderVersion' = '2', "
                        "'delta.minWriterVersion' = '5'")
    spark_insert(ctx, t, [row("k1", 1)], SIMPLE_COLUMNS)
    ctx.sql(f"ALTER TABLE {ctx.uri(t)} RENAME COLUMN note TO comment")
    expected = [{"id": "k1", "v": 1, "comment": None}]
    ctx.check_both("physical column names", t, expected)
    result = ctx.lv_txn([{"op": "insert", "table": t,
                          "rows": [{"id": "k2", "v": 2, "comment": "lv"}]}])
    ctx.check("LakeVilla refuses to write a table with column mapping",
              result.status == "aborted", result.describe())
    ctx.check_both("unchanged after the refused write", t, expected)


# ---------------------------------------------------------------------------
# runner
# ---------------------------------------------------------------------------

class Run:
    def __init__(self, args):
        self.args = args
        self.run_id = "run-" + datetime.datetime.now().strftime(
            "%Y%m%d-%H%M%S") + "-" + uuid.uuid4().hex[:6]
        self.logdir = HERE / "logs" / self.run_id
        self.logdir.mkdir(parents=True, exist_ok=True)
        self.stack = Stack(args.project, self.logdir)
        self.spark = None
        self.lv = Lv(self.stack)
        self.results = []

    def record(self, label, name, ok, detail, xfail):
        if xfail:
            status = "XPASS" if ok else "XFAIL"
        else:
            status = "PASS" if ok else "FAIL"
        self.results.append((status, label, name, detail, xfail))
        color = COLORS.get(status, "") if sys.stdout.isatty() else ""
        reset = "\033[0m" if color else ""
        line = f"  {color}{status:5}{reset} {name}"
        if status != "PASS" and detail:
            line += f"\n        {detail}"
        if status in ("XFAIL", "XPASS"):
            line += f"\n        known limitation: {xfail}"
        log(line)

    def selected(self):
        levels = self.args.levels
        for s in SCENARIOS:
            if self.args.k and not any(f in s.name for f in self.args.k):
                continue
            if s.only_levels:
                combos = [l for l in s.only_levels if l in levels]
            elif s.per_levels:
                combos = levels
            else:
                combos = [self.args.default_levels]
            for combo in combos:
                yield s, combo

    def execute(self):
        if not (ROOT / "LV/src/rapidjson/include/rapidjson/document.h").exists():
            log("initialising the rapidjson submodule ...")
            subprocess.run(["git", "submodule", "update", "--init",
                            "LV/src/rapidjson"], cwd=ROOT, check=True)
        if not self.args.no_build:
            self.stack.build()
        self.stack.up()
        self.stack.compile_lv()
        self.spark = Spark(self.stack)
        self.spark.start()
        log(f"tables: s3://{BUCKET}/{self.run_id}/, logs: {self.logdir}")
        for s, combo in self.selected():
            log(f"\n== {s.name} [levels {combo}]: {s.doc}")
            ctx = Context(self, s.name, combo)
            started = time.time()
            try:
                s.func(ctx)
            except Exception as e:  # noqa: BLE001 - reported as ERROR
                self.results.append(("ERROR", ctx.label, "scenario", str(e),
                                     None))
                log(f"  ERROR {type(e).__name__}: {e}")
                (self.logdir / f"{ctx.label}.traceback").write_text(
                    traceback.format_exc())
            log(f"  ({time.time() - started:.1f}s)")

    def summary(self):
        counts = {}
        for status, *_ in self.results:
            counts[status] = counts.get(status, 0) + 1
        log("\n== summary: " + ", ".join(
            f"{counts.get(s, 0)} {s}" for s in
            ("PASS", "FAIL", "XFAIL", "XPASS", "ERROR")))
        for status, label, name, detail, _ in self.results:
            if status in ("FAIL", "XPASS", "ERROR"):
                log(f"  {status:5} {label}: {name}")
        report = [{"status": s, "scenario": l, "check": n, "detail": d,
                   "known_limitation": x} for s, l, n, d, x in self.results]
        (self.logdir / "results.json").write_text(json.dumps(report,
                                                             indent=2))
        log(f"results: {self.logdir / 'results.json'}")
        bad = sum(counts.get(s, 0) for s in ("FAIL", "XPASS", "ERROR"))
        return 1 if bad or not self.results else 0


def main():
    parser = argparse.ArgumentParser(
        description="LakeVilla / Spark Delta Lake compatibility harness")
    parser.add_argument("-k", action="append",
                        help="run scenarios whose name contains this text "
                             "(repeatable)")
    parser.add_argument("--levels", default=",".join(DEFAULT_LEVELS),
                        help="LakeVilla level combinations for the per-level "
                             "scenarios (default: %(default)s)")
    parser.add_argument("--default-levels", default="01",
                        help="levels of the other scenarios "
                             "(default: %(default)s, LV[Write])")
    parser.add_argument("--project", default="lv-compat",
                        help="docker compose project name "
                             "(default: %(default)s)")
    parser.add_argument("--no-build", action="store_true",
                        help="do not rebuild the images")
    parser.add_argument("--keep", action="store_true",
                        help="leave the stack running afterwards")
    parser.add_argument("--list", action="store_true",
                        help="list the scenarios and exit")
    args = parser.parse_args()
    args.levels = [l.strip() for l in args.levels.split(",") if l.strip()]

    if args.list:
        for s in SCENARIOS:
            scope = ("levels " + ",".join(s.only_levels) if s.only_levels
                     else "per level" if s.per_levels else "once")
            print(f"{s.name:32} [{scope}] {s.doc}")
        return 0

    run = Run(args)
    try:
        run.execute()
    except Exception as e:  # noqa: BLE001
        log(f"\nharness error: {e}")
        run.results.append(("ERROR", "harness", "setup", str(e), None))
    finally:
        if run.spark:
            run.spark.stop()
        if not args.keep:
            run.stack.down()
    return run.summary()


if __name__ == "__main__":
    sys.exit(main())
