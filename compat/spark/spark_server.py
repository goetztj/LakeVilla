"""Spark side of the compatibility harness (run inside the Spark container).

Keeps one local SparkSession with Delta Lake (spark-defaults.conf of the
Spark image) and executes SQL statements read from stdin, one JSON object per
line: {"sql": "..."}, or {"clear_cache": true} to drop the cached Delta
snapshots (DeltaLog.clearCache). Every answer is one line on stdout prefixed with "@@":
{"ok": true, "columns": [...], "rows": [{...}, ...]} or
{"ok": false, "error": "..."}. Values: timestamps "YYYY-MM-DD HH:MM:SS.ffffff"
(UTC), dates "YYYY-MM-DD", decimals as strings (the format of compat-lv).
"""
import datetime
import decimal
import json
import math
import os
import sys
import time

os.environ["TZ"] = "UTC"
time.tzset()

from pyspark.sql import SparkSession  # noqa: E402


def value(v):
    if isinstance(v, datetime.datetime):
        return v.strftime("%Y-%m-%d %H:%M:%S.%f")
    if isinstance(v, datetime.date):
        return v.isoformat()
    if isinstance(v, decimal.Decimal):
        return str(v)
    if isinstance(v, (bytes, bytearray)):
        return bytes(v).decode("latin-1")
    if isinstance(v, float) and not math.isfinite(v):
        return "NaN" if math.isnan(v) else ("Infinity" if v > 0 else "-Infinity")
    if isinstance(v, dict):
        return {k: value(x) for k, x in v.items()}
    if isinstance(v, list):
        return [value(x) for x in v]
    return v


def answer(obj):
    sys.stdout.write("@@" + json.dumps(obj) + "\n")
    sys.stdout.flush()


def main():
    spark = (
        SparkSession.builder.master("local[2]")
        .appName("lv-compat")
        .config("spark.sql.catalogImplementation", "in-memory")
        .config("spark.sql.session.timeZone", "UTC")
        # tables are accessed by path; the catalog stays local
        .config("spark.sql.warehouse.dir", "/tmp/spark-warehouse")
        .config("spark.sql.catalog.spark_catalog.warehouse",
                "/tmp/spark-warehouse")
        .config("spark.hadoop.fs.s3a.endpoint", "http://minio:9000")
        .config("spark.hadoop.fs.s3a.path.style.access", "true")
        .config("spark.ui.enabled", "false")
        .config("spark.eventLog.enabled", "false")
        .getOrCreate()
    )
    spark.sparkContext.setLogLevel("ERROR")
    spark.sql("SELECT 1").collect()
    answer({"ok": True, "ready": True, "version": spark.version})

    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            request = json.loads(line)
            if request.get("clear_cache"):
                # forget cached Delta snapshots, as a new Spark session would
                spark._jvm.org.apache.spark.sql.delta.DeltaLog.clearCache()
                answer({"ok": True, "columns": [], "rows": []})
                continue
            df = spark.sql(request["sql"])
            rows = [value(r.asDict(recursive=True)) for r in df.collect()]
            answer({"ok": True, "columns": df.columns, "rows": rows})
        except Exception as e:  # noqa: BLE001 - reported to the harness
            java = getattr(e, "java_exception", None)
            text = java.toString() if java is not None else str(e)
            message = text.strip().splitlines()
            answer({"ok": False, "error": message[0] if message else repr(e),
                    "type": type(e).__name__})


if __name__ == "__main__":
    main()
