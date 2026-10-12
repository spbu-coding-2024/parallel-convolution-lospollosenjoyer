import csv
import math
import statistics
import sys
from collections import defaultdict

from config import PIPELINE

groups = defaultdict(list)
with open(sys.argv[1], newline="") as stream:
    for row in csv.DictReader(stream):
        key = (int(row["kernel"]), row["mode"], int(row["workers"]), int(row["capacity"]))
        seconds = float(row["seconds"])
        peak = int(row["peak_in_flight"])
        if (
            int(row["side"]) != PIPELINE["image_side"]
            or int(row["count"]) != PIPELINE["batch_size"]
        ):
            raise ValueError(f"unexpected image or batch size: {row}")
        if not math.isfinite(seconds) or seconds <= 0:
            raise ValueError(f"invalid measurement: {row}")
        if not 1 <= peak <= min(key[3], PIPELINE["batch_size"]):
            raise ValueError(f"capacity exceeded: {row}")
        groups[key].append((int(row["run"]), seconds, peak))
expected = {
    (k, "pipeline", w, c)
    for k in PIPELINE["kernel_sides"]
    for w in PIPELINE["workers"]
    for c in PIPELINE["capacities"]
}
expected |= {(k, "serial", 1, 1) for k in PIPELINE["kernel_sides"]}
if groups.keys() != expected:
    raise ValueError("incomplete configurations")
writer = csv.writer(sys.stdout, lineterminator="\n")
writer.writerow(
    (
        "kernel",
        "mode",
        "workers",
        "capacity",
        "median_ms",
        "min_ms",
        "max_ms",
        "images_per_second",
        "speedup",
        "peak_in_flight",
        "runs",
    )
)
for key, values in sorted(groups.items()):
    if sorted(v[0] for v in values) != list(range(1, PIPELINE["runs"] + 1)):
        raise ValueError(f"missing/duplicate runs for {key}")
    baseline = groups[(key[0], "serial", 1, 1)]
    times = [v[1] for v in values]
    median = statistics.median(times)
    speedup = statistics.median(v[1] for v in baseline) / median
    writer.writerow(
        (
            *key,
            f"{median * 1000:.6f}",
            f"{min(times) * 1000:.6f}",
            f"{max(times) * 1000:.6f}",
            f"{PIPELINE['batch_size'] / median:.6f}",
            f"{speedup:.6f}",
            max(v[2] for v in values),
            len(values),
        )
    )
