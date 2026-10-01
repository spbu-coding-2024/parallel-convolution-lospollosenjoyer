#include "parallel.h"

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_SEED 12345U

#define CHECK(c) \
    do { \
        if (!(c)) { \
            fprintf(stderr, "%s:%d: %s\n", __func__, __LINE__, #c); \
            exit(1); \
        } \
    } while (0)

static uint32_t next(uint32_t *state)
{
    *state = *state * 1664525U + 1013904223U;
    return *state;
}

typedef enum { NORMALIZED_POSITIVE, WEAK_MIXED, STRONG_MIXED } WeightFamily;
static const char *family_names[] = {"normalized_positive", "weak_mixed", "strong_mixed"};

static void random_kernel(Kernel *kernel, WeightFamily family, uint32_t *state)
{
    size_t count = kernel->width * kernel->height;
    double total = 0;
    for (size_t i = 0; i < count; ++i) {
        unsigned q = (unsigned)(next(state) >> 24);
        if (family == NORMALIZED_POSITIVE) {
            kernel->data[i] = (double)(q + 1);
            total += kernel->data[i];
        } else if (family == WEAK_MIXED) {
            kernel->data[i] = (double)((int)q - 128) / (128.0 * (double)count);
        } else {
            kernel->data[i] = (double)((int)(q % 17) - 6) / 32.0;
        }
    }
    if (family == NORMALIZED_POSITIVE) {
        for (size_t i = 0; i < count; ++i) {
            kernel->data[i] /= total;
        }
    } else if (family == WEAK_MIXED) {
        kernel->data[count / 2] += 0.5;
    }
}

static void compare_result(const Image *input, const Kernel *kernel, const Image *reference,
                           Image *actual, const ParallelOptions *options, WeightFamily family)
{
    size_t count = input->width * input->height;
    for (size_t i = 0; i < count; ++i) {
        actual->data[i] = (uint8_t)~reference->data[i];
    }
    bool ok = correlate_parallel(input, kernel, actual, options);
    if (ok && memcmp(actual->data, reference->data, count) == 0) {
        return;
    }
    fprintf(stderr,
            "seed=%u family=%s image=%zux%zu kernel=%zux%zu split=%d "
            "threads=%zu grid=%zux%zu success=%d\n",
            TEST_SEED, family_names[family], input->width, input->height, kernel->width,
            kernel->height, (int)options->split, options->threads, options->grid_columns,
            options->grid_rows, (int)ok);
    for (size_t i = 0; i < count; ++i) {
        if (actual->data[i] != reference->data[i]) {
            fprintf(stderr, "pixel=%zu expected=%u actual=%u\n", i, (unsigned)reference->data[i],
                    (unsigned)actual->data[i]);
            break;
        }
    }
    fputs("input:", stderr);
    for (size_t i = 0; i < count; ++i) {
        fprintf(stderr, " %u", (unsigned)input->data[i]);
    }
    fputs("\nkernel:", stderr);
    for (size_t i = 0; i < kernel->width * kernel->height; ++i) {
        fprintf(stderr, " %.17g", kernel->data[i]);
    }
    fputc('\n', stderr);
    exit(1);
}

static void compare_all_splits(void)
{
    const size_t widths[] = {1, 2, 3, 7, 16, 31};
    const size_t heights[] = {1, 2, 5, 11};
    const size_t workers[] = {1, 2, 3, 8};
    const size_t grids[][2] = {{5, 3}, {1, 1}, {5, 1}, {1, 3}, {4, 2}};
    const size_t shapes[][2] = {{1, 1}, {3, 5}, {11, 7}};
    uint32_t state = TEST_SEED;
    size_t cases[sizeof(family_names) / sizeof(family_names[0])] = {0};
    for (size_t wi = 0; wi < sizeof(widths) / sizeof(widths[0]); ++wi) {
        for (size_t hi = 0; hi < sizeof(heights) / sizeof(heights[0]); ++hi) {
            size_t width = widths[wi], height = heights[hi], count = width * height;
            Image *input = image_create(width, height), *reference = image_create(width, height);
            Image *actual = image_create(width, height), *copy = image_create(width, height);
            CHECK(input != NULL && reference != NULL && actual != NULL && copy != NULL);
            for (size_t i = 0; i < count; ++i) {
                input->data[i] = (uint8_t)(next(&state) >> 24);
            }
            memcpy(copy->data, input->data, count);
            for (size_t shape = 0; shape < sizeof(shapes) / sizeof(shapes[0]); ++shape) {
                Kernel *kernel = kernel_create(shapes[shape][0], shapes[shape][1]);
                CHECK(kernel != NULL);
                for (int f = NORMALIZED_POSITIVE; f <= STRONG_MIXED; ++f) {
                    WeightFamily family = (WeightFamily)f;
                    random_kernel(kernel, family, &state);
                    CHECK(correlate(input, kernel, reference));
                    for (int mode = SPLIT_PIXELS; mode <= SPLIT_GRID; ++mode) {
                        size_t grid_count =
                            mode == SPLIT_GRID ? sizeof(grids) / sizeof(grids[0]) : 1;
                        for (size_t g = 0; g < grid_count; ++g) {
                            for (size_t t = 0; t < sizeof(workers) / sizeof(workers[0]); ++t) {
                                ParallelOptions options = {workers[t], (Split)mode, grids[g][0],
                                                           grids[g][1]};
                                compare_result(input, kernel, reference, actual, &options, family);
                                CHECK(memcmp(input->data, copy->data, count) == 0);
                                ++cases[f];
                            }
                        }
                    }
                }
                kernel_free(kernel);
            }
            image_free(copy);
            image_free(actual);
            image_free(reference);
            image_free(input);
        }
    }
    for (size_t f = 0; f < sizeof(cases) / sizeof(cases[0]); ++f) {
        printf("pass parallel_matches_sequential family=%s (%zu configurations, "
               "seed=%u)\n",
               family_names[f], cases[f], TEST_SEED);
    }
}

static void errors_and_extreme_grid(void)
{
    uint8_t data[] = {255}, out[] = {99};
    double weight[] = {1};
    Image input = {1, 1, data}, output = {1, 1, out};
    Kernel kernel = {1, 1, weight};
    ParallelOptions options = {8, SPLIT_GRID, SIZE_MAX, SIZE_MAX};
    CHECK(correlate_parallel(&input, &kernel, &output, &options));
    CHECK(out[0] == 255);
    CHECK(!correlate_parallel(&input, &kernel, &output, NULL));
    options.threads = 0;
    CHECK(!correlate_parallel(&input, &kernel, &output, &options));
    options.threads = 8;
    options.grid_rows = 0;
    CHECK(!correlate_parallel(&input, &kernel, &output, &options));
    options.grid_rows = 1;
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    options.split = (Split)99;
    CHECK(!correlate_parallel(&input, &kernel, &output, &options));
    options.split = SPLIT_ROWS;
    CHECK(!correlate_parallel(NULL, &kernel, &output, &options));
    CHECK(!correlate_parallel(&input, &kernel, &input, &options));
    weight[0] = NAN;
    CHECK(!correlate_parallel(&input, &kernel, &output, &options));
    for (int mode = SPLIT_PIXELS; mode <= SPLIT_GRID; ++mode) {
        weight[0] = DBL_MAX;
        options.split = (Split)mode;
        CHECK(!correlate_parallel(&input, &kernel, &output, &options));
    }
    puts("pass parallel_option_errors_and_nonfinite_results");
}

int main(void)
{
    compare_all_splits();
    errors_and_extreme_grid();
    return 0;
}
