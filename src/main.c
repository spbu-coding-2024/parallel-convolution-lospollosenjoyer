#include "convolution.h"
#include "io.h"

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

static void usage(FILE *stream, const char *program)
{
    fprintf(stream,
            "Usage: %s INPUT_IMAGE FILTER OUTPUT.png\n"
            "Filters: identity, blur, gaussian, sharpen, edge (all 3x3).\n",
            program);
}

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        usage(stdout, argv[0]);
        return EXIT_SUCCESS;
    }
    if (argc != 4) {
        usage(stderr, argv[0]);
        return EXIT_FAILURE;
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
    if (!correlate(input, &kernel, output)) {
        fputs("invalid correlation arguments or non-finite sum\n", stderr);
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
