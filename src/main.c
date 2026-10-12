#include "convolution.h"
#include "io.h"
#include "parallel.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static bool positive(const char *text, size_t *value)
{
    if (text[0] < '0' || text[0] > '9') {
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

static void usage(FILE *stream, const char *program)
{
    fprintf(stream,
            "Usage: %s INPUT_IMAGE FILTER OUTPUT.png [MODE THREADS [GRID_COLS GRID_ROWS]]\n"
            "Modes: pixels, rows, columns, grid (grid needs both dimensions).\n"
            "Filters: identity, blur, gaussian, sharpen, edge (all 3x3).\n",
            program);
}

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        usage(stdout, argv[0]);
        return EXIT_SUCCESS;
    }
    if (argc != 4 && argc != 6 && argc != 8) {
        usage(stderr, argv[0]);
        return EXIT_FAILURE;
    }
    ParallelOptions options = {1, SPLIT_ROWS, 1, 1};
    if (argc != 4) {
        const char *modes[] = {"pixels", "rows", "columns", "grid"};
        const size_t mode_count = sizeof(modes) / sizeof(modes[0]);
        size_t mode;
        for (mode = 0; mode < mode_count && strcmp(argv[4], modes[mode]) != 0; ++mode) {
        }
        if (mode == mode_count || !positive(argv[5], &options.threads) ||
            (mode == SPLIT_GRID ? argc != 8 : argc != 6) ||
            (mode == SPLIT_GRID && (!positive(argv[6], &options.grid_columns) ||
                                    !positive(argv[7], &options.grid_rows)))) {
            usage(stderr, argv[0]);
            return EXIT_FAILURE;
        }
        options.split = (Split)mode;
    }
    const double *weights = NULL;
    for (size_t i = 0; i < sizeof(filters) / sizeof(filters[0]); ++i) {
        if (strcmp(argv[2], filters[i].name) == 0) {
            weights = filters[i].weights;
            break;
        }
    }
    if (weights == NULL) {
        fprintf(stderr, "unknown filter: %s\n", argv[2]);
        usage(stderr, argv[0]);
        return EXIT_FAILURE;
    }
    const char *error;
    Image *input = image_read(argv[1], &error);
    if (input == NULL) {
        fprintf(stderr, "%s: %s\n", argv[1], error);
        return EXIT_FAILURE;
    }
    double coefficients[9];
    memcpy(coefficients, weights, sizeof(coefficients));
    Kernel kernel = {3, 3, coefficients};
    Image *output = image_create(input->width, input->height);
    int status = EXIT_FAILURE;
    if (output == NULL) {
        fputs("cannot allocate output image\n", stderr);
        goto done;
    }
    if (!(argc == 4 ? correlate(input, &kernel, output)
                    : correlate_parallel(input, &kernel, output, &options))) {
        fputs("correlation failed: invalid arguments, non-finite sum or thread/allocation error\n",
              stderr);
        goto done;
    }
    if (!image_write_png(argv[3], output, &error)) {
        fprintf(stderr, "%s: %s\n", argv[3], error);
        goto done;
    }
    status = EXIT_SUCCESS;
done:
    image_free(output);
    image_free(input);
    return status;
}
