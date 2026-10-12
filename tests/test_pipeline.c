#include "pipeline.h"

#include <float.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define TEST_SEED 0x6a09e667U
#define MAX_IMAGES 128

#define CHECK(c) \
    do { \
        if (!(c)) { \
            fprintf(stderr, "%s:%d: %s\n", __func__, __LINE__, #c); \
            exit(1); \
        } \
    } while (0)

typedef struct {
    const Kernel *kernel;
    size_t capacity, fail_read, fail_write;
    atomic_size_t admitted, finished;
    bool seen[MAX_IMAGES];
    long read_delay, write_delay;
    const char *name;
    uint32_t seed;
    size_t workers;
} Context;

static void delay(long ns)
{
    struct timespec time = {0, ns};
    if (ns != 0) {
        nanosleep(&time, NULL);
    }
}

static Image *fixture(size_t index, uint32_t seed)
{
    Image *image = image_create(1 + index % 17, 1 + index % 11);
    CHECK(image != NULL);
    uint32_t state = seed ^ ((uint32_t)index + 1);
    for (size_t i = 0; i < image->width * image->height; ++i) {
        state = state * 1664525U + 1013904223U;
        image->data[i] = (uint8_t)(state >> 24);
    }
    return image;
}

static Image *read_image(size_t index, void *context)
{
    Context *c = context;
    CHECK(index == atomic_load(&c->admitted));
    size_t admitted = atomic_fetch_add(&c->admitted, 1) + 1;
    size_t live = admitted - atomic_load(&c->finished);
    if (live > c->capacity) {
        fprintf(stderr, "read index=%zu: live=%zu exceeds capacity=%zu\n", index, live,
                c->capacity);
        exit(1);
    }
    delay(c->read_delay);
    return index == c->fail_read ? NULL : fixture(index, c->seed);
}

static bool write_image(size_t index, const Image *image, void *context)
{
    Context *c = context;
    CHECK(index < sizeof(c->seen) / sizeof(c->seen[0]) && !c->seen[index]);
    delay(c->write_delay);
    if (index == c->fail_write) {
        return false;
    }
    Image *input = fixture(index, c->seed), *expected = image_create(input->width, input->height);
    CHECK(expected != NULL && correlate(input, c->kernel, expected));
    CHECK(image->width == expected->width && image->height == expected->height);
    if (memcmp(image->data, expected->data, image->width * image->height) != 0) {
        fprintf(stderr,
                "%s: seed=%u input=%zu image=%zux%zu kernel=%zux%zu workers=%zu capacity=%zu\n",
                c->name, (unsigned)c->seed, index, image->width, image->height, c->kernel->width,
                c->kernel->height, c->workers, c->capacity);
        for (size_t i = 0; i < image->width * image->height; ++i) {
            if (image->data[i] != expected->data[i]) {
                fprintf(stderr, "pixel=%zu expected=%u got=%u\n", i, (unsigned)expected->data[i],
                        (unsigned)image->data[i]);
                break;
            }
        }
        fputs("input pixels:", stderr);
        for (size_t i = 0; i < input->width * input->height; ++i) {
            fprintf(stderr, " %u", (unsigned)input->data[i]);
        }
        fputs("\nkernel:", stderr);
        for (size_t i = 0; i < c->kernel->width * c->kernel->height; ++i) {
            fprintf(stderr, " %.17g", c->kernel->data[i]);
        }
        fputc('\n', stderr);
        exit(1);
    }
    c->seen[index] = true;
    atomic_fetch_add(&c->finished, 1);
    image_free(expected);
    image_free(input);
    return true;
}

static void successful_kernel(const char *name, const Kernel *kernel, uint32_t seed, size_t count,
                              size_t workers, size_t capacity, long read_delay, long write_delay)
{
    Context context = {.kernel = kernel,
                       .capacity = capacity,
                       .fail_read = SIZE_MAX,
                       .fail_write = SIZE_MAX,
                       .read_delay = read_delay,
                       .write_delay = write_delay,
                       .name = name,
                       .seed = seed,
                       .workers = workers};
    PipelineOptions options = {workers, capacity};
    PipelineIO io = {read_image, write_image, &context};
    PipelineResult result;
    if (!pipeline_run(count, kernel, &options, &io, &result)) {
        fprintf(stderr,
                "%s: seed=%u kernel=%zux%zu workers=%zu capacity=%zu count=%zu: "
                "expected success, failed stage=%d index=%zu\n",
                name, (unsigned)seed, kernel->width, kernel->height, workers, capacity, count,
                (int)result.failed_stage, result.failed_index);
        exit(1);
    }
    CHECK(result.read == count && result.processed == count && result.written == count);
    CHECK(result.failed_stage == PIPELINE_OK && result.failed_index == SIZE_MAX);
    CHECK(result.peak_in_flight <= capacity && result.peak_in_flight <= count);
    CHECK(count == 0 || result.peak_in_flight > 0);
    for (size_t i = 0; i < count; ++i) {
        CHECK(context.seen[i]);
    }
    CHECK(atomic_load(&context.admitted) == count && atomic_load(&context.finished) == count);
}

static void successful(const char *name, size_t count, size_t workers, size_t capacity,
                       long read_delay, long write_delay)
{
    double weights[15] = {1.0 / 8, -1.0 / 16, 0, 0, 1.0 / 16, 0,        0,      0.5,
                          0,       0,         0, 0, 0,        -1.0 / 8, 1.0 / 4};
    Kernel kernel = {5, 3, weights};
    successful_kernel(name, &kernel, 0, count, workers, capacity, read_delay, write_delay);
    printf("pass %s (count=%zu workers=%zu capacity=%zu)\n", name, count, workers, capacity);
}

static uint32_t random_word(uint32_t *state)
{
    *state = *state * 1664525U + 1013904223U;
    return *state;
}

static void random_filters_match_sequential(void)
{
    const size_t sides[] = {1, 3, 5, 9, 19};
    const PipelineOptions schedules[] = {{1, 1}, {1, 5}, {4, 3}, {8, 7}};
    uint32_t state = TEST_SEED;
    const size_t batch_size = 23;
    size_t kernel_count = 0, image_count = 0;
    size_t cases = 0;
    const char *families[] = {"normalized_positive", "weak_mixed", "strong_mixed"};
    for (size_t family = 0; family < sizeof(families) / sizeof(families[0]); ++family) {
        for (size_t h = 0; h < sizeof(sides) / sizeof(sides[0]); ++h) {
            for (size_t w = 0; w < sizeof(sides) / sizeof(sides[0]); ++w) {
                Kernel *kernel = kernel_create(sides[w], sides[h]);
                CHECK(kernel != NULL);
                size_t taps = kernel->width * kernel->height;
                double total = 0;
                for (size_t i = 0; i < taps; ++i) {
                    unsigned q = (unsigned)(random_word(&state) >> 24);
                    if (family == 0) {
                        kernel->data[i] = (double)q;
                        total += (double)q;
                    } else {
                        double divisor = family == 1 ? 128.0 * (double)taps : 32.0;
                        kernel->data[i] = (double)((int)q - 128) / divisor;
                    }
                }
                if (family == 0) {
                    if (total == 0) {
                        kernel->data[taps / 2] = 1;
                        total = 1;
                    }
                    for (size_t i = 0; i < taps; ++i) {
                        kernel->data[i] /= total;
                    }
                } else if (family == 1) {
                    kernel->data[taps / 2] += 0.5;
                }
                uint32_t image_seed = random_word(&state);
                for (size_t schedule = 0; schedule < sizeof(schedules) / sizeof(schedules[0]);
                     ++schedule) {
                    successful_kernel(families[family], kernel, image_seed, batch_size,
                                      schedules[schedule].workers, schedules[schedule].capacity, 0,
                                      0);
                    ++cases;
                    image_count += batch_size;
                }
                ++kernel_count;
                kernel_free(kernel);
            }
        }
    }
    printf("pass random_filters_match_sequential (seed=%u, %zu kernels, %zu batches, %zu images)\n",
           TEST_SEED, kernel_count, cases, image_count);
}

static void failure(const char *name, PipelineStage stage, size_t index)
{
    double weight = stage == PIPELINE_CORRELATE ? DBL_MAX : 1;
    Kernel kernel = {1, 1, &weight};
    Context context = {.kernel = &kernel,
                       .capacity = 3,
                       .fail_read = stage == PIPELINE_READ ? index : SIZE_MAX,
                       .fail_write = stage == PIPELINE_WRITE ? index : SIZE_MAX,
                       .name = name,
                       .workers = 4};
    PipelineOptions options = {4, 3};
    PipelineIO io = {read_image, write_image, &context};
    PipelineResult result;
    CHECK(!pipeline_run(100, &kernel, &options, &io, &result));
    CHECK(result.failed_stage == stage);
    if (stage != PIPELINE_CORRELATE) {
        CHECK(result.failed_index == index);
    }
    CHECK(result.failed_index < 100 && result.peak_in_flight <= 3);
    printf("pass %s\n", name);
}

static void invalid_options(void)
{
    double weight = 1;
    Kernel kernel = {1, 1, &weight};
    PipelineOptions options = {0, 1};
    PipelineIO io = {read_image, write_image, NULL};
    PipelineResult result;
    CHECK(!pipeline_run(1, &kernel, &options, &io, &result));
    CHECK(result.failed_stage == PIPELINE_ARGUMENT);
    options = (PipelineOptions){1, 0};
    CHECK(!pipeline_run(1, &kernel, &options, &io, &result));
    CHECK(!pipeline_run(1, &kernel, NULL, &io, &result));
    options.capacity = 1;
    CHECK(!pipeline_run(1, &kernel, &options, NULL, &result));
    CHECK(!pipeline_run(1, NULL, &options, &io, &result));
    CHECK(!pipeline_run(1, &kernel, &options, &io, NULL));
    io.write = NULL;
    CHECK(!pipeline_run(1, &kernel, &options, &io, &result));
    io.write = write_image;
    io.read = NULL;
    CHECK(!pipeline_run(1, &kernel, &options, &io, &result));
    puts("pass pipeline_invalid_options");
}

int main(void)
{
    successful("empty_batch", 0, 4, 3, 0, 0);
    successful("single_image_excess_workers", 1, SIZE_MAX, SIZE_MAX, 0, 0);
    successful("capacity_one", 31, 8, 1, 0, 0);
    successful("many_images_small_queue", MAX_IMAGES, 4, 3, 0, 0);
    successful("slow_reader", 31, 4, 5, 200000, 0);
    successful("slow_writer_backpressure", 31, 4, 5, 0, 300000);
    successful("one_worker", 31, 1, 4, 0, 0);
    successful("more_slots_than_images", 3, 8, 20, 0, 0);
    failure("first_read_fails", PIPELINE_READ, 0);
    failure("read_fails_mid_stream", PIPELINE_READ, 7);
    failure("write_fails_mid_stream", PIPELINE_WRITE, 5);
    failure("correlation_overflows", PIPELINE_CORRELATE, 0);
    invalid_options();
    random_filters_match_sequential();
    return 0;
}
