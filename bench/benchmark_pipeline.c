#include "pipeline.h"
#include "io.h"
#include "bench_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {
    char *inputs[PIPELINE_BATCH_SIZE], *outputs[PIPELINE_BATCH_SIZE];
} Files;

static double seconds(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        perror("clock_gettime");
        exit(EXIT_FAILURE);
    }
    return (double)now.tv_sec + (double)now.tv_nsec * 1e-9;
}

static Image *load(size_t index, void *context)
{
    const char *error;
    Image *image = image_read(((Files *)context)->inputs[index], &error);
    if (image == NULL) {
        fprintf(stderr, "read: %s\n", error);
    }
    return image;
}

static bool save(size_t index, const Image *image, void *context)
{
    const char *error;
    bool ok = image_write_png(((Files *)context)->outputs[index], image, &error);
    if (!ok) {
        fprintf(stderr, "write: %s\n", error);
    }
    return ok;
}

static bool reset_outputs(const Files *files)
{
    for (size_t i = 0; i < PIPELINE_BATCH_SIZE; ++i) {
        FILE *file = fopen(files->outputs[i], "wb");
        if (file == NULL) {
            perror(files->outputs[i]);
            return false;
        }
        if (fclose(file) != 0) {
            perror(files->outputs[i]);
            return false;
        }
    }
    return true;
}

static char *path(const char *directory, const char *kind, size_t index)
{
    int length = snprintf(NULL, 0, "%s/%s-%06zu.png", directory, kind, index);
    if (length < 0) {
        return NULL;
    }
    char *name = malloc((size_t)length + 1);
    if (name == NULL) {
        return NULL;
    }
    snprintf(name, (size_t)length + 1, "%s/%s-%06zu.png", directory, kind, index);
    return name;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s EXISTING_SCRATCH_DIR\n", argv[0]);
        return EXIT_FAILURE;
    }
    Files files = {0};
    Image *expected[PIPELINE_BATCH_SIZE] = {0};
    Image *input = NULL, *output = NULL;
    Kernel *kernel = NULL;
    int status = EXIT_FAILURE;
    const char *error;
    for (size_t i = 0; i < PIPELINE_BATCH_SIZE; ++i) {
        files.inputs[i] = path(argv[1], "input", i);
        files.outputs[i] = path(argv[1], "output", i);
        if (files.inputs[i] == NULL || files.outputs[i] == NULL) {
            goto cleanup;
        }
        input = image_create(PIPELINE_IMAGE_SIDE, PIPELINE_IMAGE_SIDE);
        expected[i] = image_create(PIPELINE_IMAGE_SIDE, PIPELINE_IMAGE_SIDE);
        if (input == NULL || expected[i] == NULL) {
            goto cleanup;
        }
        uint32_t state = (uint32_t)(BENCH_SEED + i);
        for (size_t p = 0; p < (size_t)PIPELINE_IMAGE_SIDE * PIPELINE_IMAGE_SIDE; ++p) {
            state = state * 1664525U + 1013904223U;
            input->data[p] = (uint8_t)(state >> 24);
        }
        if (!image_write_png(files.inputs[i], input, &error)) {
            goto cleanup;
        }
        image_free(input);
        input = NULL;
    }
    PipelineIO io = {load, save, &files};
    const size_t workers[] = PIPELINE_WORKERS, capacities[] = PIPELINE_CAPACITIES,
                 kernels[] = PIPELINE_KERNEL_SIDES;
    const int capacity_count = (int)(sizeof(capacities) / sizeof(capacities[0]));
    const int configuration_count =
        (int)(sizeof(workers) / sizeof(workers[0])) * capacity_count + 1;
    puts("side,count,kernel,mode,workers,capacity,run,seconds,peak_in_flight");
    for (size_t k = 0; k < sizeof(kernels) / sizeof(kernels[0]); ++k) {
        kernel = kernel_create(kernels[k], kernels[k]);
        if (kernel == NULL) {
            goto cleanup;
        }
        for (size_t p = 0; p < kernels[k] * kernels[k]; ++p) {
            kernel->data[p] = 1.0 / (double)(kernels[k] * kernels[k]);
        }
        for (size_t i = 0; i < PIPELINE_BATCH_SIZE; ++i) {
            input = load(i, &files);
            if (input == NULL || !correlate(input, kernel, expected[i])) {
                goto cleanup;
            }
            image_free(input);
            input = NULL;
        }
        /* warmup zero run */
        for (int run = 0; run <= PIPELINE_RUNS; ++run) {
            for (int step = 0; step < configuration_count; ++step) {
                int configuration = (step + run) % configuration_count;
                size_t worker_count =
                    configuration == 0 ? 1 : workers[(configuration - 1) / capacity_count];
                size_t capacity =
                    configuration == 0 ? 1 : capacities[(configuration - 1) % capacity_count];
                PipelineResult result = {0};
                if (!reset_outputs(&files)) {
                    goto cleanup;
                }
                double begin = seconds();
                if (configuration == 0) {
                    for (size_t i = 0; i < PIPELINE_BATCH_SIZE; ++i) {
                        input = load(i, &files);
                        if (input == NULL) {
                            goto cleanup;
                        }
                        output = image_create(input->width, input->height);
                        if (output == NULL || !correlate(input, kernel, output) ||
                            !save(i, output, &files)) {
                            goto cleanup;
                        }
                        image_free(output);
                        image_free(input);
                        output = NULL;
                        input = NULL;
                    }
                    result.peak_in_flight = 1;
                } else {
                    PipelineOptions options = {worker_count, capacity};
                    if (!pipeline_run(PIPELINE_BATCH_SIZE, kernel, &options, &io, &result) ||
                        result.written != PIPELINE_BATCH_SIZE) {
                        goto cleanup;
                    }
                }
                double elapsed = seconds() - begin;
                for (size_t i = 0; i < PIPELINE_BATCH_SIZE; ++i) {
                    output = image_read(files.outputs[i], &error);
                    if (output == NULL || output->width != PIPELINE_IMAGE_SIDE ||
                        output->height != PIPELINE_IMAGE_SIDE ||
                        memcmp(output->data, expected[i]->data,
                               (size_t)PIPELINE_IMAGE_SIDE * PIPELINE_IMAGE_SIDE) != 0) {
                        fprintf(stderr,
                                "result verification failed: kernel=%zu configuration=%d "
                                "run=%d image=%zu path=%s: %s\n",
                                kernels[k], configuration, run, i, files.outputs[i],
                                output == NULL ? error : "pixels differ");
                        goto cleanup;
                    }
                    image_free(output);
                    output = NULL;
                }
                if (run > 0) {
                    printf("%d,%d,%zu,%s,%zu,%zu,%d,%.9f,%zu\n", PIPELINE_IMAGE_SIDE,
                           PIPELINE_BATCH_SIZE, kernels[k],
                           configuration == 0 ? "serial" : "pipeline", worker_count, capacity, run,
                           elapsed, result.peak_in_flight);
                }
            }
        }
        kernel_free(kernel);
        kernel = NULL;
    }
    status = EXIT_SUCCESS;
cleanup:
    image_free(input);
    image_free(output);
    kernel_free(kernel);
    for (size_t i = 0; i < PIPELINE_BATCH_SIZE; ++i) {
        image_free(expected[i]);
        free(files.inputs[i]);
        free(files.outputs[i]);
    }
    return status;
}
