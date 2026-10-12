CC = cc
CPPFLAGS = -Iinclude -isystem vendor/stb -D_POSIX_C_SOURCE=200809L
CFLAGS = -std=c17 -O3 -Wall -Wextra -Wpedantic -Wconversion -Wshadow
LDLIBS = -lm
BUILD = build
BENCH = $(BUILD)/bench/sequential
CORE = src/convolution.c
IO = src/io.c
HEADERS = include/convolution.h include/io.h
STB_HEADERS = vendor/stb/stb_image.h vendor/stb/stb_image_write.h
FORMAT_FILES = $(wildcard src/*.[ch] include/*.h tests/*.c bench/*.c)

.PHONY: all test sanitize tidy format format-check benchmark benchmark-save clean
all: $(BUILD)/convolve

$(BUILD):
	mkdir -p $@

$(BUILD)/bench_config.h: bench/config.py | $(BUILD)
	python3 $< c > "$@"

$(BUILD)/stb.o: src/stb.c $(STB_HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c src/stb.c -o $@

$(BUILD)/convolve: src/main.c $(CORE) $(IO) $(HEADERS) $(BUILD)/stb.o | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) src/main.c $(CORE) $(IO) $(BUILD)/stb.o $(LDFLAGS) $(LDLIBS) -o $@

$(BUILD)/test_convolution: tests/test_convolution.c $(CORE) include/convolution.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_convolution.c $(CORE) $(LDFLAGS) $(LDLIBS) -o $@

$(BUILD)/test_io: tests/test_io.c $(CORE) $(IO) $(HEADERS) $(BUILD)/stb.o | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_io.c $(CORE) $(IO) $(BUILD)/stb.o $(LDFLAGS) $(LDLIBS) -o $@

$(BUILD)/check_cli_output: tests/check_cli_output.c $(CORE) $(IO) $(HEADERS) $(BUILD)/stb.o | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/check_cli_output.c $(CORE) $(IO) $(BUILD)/stb.o $(LDFLAGS) $(LDLIBS) -o $@

test: $(BUILD)/convolve $(BUILD)/test_convolution $(BUILD)/test_io $(BUILD)/check_cli_output
	"$(BUILD)/test_convolution"
	cd "$(BUILD)" && ./test_io
	sh tests/test_cli.sh "$(abspath $(BUILD))/convolve" "$(abspath $(BUILD))/check_cli_output"

sanitize:
	$(MAKE) -B test BUILD="$(BUILD)/sanitize" CFLAGS='-std=c17 -O1 -g -Wall -Wextra -Wpedantic -Wconversion -Wshadow -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer'

tidy: $(BUILD)/bench_config.h
	clang-tidy $(wildcard src/*.c tests/*.c bench/*.c) -- $(CPPFLAGS) -I"$(BUILD)" $(CFLAGS)

format:
	clang-format -i $(FORMAT_FILES)
	ruff format --no-cache bench/*.py

format-check:
	clang-format --dry-run --Werror $(FORMAT_FILES)
	ruff format --no-cache --check bench/*.py

benchmark:
	mkdir -p "$(BENCH)"
	rm -f "$(BENCH)/complete"
	python3 bench/config.py c > "$(BENCH)/bench_config.h"
	python3 bench/config.py gnuplot > "$(BENCH)/bench_config.gp"
	$(CC) $(CPPFLAGS) -I"$(BENCH)" $(CFLAGS) bench/benchmark.c $(CORE) $(LDFLAGS) $(LDLIBS) -o "$(BENCH)/benchmark"
	python3 bench/environment.py "$(CC)" "$(CPPFLAGS)" "$(CFLAGS)" "$(LDFLAGS)" "$(LDLIBS)" > "$(BENCH)/environment.txt"
	"$(BENCH)/benchmark" > "$(BENCH)/results.csv"
	python3 bench/summarize.py "$(BENCH)/results.csv" > "$(BENCH)/summary.csv"
	gnuplot -c bench/plot.gp "$(BENCH)/summary.csv" "$(BENCH)/timing.png" "$(BENCH)/bench_config.gp"
	touch "$(BENCH)/complete"

benchmark-save:
	test -f "$(BENCH)/complete"
	for name in results.csv summary.csv timing.png environment.txt; do \
		test -s "$(BENCH)/$$name" || exit 1; \
	done
	mkdir -p bench/results/sequential
	cp "$(BENCH)/results.csv" "$(BENCH)/summary.csv" "$(BENCH)/timing.png" "$(BENCH)/environment.txt" bench/results/sequential/

clean:
	rm -rf -- "$(BUILD)"
