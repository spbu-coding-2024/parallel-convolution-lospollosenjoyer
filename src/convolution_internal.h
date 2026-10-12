#ifndef CONVOLUTION_INTERNAL_H
#define CONVOLUTION_INTERNAL_H

#include "convolution.h"

/* Internal helpers: validate once, then give each pixel a single owner. */
bool correlation_valid(const Image *input, const Kernel *kernel, const Image *output);
bool correlation_pixel(const Image *input, const Kernel *kernel, Image *output, size_t x, size_t y);

#endif
