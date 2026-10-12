#include "convolution.h"
#include "bench_config.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static double seconds(void)
{
    struct timespec time;
    if (clock_gettime(CLOCK_MONOTONIC, &time) != 0) {
        perror("clock_gettime");
        exit(EXIT_FAILURE);
    }
    return (double)time.tv_sec + (double)time.tv_nsec * 1e-9;
}

int main(void)
{
    const size_t sizes[] = SEQUENTIAL_IMAGE_SIDES;
    const size_t kernels[] = SEQUENTIAL_KERNEL_SIDES;
    puts("width,height,kernel,run,seconds,checksum");
    for (size_t s = 0; s < sizeof(sizes) / sizeof(sizes[0]); ++s) {
        Image *input = image_create(sizes[s], sizes[s]);
        Image *output = image_create(sizes[s], sizes[s]);
        if (input == NULL || output == NULL) {
            fputs("cannot allocate benchmark image\n", stderr);
            image_free(input);
            image_free(output);
            return EXIT_FAILURE;
        }
        uint32_t state = BENCH_SEED;
        for (size_t i = 0; i < input->width * input->height; ++i) {
            state = state * 1664525U + 1013904223U;
            input->data[i] = (uint8_t)(state >> 24);
        }
        for (size_t k = 0; k < sizeof(kernels) / sizeof(kernels[0]); ++k) {
            Kernel *kernel = kernel_create(kernels[k], kernels[k]);
            if (kernel == NULL) {
                fputs("cannot allocate benchmark kernel\n", stderr);
                image_free(input);
                image_free(output);
                return EXIT_FAILURE;
            }
            for (size_t i = 0; i < kernel->width * kernel->height; ++i) {
                kernel->data[i] = 1.0 / (double)(kernel->width * kernel->height);
            }
            /* warmup zero run */
            for (int run = 0; run <= SEQUENTIAL_RUNS; ++run) {
                double start = seconds();
                if (!correlate(input, kernel, output)) {
                    fputs("invalid benchmark correlation\n", stderr);
                    kernel_free(kernel);
                    image_free(input);
                    image_free(output);
                    return EXIT_FAILURE;
                }
                double elapsed = seconds() - start;
                double checksum = 0.0;
                for (size_t i = 0; i < output->width * output->height; ++i) {
                    checksum += output->data[i];
                }
                if (run > 0) {
                    printf("%zu,%zu,%zu,%d,%.9f,%.9f\n", input->width, input->height, kernel->width,
                           run, elapsed, checksum);
                }
            }
            kernel_free(kernel);
        }
        image_free(input);
        image_free(output);
    }
    return EXIT_SUCCESS;
}
