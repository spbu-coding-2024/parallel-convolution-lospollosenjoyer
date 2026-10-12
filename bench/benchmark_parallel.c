#include "parallel.h"
#include "bench_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double seconds(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        perror("clock_gettime");
        exit(EXIT_FAILURE);
    }
    return (double)now.tv_sec + (double)now.tv_nsec * 1e-9;
}

int main(void)
{
    const size_t sizes[] = PARALLEL_IMAGE_SIDES, kernels[] = PARALLEL_KERNEL_SIDES,
                 threads[] = PARALLEL_THREADS;
    const char *names[] = PARALLEL_MODES;
    const Split splits[] = PARALLEL_SPLITS;
    const int mode_count = (int)(sizeof(names) / sizeof(names[0])) + 1;
    const size_t thread_count = sizeof(threads) / sizeof(threads[0]);
    Image *input = NULL, *reference = NULL, *output = NULL;
    Kernel *kernel = NULL;
    int status = EXIT_FAILURE;
    puts("side,kernel,mode,threads,run,seconds");
    for (size_t s = 0; s < sizeof(sizes) / sizeof(sizes[0]); ++s) {
        for (size_t k = 0; k < sizeof(kernels) / sizeof(kernels[0]); ++k) {
            input = image_create(sizes[s], sizes[s]);
            reference = image_create(sizes[s], sizes[s]);
            output = image_create(sizes[s], sizes[s]);
            kernel = kernel_create(kernels[k], kernels[k]);
            if (input == NULL || reference == NULL || output == NULL || kernel == NULL) {
                fputs("cannot allocate benchmark images or kernel\n", stderr);
                goto done;
            }
            uint32_t state = BENCH_SEED;
            for (size_t i = 0; i < input->width * input->height; ++i) {
                state = state * 1664525U + 1013904223U;
                input->data[i] = (uint8_t)(state >> 24);
            }
            for (size_t i = 0; i < kernel->width * kernel->height; ++i) {
                kernel->data[i] = 1.0 / (double)(kernel->width * kernel->height);
            }
            if (!correlate(input, kernel, reference)) {
                fputs("invalid benchmark correlation\n", stderr);
                goto done;
            }
            for (int run = 0; run <= PARALLEL_RUNS; ++run) {
                for (int step = 0; step < mode_count; ++step) {
                    int mode = (step + run) % mode_count - 1;
                    for (size_t t = 0; t < (mode < 0 ? 1U : thread_count); ++t) {
                        ParallelOptions options = {.threads = mode < 0 ? 1 : threads[t],
                                                   .split = mode < 0 ? SPLIT_ROWS : splits[mode],
                                                   .grid_columns = PARALLEL_GRID_COLUMNS,
                                                   .grid_rows = PARALLEL_GRID_ROWS};
                        for (size_t i = 0; i < input->width * input->height; ++i) {
                            output->data[i] = (uint8_t)~reference->data[i];
                        }
                        double begin = seconds();
                        bool ok = mode < 0 ? correlate(input, kernel, output)
                                           : correlate_parallel(input, kernel, output, &options);
                        double elapsed = seconds() - begin;
                        if (!ok || memcmp(reference->data, output->data,
                                          input->width * input->height) != 0) {
                            fprintf(stderr,
                                    "result verification failed: side=%zu kernel=%zu mode=%s "
                                    "threads=%zu run=%d success=%d\n",
                                    sizes[s], kernels[k], mode < 0 ? "serial" : names[mode],
                                    options.threads, run, (int)ok);
                            goto done;
                        }
                        if (run > 0) {
                            printf("%zu,%zu,%s,%zu,%d,%.9f\n", sizes[s], kernels[k],
                                   mode < 0 ? "serial" : names[mode], options.threads, run,
                                   elapsed);
                        }
                    }
                }
            }
            kernel_free(kernel);
            image_free(output);
            image_free(reference);
            image_free(input);
            kernel = NULL;
            input = reference = output = NULL;
        }
    }
    status = EXIT_SUCCESS;
done:
    kernel_free(kernel);
    image_free(output);
    image_free(reference);
    image_free(input);
    return status;
}
