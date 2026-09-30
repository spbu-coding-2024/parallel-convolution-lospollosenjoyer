#include "io.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *test_name;

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "%s, line %d: %s failed\n", test_name, __LINE__, #condition); \
            exit(EXIT_FAILURE); \
        } \
    } while (0)

static FILE *temporary_file(char *path)
{
    int fd = mkstemp(path);
    CHECK(fd >= 0);
    FILE *file = fdopen(fd, "w");
    CHECK(file != NULL);
    return file;
}

static void png_roundtrip(const char *name, size_t width, size_t height)
{
    test_name = name;
    uint8_t pixels[] = {0, 1, 127, 128, 254, 255};
    Image *input = image_create(width, height);
    CHECK(input != NULL);
    for (size_t i = 0; i < width * height; ++i) {
        input->data[i] = pixels[i % sizeof(pixels)];
    }
    char path[] = "io-test-XXXXXX";
    const char unchanged[] = "no error assigned";
    const char *error = unchanged;
    CHECK(fclose(temporary_file(path)) == 0);
    CHECK(image_write_png(path, input, &error));
    CHECK(error == unchanged);
    Image *output = image_read(path, &error);
    CHECK(error == unchanged);
    CHECK(unlink(path) == 0);
    CHECK(output != NULL);
    CHECK(output->width == width && output->height == height);
    CHECK(memcmp(input->data, output->data, width * height) == 0);
    image_free(output);
    image_free(input);
    printf("pass %s\n", test_name);
}

static void invalid_files(void)
{
    test_name = "i/o errors";
    char path[] = "io-test-XXXXXX";
    const char *error = NULL;
    FILE *file = temporary_file(path);
    CHECK(fputs("not an image", file) >= 0);
    CHECK(fclose(file) == 0);
    CHECK(image_read(path, &error) == NULL && error != NULL && error[0] != '\0');
    CHECK(unlink(path) == 0);
    error = NULL;
    CHECK(image_read(path, &error) == NULL);
    CHECK(error != NULL && error[0] != '\0');
    printf("pass %s\n", test_name);
}

int main(void)
{
    png_roundtrip("png roundtrip", 3, 2);
    invalid_files();
    return EXIT_SUCCESS;
}
