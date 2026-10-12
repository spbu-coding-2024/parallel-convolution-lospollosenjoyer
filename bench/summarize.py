import csv
import math
import statistics
import sys
from collections import defaultdict

from config import SEQUENTIAL

groups = defaultdict(list)
with open(sys.argv[1], newline="") as source:
    for row in csv.DictReader(source):
        key = tuple(int(row[name]) for name in ("width", "height", "kernel"))
        seconds = float(row["seconds"])
        checksum = float(row["checksum"])
        if not all(value > 0 for value in key) or key[2] % 2 == 0:
            raise ValueError(f"invalid dimensions: {row}")
        if not math.isfinite(seconds) or seconds <= 0 or not math.isfinite(checksum):
            raise ValueError(f"invalid measurement: {row}")
        groups[key].append((int(row["run"]), seconds, checksum))
expected = {
    (side, side, kernel)
    for side in SEQUENTIAL["image_sides"]
    for kernel in SEQUENTIAL["kernel_sides"]
}
if groups.keys() != expected:
    raise ValueError(
        f"unexpected configurations: missing={expected - groups.keys()}, "
        f"extra={groups.keys() - expected}"
    )
for key, samples in groups.items():
    if sorted(run for run, _, _ in samples) != list(range(1, SEQUENTIAL["runs"] + 1)):
        raise ValueError(f"missing or duplicate repetition for {key}: {samples}")
    if len({checksum for _, _, checksum in samples}) != 1:
        raise ValueError(f"different results between repetitions for {key}: {samples}")
writer = csv.writer(sys.stdout, lineterminator="\n")
writer.writerow(
    ("width", "height", "kernel", "median_ms", "min_ms", "max_ms", "mpixels_per_second")
)
for (width, height, kernel), samples in sorted(groups.items()):
    times = [seconds for _, seconds, _ in samples]
    median = statistics.median(times)
    writer.writerow(
        (
            width,
            height,
            kernel,
            f"{median * 1000:.6f}",
            f"{min(times) * 1000:.6f}",
            f"{max(times) * 1000:.6f}",
            f"{width * height / median / 1e6:.6f}",
        )
    )
