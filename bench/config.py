import argparse
import json

BENCH = {"seed": 42}
SEQUENTIAL = {
    "image_sides": (256, 512, 1024, 2048),
    "kernel_sides": (3, 5, 9),
    "runs": 7,
}

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("format", choices=("c", "gnuplot"))
    args = parser.parse_args()
    if args.format == "c":
        print("#ifndef BENCH_CONFIG_H\n#define BENCH_CONFIG_H")
    for prefix, config in (
        ("BENCH", BENCH),
        ("SEQUENTIAL", SEQUENTIAL),
    ):
        for name, value in config.items():
            if isinstance(value, tuple):
                if args.format == "c":
                    literal = "{" + ", ".join(json.dumps(item) for item in value) + "}"
                else:
                    literal = json.dumps(" ".join(str(item) for item in value))
            else:
                literal = str(value)
            name = f"{prefix}_{name.upper()}"
            print(f"#define {name} {literal}" if args.format == "c" else f"{name} = {literal}")
    if args.format == "c":
        print("#endif")
