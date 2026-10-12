import csv
import math
import statistics
import sys
from collections import defaultdict

from config import PARALLEL

groups = defaultdict(list)
with open(sys.argv[1], newline="") as stream:
    for row in csv.DictReader(stream):
        key = (int(row["side"]), int(row["kernel"]), row["mode"], int(row["threads"]))
        value = float(row["seconds"])
        if not math.isfinite(value) or value <= 0:
            raise ValueError(row)
        groups[key].append((int(row["run"]), value))
expected = {
    (s, k, m, t)
    for s in PARALLEL["image_sides"]
    for k in PARALLEL["kernel_sides"]
    for m in ("serial", *PARALLEL["modes"])
    for t in ((1,) if m == "serial" else PARALLEL["threads"])
}
if groups.keys() != expected:
    raise ValueError("incomplete configuration set")
for key, values in groups.items():
    if sorted(v[0] for v in values) != list(range(1, PARALLEL["runs"] + 1)):
        raise ValueError(f"missing/duplicate runs for {key}")
writer = csv.writer(sys.stdout, lineterminator="\n")
writer.writerow(
    (
        "side",
        "kernel",
        "mode",
        "threads",
        "median_ms",
        "min_ms",
        "max_ms",
        "speedup",
        "min_speedup",
        "max_speedup",
        "runs",
    )
)
for key, values in sorted(groups.items()):
    times = [v[1] * 1000 for v in values]
    median = statistics.median(times)
    baseline = {v[0]: v[1] for v in groups[(*key[:2], "serial", 1)]}
    speedups = [baseline[run] / seconds for run, seconds in values]
    writer.writerow(
        (
            *key,
            f"{median:.6f}",
            f"{min(times):.6f}",
            f"{max(times):.6f}",
            f"{statistics.median(speedups):.6f}",
            f"{min(speedups):.6f}",
            f"{max(speedups):.6f}",
            len(values),
        )
    )
