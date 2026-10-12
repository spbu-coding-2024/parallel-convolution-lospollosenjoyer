#include "parallel.h"
#include "convolution_internal.h"

#include <pthread.h>
#include <stdlib.h>

typedef struct {
    const Image *input;
    const Kernel *kernel;
    Image *output;
    ParallelOptions options;
    size_t id, count;
    bool ok;
} Worker;

typedef struct {
    size_t x0, y0, x1, y1;
} Rectangle;

static size_t boundary(size_t length, size_t rank, size_t count)
{
    return (length / count) * rank + (rank < length % count ? rank : length % count);
}

static bool rectangle(Worker *w, Rectangle area)
{
    for (size_t y = area.y0; y < area.y1; ++y) {
        for (size_t x = area.x0; x < area.x1; ++x) {
            if (!correlation_pixel(w->input, w->kernel, w->output, x, y)) {
                return false;
            }
        }
    }
    return true;
}

static void *work(void *argument)
{
    Worker *w = argument;
    size_t width = w->input->width, height = w->input->height;
    w->ok = true;
    switch (w->options.split) {
    case SPLIT_PIXELS:
        for (size_t i = w->id; i < width * height; i += w->count) {
            if (!correlation_pixel(w->input, w->kernel, w->output, i % width, i / width)) {
                w->ok = false;
                break;
            }
        }
        break;
    case SPLIT_ROWS:
        w->ok = rectangle(w, (Rectangle){.y0 = boundary(height, w->id, w->count),
                                         .x1 = width,
                                         .y1 = boundary(height, w->id + 1, w->count)});
        break;
    case SPLIT_COLUMNS:
        w->ok = rectangle(w, (Rectangle){.x0 = boundary(width, w->id, w->count),
                                         .x1 = boundary(width, w->id + 1, w->count),
                                         .y1 = height});
        break;
    case SPLIT_GRID: {
        size_t columns = w->options.grid_columns, rows = w->options.grid_rows;
        for (size_t tile = w->id; tile < columns * rows; tile += w->count) {
            size_t x = tile % columns, y = tile / columns;
            Rectangle area = {.x0 = boundary(width, x, columns),
                              .y0 = boundary(height, y, rows),
                              .x1 = boundary(width, x + 1, columns),
                              .y1 = boundary(height, y + 1, rows)};
            if (!rectangle(w, area)) {
                w->ok = false;
                break;
            }
        }
        break;
    }
    }
    return NULL;
}

bool correlate_parallel(const Image *input, const Kernel *kernel, Image *output,
                        const ParallelOptions *options)
{
    if (options == NULL || options->threads == 0 || options->split < SPLIT_PIXELS ||
        options->split > SPLIT_GRID ||
        (options->split == SPLIT_GRID && (options->grid_columns == 0 || options->grid_rows == 0)) ||
        !correlation_valid(input, kernel, output)) {
        return false;
    }
    ParallelOptions actual = *options;
    size_t units = input->width * input->height;
    if (actual.split == SPLIT_ROWS) {
        units = input->height;
    }
    if (actual.split == SPLIT_COLUMNS) {
        units = input->width;
    }
    if (actual.split == SPLIT_GRID) {
        if (actual.grid_columns > input->width) {
            actual.grid_columns = input->width;
        }
        if (actual.grid_rows > input->height) {
            actual.grid_rows = input->height;
        }
        units = actual.grid_columns * actual.grid_rows;
    }
    size_t count = actual.threads < units ? actual.threads : units;
    if (count > SIZE_MAX / sizeof(Worker) || count > SIZE_MAX / sizeof(pthread_t)) {
        return false;
    }
    Worker *workers = calloc(count, sizeof(*workers));
    pthread_t *threads = calloc(count, sizeof(*threads));
    if (workers == NULL || threads == NULL) {
        free(workers);
        free(threads);
        return false;
    }
    size_t started = 0;
    for (; started < count; ++started) {
        workers[started] = (Worker){input, kernel, output, actual, started, count, false};
        if (pthread_create(&threads[started], NULL, work, &workers[started]) != 0) {
            break;
        }
    }
    bool ok = started == count;
    for (size_t i = 0; i < started; ++i) {
        if (pthread_join(threads[i], NULL) != 0) {
            abort();
        }
        if (!workers[i].ok) {
            ok = false;
        }
    }
    free(threads);
    free(workers);
    return ok;
}
