#ifndef CONVOLUTION_IO_H
#define CONVOLUTION_IO_H

#include "convolution.h"

/* Paths and error are non-NULL. Failure sets *error to a borrowed message:
 * print or copy it immediately; do not free it. Success leaves *error unchanged.
 * image_read returns an owned image to release with image_free.
 */
Image *image_read(const char *path, const char **error);
bool image_write_png(const char *path, const Image *image, const char **error);

#endif
