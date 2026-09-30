#include "io.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <stb_image.h>
#include <stb_image_write.h>

Image *image_read(const char *path, const char **error)
{
    int width, height;
    stbi_uc *pixels = stbi_load(path, &width, &height, NULL, STBI_grey);
    if (pixels == NULL) {
        const char *reason = stbi_failure_reason();
        *error = reason != NULL ? reason : "cannot load image";
        return NULL;
    }
    Image *image = image_create((size_t)width, (size_t)height);
    if (image == NULL) {
        *error = "invalid image dimensions or out of memory";
    } else {
        memcpy(image->data, pixels, image->width * image->height);
    }
    stbi_image_free(pixels);
    return image;
}

static void write_bytes(void *context, void *data, int size)
{
    (void)fwrite(data, 1, (size_t)size, context);
}

bool image_write_png(const char *path, const Image *image, const char **error)
{
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        *error = strerror(errno);
        return false;
    }
    bool ok = stbi_write_png_to_func(write_bytes, file, (int)image->width, (int)image->height, 1,
                                     image->data, (int)image->width) != 0;
    if (ferror(file)) {
        ok = false;
    }
    if (fclose(file) != 0) {
        ok = false;
    }
    if (!ok) {
        *error = "failed to write PNG";
    }
    return ok;
}
