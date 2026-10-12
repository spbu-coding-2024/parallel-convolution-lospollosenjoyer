#include "pipeline.h"

#include <pthread.h>
#include <stdlib.h>

typedef struct Item {
    size_t index;
    Image *input, *output;
    struct Item *next;
} Item;
typedef struct {
    Item **data;
    size_t head, count;
} Queue;
typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t changed;
    const Kernel *kernel;
    const PipelineIO *io;
    PipelineResult *result;
    Item *free_items;
    Queue ready, done;
    size_t capacity, live, workers_alive;
    bool reader_done, cancelled;
} Pipeline;

static void fail(Pipeline *p, PipelineStage stage, const Item *item)
{
    if (!p->cancelled) {
        p->result->failed_stage = stage;
        p->result->failed_index = item != NULL ? item->index : SIZE_MAX;
    }
    p->cancelled = true;
    pthread_cond_broadcast(&p->changed);
}

static void push(Queue *q, Item *item, size_t capacity)
{
    q->data[(q->head + q->count) % capacity] = item;
    ++q->count;
}

static Item *pop(Queue *q, size_t capacity)
{
    Item *item = q->data[q->head];
    q->head = (q->head + 1) % capacity;
    --q->count;
    return item;
}

static void *compute(void *argument)
{
    Pipeline *p = argument;
    pthread_mutex_lock(&p->mutex);
    for (;;) {
        while (!p->cancelled && p->ready.count == 0 && !p->reader_done) {
            pthread_cond_wait(&p->changed, &p->mutex);
        }
        if (p->cancelled || p->ready.count == 0) {
            break;
        }
        Item *item = pop(&p->ready, p->capacity);
        pthread_mutex_unlock(&p->mutex);
        item->output = image_create(item->input->width, item->input->height);
        bool ok = item->output != NULL && correlate(item->input, p->kernel, item->output);
        image_free(item->input);
        item->input = NULL;
        pthread_mutex_lock(&p->mutex);
        if (!ok) {
            fail(p, item->output == NULL ? PIPELINE_RESOURCE : PIPELINE_CORRELATE, item);
            break;
        }
        if (p->cancelled) {
            break;
        }
        ++p->result->processed;
        push(&p->done, item, p->capacity);
        pthread_cond_broadcast(&p->changed);
    }
    --p->workers_alive;
    pthread_cond_broadcast(&p->changed);
    pthread_mutex_unlock(&p->mutex);
    return NULL;
}

static void *write_results(void *argument)
{
    Pipeline *p = argument;
    pthread_mutex_lock(&p->mutex);
    for (;;) {
        while (!p->cancelled && p->done.count == 0 && p->workers_alive != 0) {
            pthread_cond_wait(&p->changed, &p->mutex);
        }
        if (p->cancelled || p->done.count == 0) {
            break;
        }
        Item *item = pop(&p->done, p->capacity);
        pthread_mutex_unlock(&p->mutex);
        bool ok = p->io->write(item->index, item->output, p->io->context);
        image_free(item->output);
        item->output = NULL;
        pthread_mutex_lock(&p->mutex);
        if (!ok) {
            fail(p, PIPELINE_WRITE, item);
            break;
        }
        ++p->result->written;
        --p->live;
        item->next = p->free_items;
        p->free_items = item;
        pthread_cond_broadcast(&p->changed);
    }
    pthread_mutex_unlock(&p->mutex);
    return NULL;
}

bool pipeline_run(size_t count, const Kernel *kernel, const PipelineOptions *options,
                  const PipelineIO *io, PipelineResult *result)
{
    if (result == NULL) {
        return false;
    }
    *result = (PipelineResult){.failed_stage = PIPELINE_ARGUMENT, .failed_index = SIZE_MAX};
    if (options == NULL || options->workers == 0 || options->capacity == 0 || io == NULL ||
        io->read == NULL || io->write == NULL || kernel == NULL) {
        return false;
    }
    result->failed_stage = PIPELINE_OK;
    if (count == 0) {
        return true;
    }
    size_t capacity = options->capacity < count ? options->capacity : count;
    size_t workers = options->workers < capacity ? options->workers : capacity;
    result->failed_stage = PIPELINE_RESOURCE;
    if (capacity > SIZE_MAX / sizeof(Item) || capacity > SIZE_MAX / sizeof(Item *) ||
        workers > SIZE_MAX / sizeof(pthread_t)) {
        return false;
    }
    Item *items = calloc(capacity, sizeof(*items));
    Item **ready = calloc(capacity, sizeof(*ready)), **done = calloc(capacity, sizeof(*done));
    pthread_t *threads = calloc(workers, sizeof(*threads)), writer;
    if (items == NULL || ready == NULL || done == NULL || threads == NULL) {
        free(items);
        free(ready);
        free(done);
        free(threads);
        return false;
    }
    Pipeline p = {.kernel = kernel,
                  .io = io,
                  .result = result,
                  .free_items = items,
                  .ready = {.data = ready},
                  .done = {.data = done},
                  .capacity = capacity,
                  .workers_alive = workers};
    for (size_t i = 0; i + 1 < capacity; ++i) {
        items[i].next = &items[i + 1];
    }
    if (pthread_mutex_init(&p.mutex, NULL) != 0) {
        goto release;
    }
    if (pthread_cond_init(&p.changed, NULL) != 0) {
        pthread_mutex_destroy(&p.mutex);
        goto release;
    }
    size_t started = 0;
    bool writer_started = pthread_create(&writer, NULL, write_results, &p) == 0;
    if (writer_started) {
        for (; started < workers; ++started) {
            if (pthread_create(&threads[started], NULL, compute, &p) != 0) {
                break;
            }
        }
    }
    pthread_mutex_lock(&p.mutex);
    if (!writer_started || started != workers) {
        fail(&p, PIPELINE_RESOURCE, NULL);
    } else {
        result->failed_stage = PIPELINE_OK;
    }
    for (size_t index = 0; index < count && !p.cancelled; ++index) {
        while (!p.cancelled && p.free_items == NULL) {
            pthread_cond_wait(&p.changed, &p.mutex);
        }
        if (p.cancelled) {
            break;
        }
        Item *item = p.free_items;
        p.free_items = item->next;
        item->index = index;
        ++p.live;
        if (p.live > result->peak_in_flight) {
            result->peak_in_flight = p.live;
        }
        pthread_mutex_unlock(&p.mutex);
        item->input = io->read(index, io->context);
        pthread_mutex_lock(&p.mutex);
        if (item->input == NULL) {
            fail(&p, PIPELINE_READ, item);
            break;
        }
        ++result->read;
        if (p.cancelled) {
            break;
        }
        push(&p.ready, item, capacity);
        pthread_cond_broadcast(&p.changed);
    }
    p.reader_done = true;
    pthread_cond_broadcast(&p.changed);
    pthread_mutex_unlock(&p.mutex);
    for (size_t i = 0; i < started; ++i) {
        if (pthread_join(threads[i], NULL) != 0) {
            abort();
        }
    }
    if (writer_started && pthread_join(writer, NULL) != 0) {
        abort();
    }
    pthread_cond_destroy(&p.changed);
    pthread_mutex_destroy(&p.mutex);
release:
    for (size_t i = 0; i < capacity; ++i) {
        image_free(items[i].input);
        image_free(items[i].output);
    }
    free(items);
    free(ready);
    free(done);
    free(threads);
    return result->failed_stage == PIPELINE_OK;
}
