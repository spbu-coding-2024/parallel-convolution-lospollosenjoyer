from datetime import datetime
from pathlib import Path
import platform
import shlex
import subprocess
import sys

cc, cppflags, cflags, ldflags, ldlibs = sys.argv[1:]
cpu = platform.processor() or platform.machine()
cpuinfo = Path("/proc/cpuinfo")
if cpuinfo.exists():
    cpu = next(
        (
            line.split(":", 1)[1].strip()
            for line in cpuinfo.read_text().splitlines()
            if line.startswith("model name")
        ),
        cpu,
    )
compiler = subprocess.check_output(shlex.split(cc) + ["--version"], text=True)
print("Date:", datetime.now().astimezone().isoformat(timespec="seconds"))
print("CPU:", cpu)
print("OS:", platform.platform())
print("Compiler:", compiler.splitlines()[0])
for name, value in zip(("CC", "CPPFLAGS", "CFLAGS", "LDFLAGS", "LDLIBS"), sys.argv[1:]):
    print(f"{name}: {value}")
print("Python:", platform.python_version())
print(subprocess.check_output(["gnuplot", "--version"], text=True).strip())
