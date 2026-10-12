#include "io.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const struct {
    const char *filter;
    uint8_t pixels[25];
} cases[] = {
    {"identity", {[12] = 16}},
    {"blur", {0, 0, 0, 0, 0, 0, 2, 2, 2, 0, 0, 2, 2, 2, 0, 0, 2, 2, 2, 0, 0, 0, 0, 0, 0}},
    {"gaussian", {0, 0, 0, 0, 0, 0, 1, 2, 1, 0, 0, 2, 4, 2, 0, 0, 1, 2, 1, 0, 0, 0, 0, 0, 0}},
    {"sharpen", {[12] = 80}},
    {"edge", {[12] = 128}}};

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "Usage: %s FILTER OUTPUT.png\n", argv[0]);
        return EXIT_FAILURE;
    }
    const uint8_t *expected = NULL;
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        if (strcmp(argv[1], cases[i].filter) == 0) {
            expected = cases[i].pixels;
            break;
        }
    }
    if (expected == NULL) {
        fprintf(stderr, "unknown test filter: %s\n", argv[1]);
        return EXIT_FAILURE;
    }
    const char *error;
    Image *output = image_read(argv[2], &error);
    if (output == NULL) {
        fprintf(stderr, "%s: %s\n", argv[2], error);
        return EXIT_FAILURE;
    }
    int status = EXIT_FAILURE;
    if (output->width != 5 || output->height != 5) {
        fprintf(stderr, "%s: expected 5x5, got %zux%zu\n", argv[1], output->width, output->height);
        goto done;
    }
    for (size_t i = 0; i < 25; ++i) {
        if (output->data[i] != expected[i]) {
            fprintf(stderr, "%s: pixel (%zu,%zu): expected %u, got %u\n", argv[1], i % 5, i / 5,
                    (unsigned)expected[i], (unsigned)output->data[i]);
            goto done;
        }
    }
    printf("pass CLI %s pixels\n", argv[1]);
    status = EXIT_SUCCESS;
done:
    image_free(output);
    return status;
}
