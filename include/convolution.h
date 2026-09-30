#ifndef CONVOLUTION_H
#define CONVOLUTION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Row-major grayscale image: data[y * width + x], one byte per pixel. */
typedef struct {
    size_t width;
    size_t height;
    uint8_t *data;
} Image;

/* Row-major coefficients: data[y * width + x]. */
typedef struct {
    size_t width;
    size_t height;
    double *data;
} Kernel;

/* Zero-initialized storage; NULL for invalid sizes or allocation failure.
 * Dimensions are positive and <= INT_MAX; kernel dimensions must also be odd.
 * The free functions accept a matching create result or NULL and release both
 * the data and the struct. Do not pass stack-allocated views to them.
 */
Image *image_create(size_t width, size_t height);
void image_free(Image *image);
Kernel *kernel_create(size_t width, size_t height);
void kernel_free(Kernel *kernel);

/* Correlation (no kernel flip), centered odd kernel, cyclic/wrap boundaries.
 * Input and kernel are read-only. Output storage must not overlap either.
 * The caller must provide buffers allocated for the declared dimensions;
 * their actual capacity and partial overlap cannot be checked here.
 * Accumulate in double, then clamp to [0,255] and round halves upwards once
 * per output pixel. Output dimensions equal input dimensions.
 * Returns false for NULL arguments/data, invalid dimensions, unequal image
 * shapes, identical output/input or output/kernel data pointers, non-finite
 * coefficients or a non-finite sum. In the last case, previously computed
 * output pixels may be overwritten.
 */
bool correlate(const Image *input, const Kernel *kernel, Image *output);

#endif
