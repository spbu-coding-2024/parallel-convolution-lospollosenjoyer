#include "pipeline.h"
#include "io.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static const struct {
    const char *name;
    double weights[9];
} filters[] = {
    {"identity", {0, 0, 0, 0, 1, 0, 0, 0, 0}},
    {"blur", {1.0 / 9, 1.0 / 9, 1.0 / 9, 1.0 / 9, 1.0 / 9, 1.0 / 9, 1.0 / 9, 1.0 / 9, 1.0 / 9}},
    {"gaussian",
     {1.0 / 16, 2.0 / 16, 1.0 / 16, 2.0 / 16, 4.0 / 16, 2.0 / 16, 1.0 / 16, 2.0 / 16, 1.0 / 16}},
    {"sharpen", {0, -1, 0, -1, 5, -1, 0, -1, 0}},
    {"edge", {-1, -1, -1, -1, 8, -1, -1, -1, -1}},
};

typedef struct {
    char **inputs;
    const char *directory;
} Files;

static Image *read_file(size_t index, void *context)
{
    Files *files = context;
    const char *error;
    Image *image = image_read(files->inputs[index], &error);
    if (image == NULL) {
        fprintf(stderr, "%s: %s\n", files->inputs[index], error);
    }
    return image;
}

static char *output_path(const Files *files, size_t index)
{
    int length = snprintf(NULL, 0, "%s/%06zu.png", files->directory, index);
    if (length < 0) {
        return NULL;
    }
    char *path = malloc((size_t)length + 1);
    if (path != NULL) {
        snprintf(path, (size_t)length + 1, "%s/%06zu.png", files->directory, index);
    }
    return path;
}

static bool write_file(size_t index, const Image *image, void *context)
{
    char *path = output_path(context, index);
    if (path == NULL) {
        return false;
    }
    const char *error;
    bool ok = image_write_png(path, image, &error);
    if (!ok) {
        fprintf(stderr, "%s: %s\n", path, error);
    }
    free(path);
    return ok;
}

static bool positive(const char *text, size_t *value)
{
    if (*text < '0' || *text > '9') {
        return false;
    }
    char *end;
    errno = 0;
    unsigned long long number = strtoull(text, &end, 10);
    if (errno != 0 || *end != '\0' || number == 0 || number > SIZE_MAX) {
        return false;
    }
    *value = (size_t)number;
    return true;
}

static bool select_filter(const char *name, double weights[9])
{
    for (size_t i = 0; i < sizeof(filters) / sizeof(filters[0]); ++i) {
        if (strcmp(name, filters[i].name) == 0) {
            memcpy(weights, filters[i].weights, sizeof(filters[i].weights));
            return true;
        }
    }
    return false;
}

static void usage(FILE *stream, const char *program)
{
    fprintf(stream,
            "Usage: %s FILTER OUTPUT_DIR WORKERS CAPACITY INPUT_IMAGE...\n"
            "Filters: identity, blur, gaussian, sharpen, edge.\n"
            "OUTPUT_DIR must not exist; it is created for 000000.png, 000001.png, ... .\n"
            "WORKERS and CAPACITY must be positive; CAPACITY bounds all in-flight jobs.\n",
            program);
}

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        usage(stdout, argv[0]);
        return EXIT_SUCCESS;
    }
    PipelineOptions options;
    double weights[9];
    if (argc < 6 || !select_filter(argv[1], weights) || !positive(argv[3], &options.workers) ||
        !positive(argv[4], &options.capacity)) {
        usage(stderr, argv[0]);
        return EXIT_FAILURE;
    }
    Kernel kernel = {3, 3, weights};
    Files files = {argv + 5, argv[2]};
    if (mkdir(files.directory, 0777) != 0) {
        perror(files.directory);
        return EXIT_FAILURE;
    }
    PipelineIO io = {read_file, write_file, &files};
    PipelineResult result;
    bool ok = pipeline_run((size_t)(argc - 5), &kernel, &options, &io, &result);
    fprintf(stderr, "read=%zu processed=%zu written=%zu peak_in_flight=%zu\n", result.read,
            result.processed, result.written, result.peak_in_flight);
    if (!ok) {
        const char *stages[] = {"ok", "arguments", "resources", "read", "correlate", "write"};
        if (result.failed_index == SIZE_MAX) {
            fprintf(stderr, "pipeline failed: %s\n", stages[result.failed_stage]);
        } else {
            fprintf(stderr, "pipeline failed: %s, input %zu (%s)\n", stages[result.failed_stage],
                    result.failed_index, files.inputs[result.failed_index]);
        }
    }
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
