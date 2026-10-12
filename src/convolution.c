#include "convolution.h"
#include "convolution_internal.h"

#include <limits.h>
#include <math.h>
#include <stdlib.h>

static bool valid_size(size_t width, size_t height, size_t element_size)
{
    return width > 0 && height > 0 && width <= INT_MAX && height <= INT_MAX &&
           width <= (size_t)PTRDIFF_MAX / element_size / height;
}

Image *image_create(size_t width, size_t height)
{
    if (!valid_size(width, height, sizeof(uint8_t))) {
        return NULL;
    }
    Image *image = malloc(sizeof(*image));
    if (image == NULL) {
        return NULL;
    }
    image->data = calloc(width * height, sizeof(*image->data));
    if (image->data == NULL) {
        free(image);
        return NULL;
    }
    image->width = width;
    image->height = height;
    return image;
}

void image_free(Image *image)
{
    if (image != NULL) {
        free(image->data);
        free(image);
    }
}

Kernel *kernel_create(size_t width, size_t height)
{
    if (!valid_size(width, height, sizeof(double)) || width % 2 == 0 || height % 2 == 0) {
        return NULL;
    }
    Kernel *kernel = malloc(sizeof(*kernel));
    if (kernel == NULL) {
        return NULL;
    }
    kernel->data = calloc(width * height, sizeof(*kernel->data));
    if (kernel->data == NULL) {
        free(kernel);
        return NULL;
    }
    kernel->width = width;
    kernel->height = height;
    return kernel;
}

void kernel_free(Kernel *kernel)
{
    if (kernel != NULL) {
        free(kernel->data);
        free(kernel);
    }
}

static bool valid_image(const Image *image)
{
    return image != NULL && image->data != NULL &&
           valid_size(image->width, image->height, sizeof(*image->data));
}

static size_t wrap(int64_t coordinate, size_t length)
{
    int64_t remainder = coordinate % (int64_t)length;
    return (size_t)(remainder < 0 ? remainder + (int64_t)length : remainder);
}

bool correlation_valid(const Image *input, const Kernel *kernel, const Image *output)
{
    if (!valid_image(input) || !valid_image(output) || kernel == NULL || kernel->data == NULL ||
        !valid_size(kernel->width, kernel->height, sizeof(double)) || kernel->width % 2 == 0 ||
        kernel->height % 2 == 0 || input->width != output->width ||
        input->height != output->height || output->data == input->data ||
        (void *)output->data == (void *)kernel->data) {
        return false;
    }
    for (size_t i = 0; i < kernel->width * kernel->height; ++i) {
        if (!isfinite(kernel->data[i])) {
            return false;
        }
    }

    return true;
}

bool correlation_pixel(const Image *input, const Kernel *kernel, Image *output, size_t x, size_t y)
{
    const int64_t radius_x = (int64_t)(kernel->width / 2);
    const int64_t radius_y = (int64_t)(kernel->height / 2);
    double sum = 0.0;
    for (size_t ky = 0; ky < kernel->height; ++ky) {
        size_t sy = wrap((int64_t)y + (int64_t)ky - radius_y, input->height);
        for (size_t kx = 0; kx < kernel->width; ++kx) {
            size_t sx = wrap((int64_t)x + (int64_t)kx - radius_x, input->width);
            sum +=
                (double)input->data[sy * input->width + sx] * kernel->data[ky * kernel->width + kx];
        }
    }
    if (!isfinite(sum)) {
        return false;
    }
    output->data[y * output->width + x] = sum <= 0.0 ? 0 : sum >= 255.0 ? 255 : (uint8_t)round(sum);
    return true;
}

bool correlate(const Image *input, const Kernel *kernel, Image *output)
{
    if (!correlation_valid(input, kernel, output)) {
        return false;
    }
    for (size_t y = 0; y < input->height; ++y) {
        for (size_t x = 0; x < input->width; ++x) {
            if (!correlation_pixel(input, kernel, output, x, y)) {
                return false;
            }
        }
    }
    return true;
}
