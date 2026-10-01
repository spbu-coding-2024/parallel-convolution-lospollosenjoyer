#ifndef PIPELINE_H
#define PIPELINE_H
#include "convolution.h"

typedef struct {
    size_t workers, capacity;
} PipelineOptions;
typedef enum {
    PIPELINE_OK,
    PIPELINE_ARGUMENT,
    PIPELINE_RESOURCE,
    PIPELINE_READ,
    PIPELINE_CORRELATE,
    PIPELINE_WRITE
} PipelineStage;
typedef struct {
    size_t read, processed, written, peak_in_flight;
    PipelineStage failed_stage;
    size_t failed_index; /* SIZE_MAX for setup errors or success. */
} PipelineResult;
typedef struct {
    /* One reader calls read in input order; returned Image ownership transfers
     * to the pipeline (must be compatible with image_free). NULL means error.
     * One writer calls write in completion order. It borrows output only for
     * this call; false means error. read and write may overlap: their context
     * must support that. Each callback has at most one concurrent invocation.
     */
    Image *(*read)(size_t index, void *context);
    bool (*write)(size_t index, const Image *image, void *context);
    void *context;
} PipelineIO;

/* Caller reads, workers correlate sequentially, one writer saves results.
 * capacity bounds ALL in-flight jobs (reading, queued, processing, writing).
 * Each job owns at most an input and output image, excluding codec scratch
 * storage. No unbounded prefetching; workers are capped to capacity/job count.
 * Kernel and IO context must remain valid until return; kernel is read-only.
 * On error, stop admitting jobs, wake waiters, join all threads, release every
 * owned image. Already completed outputs are retained. In-progress callbacks
 * finish normally; callbacks must eventually return. count==0 succeeds without
 * callbacks, after checking options/IO and non-NULL kernel/result pointers.
 */
bool pipeline_run(size_t count, const Kernel *kernel, const PipelineOptions *options,
                  const PipelineIO *io, PipelineResult *result);
#endif
