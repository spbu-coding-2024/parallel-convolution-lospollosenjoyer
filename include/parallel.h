#ifndef PARALLEL_H
#define PARALLEL_H

#include "convolution.h"

typedef enum { SPLIT_PIXELS, SPLIT_ROWS, SPLIT_COLUMNS, SPLIT_GRID } Split;
typedef struct {
    size_t threads;
    Split split;
    size_t grid_columns;
    size_t grid_rows;
} ParallelOptions;

/* Same numerical/aliasing contract as correlate. Pixel indices are assigned
 * round-robin; rows/columns in contiguous balanced strips; grid tiles round-robin.
 * Grid dimensions count tiles, and are clipped to image dimensions.
 * threads must be positive; grid dimensions must be positive for SPLIT_GRID.
 * Excess threads are capped to the number of work units. Returns false on bad
 * options or allocation/thread-creation failure. All started threads are joined
 * before return, including errors; output may be partially written on failure.
 */
bool correlate_parallel(const Image *input, const Kernel *kernel, Image *output,
                        const ParallelOptions *options);

#endif
