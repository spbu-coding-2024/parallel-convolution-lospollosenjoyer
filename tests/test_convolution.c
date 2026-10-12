#include "convolution.h"

#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WEIGHT_SCALE 32
#define TEST_SEED 1U

#define CHECK(name, condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "%s:%d: %s failed: %s\n", __func__, __LINE__, name, #condition); \
            exit(EXIT_FAILURE); \
        } \
    } while (0)

static void expect(const char *name, const Image *input, const Kernel *kernel, const Image *output,
                   const uint8_t *expected)
{
    CHECK(name, input->width == output->width && input->height == output->height);
    for (size_t i = 0; i < output->width * output->height; ++i) {
        if (output->data[i] != expected[i]) {
            fprintf(stderr, "%s: pixel (%zu,%zu): expected %u, got %u\n", name, i % output->width,
                    i / output->width, (unsigned)expected[i], (unsigned)output->data[i]);
            fprintf(stderr, "input (%zux%zu):", input->width, input->height);
            for (size_t j = 0; j < input->width * input->height; ++j) {
                fprintf(stderr, " %u", (unsigned)input->data[j]);
            }
            fprintf(stderr, "\nkernel (%zux%zu):", kernel->width, kernel->height);
            for (size_t j = 0; j < kernel->width * kernel->height; ++j) {
                fprintf(stderr, " %.17g", kernel->data[j]);
            }
            fputc('\n', stderr);
            exit(EXIT_FAILURE);
        }
    }
}

static void fixed_cases(void)
{
    double summed_kernel[35];
    for (size_t i = 0; i < sizeof(summed_kernel) / sizeof(summed_kernel[0]); ++i) {
        summed_kernel[i] = (double)(i + 1) / 64.0;
    }
    const struct {
        const char *name;
        Image input;
        Kernel kernel;
        const uint8_t *expected;
    } cases[] = {
        {"asymmetric_horizontal",
         {4, 1, (uint8_t[]){10, 20, 30, 40}},
         {3, 1, (double[]){1, 2, 4}},
         (uint8_t[]){140, 170, 240, 150}},
        {"asymmetric_vertical",
         {1, 4, (uint8_t[]){10, 20, 30, 40}},
         {1, 3, (double[]){1, 2, 4}},
         (uint8_t[]){140, 170, 240, 150}},
        {"impulse_response_of_correlation",
         {5, 5, (uint8_t[25]){[12] = 1}},
         {3, 3, (double[]){1, 2, 3, 4, 5, 6, 7, 8, 9}},
         (uint8_t[]){0, 0, 0, 0, 0, 0, 9, 8, 7, 0, 0, 6, 5, 4, 0, 0, 3, 2, 1, 0, 0, 0, 0, 0, 0}},
        {"wrap_at_every_corner",
         {2, 2, (uint8_t[]){1, 2, 3, 4}},
         {3, 3, (double[]){1, 1, 1, 1, 1, 1, 1, 1, 1}},
         (uint8_t[]){27, 24, 21, 18}},
        {"rectangular_image_with_mixed_2d_kernel",
         {2, 3, (uint8_t[]){1, 2, 3, 4, 5, 6}},
         {3, 3, (double[]){1, 2, 0, 0, -1, 0, 0, 0, 3}},
         (uint8_t[]){27, 24, 19, 16, 11, 8}},
        {"kernel_larger_than_single_pixel",
         {1, 1, (uint8_t[]){7}},
         {5, 7, summed_kernel},
         (uint8_t[]){69}},
        {"negative_coordinate_wraps_multiple_times",
         {3, 1, (uint8_t[]){3, 7, 11}},
         {11, 1, (double[11]){1}},
         (uint8_t[]){7, 11, 3}},
        {"positive_coordinate_wraps_multiple_times",
         {1, 3, (uint8_t[]){3, 7, 11}},
         {1, 11, (double[11]){[10] = 1}},
         (uint8_t[]){11, 3, 7}},
        {"mixed_kernel_wraps_multiple_times_on_both_axes",
         {3, 2, (uint8_t[]){10, 20, 30, 40, 50, 60}},
         {13, 9, (double[117]){[0] = 0.5, [14] = -0.25, [116] = 0.75}},
         (uint8_t[]){0, 10, 28, 45, 55, 73}},
        {"round_half_up",
         {5, 1, (uint8_t[]){0, 1, 2, 3, 255}},
         {1, 1, (double[]){0.5}},
         (uint8_t[]){0, 1, 1, 2, 128}},
        {"round_below_and_above_half",
         {3, 1, (uint8_t[]){1, 2, 3}},
         {1, 1, (double[]){0.25}},
         (uint8_t[]){0, 1, 1}},
        {"clamp_upper_boundary",
         {3, 1, (uint8_t[]){127, 128, 255}},
         {1, 1, (double[]){2}},
         (uint8_t[]){254, 255, 255}},
        {"clamp_negative_result",
         {3, 1, (uint8_t[]){1, 127, 255}},
         {1, 1, (double[]){-2}},
         (uint8_t[]){0, 0, 0}},
        {"round_only_after_accumulation",
         {1, 1, (uint8_t[]){1}},
         {3, 1, (double[]){0.25, 0.25, 0.25}},
         (uint8_t[]){1}},
        {"clamp_only_after_accumulation",
         {1, 1, (uint8_t[]){100}},
         {3, 1, (double[]){-3, 4, -0.5}},
         (uint8_t[]){50}},
        {"zero_filter_overwrites_existing_output",
         {3, 2, (uint8_t[]){1, 7, 4, 200, 5, 3}},
         {3, 3, (double[9]){0}},
         (uint8_t[6]){0}}};
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        Image *output = image_create(cases[i].input.width, cases[i].input.height);
        CHECK(cases[i].name, output != NULL);
        memset(output->data, 9, output->width * output->height);
        CHECK(cases[i].name, correlate(&cases[i].input, &cases[i].kernel, output));
        expect(cases[i].name, &cases[i].input, &cases[i].kernel, output, cases[i].expected);
        image_free(output);
        printf("pass %s\n", cases[i].name);
    }
}

/* Marsaglia's xorshift32 (13, 17, 5); state must be nonzero. */
static uint8_t random_pixel(uint32_t *state)
{
    *state ^= *state << 13;
    *state ^= *state >> 17;
    *state ^= *state << 5;
    return (uint8_t)(*state >> 24);
}

static void apply(const char *name, const Image *input, const Kernel *kernel, Image *output)
{
    if (!correlate(input, kernel, output)) {
        fprintf(stderr, "%s: correlate failed for image %zux%zu, kernel %zux%zu\n", name,
                input->width, input->height, kernel->width, kernel->height);
        exit(EXIT_FAILURE);
    }
}

static void expect_reference(const Image *input, const Kernel *kernel, const Image *output)
{
    size_t count = input->width * input->height;
    int64_t *sums = calloc(count, sizeof(*sums));
    uint8_t *expected = malloc(count);
    CHECK("reference buffers", sums != NULL && expected != NULL);
    for (size_t src = 0; src < count; ++src) {
        for (size_t tap = 0; tap < kernel->width * kernel->height; ++tap) {
            int64_t x = (int64_t)(src % input->width) + (int64_t)(kernel->width / 2) -
                        (int64_t)(tap % kernel->width);
            int64_t y = (int64_t)(src / input->width) + (int64_t)(kernel->height / 2) -
                        (int64_t)(tap / kernel->width);
            while (x < 0) {
                x += (int64_t)input->width;
            }
            while (x >= (int64_t)input->width) {
                x -= (int64_t)input->width;
            }
            while (y < 0) {
                y += (int64_t)input->height;
            }
            while (y >= (int64_t)input->height) {
                y -= (int64_t)input->height;
            }
            int64_t numerator = (int64_t)(kernel->data[tap] * WEIGHT_SCALE);
            sums[(size_t)y * input->width + (size_t)x] += input->data[src] * numerator;
        }
    }
    for (size_t i = 0; i < count; ++i) {
        expected[i] = (uint8_t)(sums[i] <= 0 ? 0
                                : sums[i] >= INT64_C(255) * WEIGHT_SCALE
                                    ? 255
                                    : (sums[i] + WEIGHT_SCALE / 2) / WEIGHT_SCALE);
    }
    expect("dense_reference", input, kernel, output, expected);
    free(expected);
    free(sums);
}

static void generated_properties(void)
{
    const size_t sides[] = {1, 3, 5, 9, 15, 31};
    const size_t side_count = sizeof(sides) / sizeof(sides[0]);
    uint32_t state = TEST_SEED;
    size_t cases = 0;
    for (size_t height = 1; height <= 11; ++height) {
        for (size_t width = 1; width <= 17; ++width) {
            Image *input = image_create(width, height);
            Image *first = image_create(width, height);
            Image *second = image_create(width, height);
            CHECK("property buffers", input != NULL && first != NULL && second != NULL);
            for (size_t i = 0; i < width * height; ++i) {
                input->data[i] = random_pixel(&state);
            }
            for (size_t h = 0; h < side_count; ++h) {
                for (size_t w = 0; w < side_count; ++w) {
                    size_t kw = sides[w], kh = sides[h];
                    Kernel *kernel = kernel_create(kw, kh);
                    Kernel *padded = kernel_create(kw + 2, kh + 4);
                    CHECK("property kernels", kernel != NULL && padded != NULL);

                    const char *name = "identity_on_generated_shapes";
                    kernel->data[(kh / 2) * kw + kw / 2] = 1;
                    apply(name, input, kernel, first);
                    expect(name, input, kernel, first, input->data);

                    name = "inverse_cyclic_shifts_restore_image";
                    kernel->data[(kh / 2) * kw + kw / 2] = 0;
                    kernel->data[0] = 1;
                    apply(name, input, kernel, first);
                    kernel->data[0] = 0;
                    kernel->data[kw * kh - 1] = 1;
                    apply(name, first, kernel, second);
                    expect(name, input, kernel, second, input->data);

                    name = "zero_extension_of_kernel_is_invariant";
                    for (size_t y = 0; y < kh; ++y) {
                        for (size_t x = 0; x < kw; ++x) {
                            double weight =
                                (double)((int)(random_pixel(&state) % 9) - 4) / WEIGHT_SCALE;
                            kernel->data[y * kw + x] = weight;
                            padded->data[(y + 2) * padded->width + x + 1] = weight;
                        }
                    }
                    apply("dense_reference", input, kernel, first);
                    expect_reference(input, kernel, first);
                    apply(name, input, padded, second);
                    expect(name, input, padded, second, first->data);
                    ++cases;
                    kernel_free(kernel);
                    kernel_free(padded);
                }
            }
            image_free(input);
            image_free(first);
            image_free(second);
        }
    }
    printf("pass identity_on_generated_shapes (%zu cases)\n", cases);
    printf("pass inverse_cyclic_shifts_restore_image (%zu cases)\n", cases);
    printf("pass zero_extension_of_kernel_is_invariant (%zu cases)\n", cases);
    printf("pass dense_reference (%zu cases)\n", cases);
}

static void inputs_remain_unchanged(void)
{
    uint8_t pixels[] = {1, 3, 7, 11, 13, 17}, pixel_copy[6];
    double weights[] = {2, -1, 3};
    unsigned char weight_copy[sizeof(weights)];
    memcpy(pixel_copy, pixels, sizeof(pixels));
    memcpy(weight_copy, weights, sizeof(weights));
    Image input = {3, 2, pixels};
    Kernel kernel = {3, 1, weights};
    Image *output = image_create(3, 2);
    CHECK(__func__, output != NULL && correlate(&input, &kernel, output));
    CHECK(__func__, memcmp(pixel_copy, pixels, sizeof(pixels)) == 0);
    CHECK(__func__, memcmp(weight_copy, (const unsigned char *)weights, sizeof(weights)) == 0);
    image_free(output);
    puts("pass inputs_remain_unchanged");
}

static void invalid_arguments(void)
{
    uint8_t data[] = {1, 2}, in_place_data[] = {2}, bright_data[] = {255}, result[] = {99};
    double weights[] = {1, 2}, triple[] = {3}, nonfinite[] = {NAN}, huge[] = {DBL_MAX};
    Image input = {1, 1, data}, wide = {2, 1, data}, output = {1, 1, result};
    Image in_place = {1, 1, in_place_data}, bright = {1, 1, bright_data};
    Kernel valid = {1, 1, weights}, even = {2, 1, weights}, times_three = {1, 1, triple};
    Kernel nan_kernel = {1, 1, nonfinite}, overflowing = {1, 1, huge};
    const struct {
        const char *name;
        const Image *input;
        const Kernel *kernel;
        Image *output;
    } cases[] = {{"rejects_even_kernel", &input, &even, &output},
                 {"rejects_in_place_output", &in_place, &times_three, &in_place},
                 {"rejects_output_shape_mismatch", &wide, &valid, &output},
                 {"rejects_null_input", NULL, &valid, &output},
                 {"rejects_nonfinite_kernel", &input, &nan_kernel, &output},
                 {"rejects_overflowing_sum", &bright, &overflowing, &output}};
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        uint8_t original = cases[i].output->data[0];
        CHECK(cases[i].name, !correlate(cases[i].input, cases[i].kernel, cases[i].output));
        CHECK(cases[i].name, cases[i].output->data[0] == original);
        printf("pass %s\n", cases[i].name);
    }
    CHECK("rejects_zero_dimension", image_create(0, 1) == NULL);
    CHECK("rejects_too_large_dimension", image_create(SIZE_MAX, 2) == NULL);
    CHECK("rejects_unrepresentable_kernel_size", kernel_create(INT_MAX, INT_MAX) == NULL);
    CHECK("rejects_even_kernel_allocation", kernel_create(1, 2) == NULL);
    puts("pass allocation_errors (4 cases)");
}

int main(void)
{
    fixed_cases();
    generated_properties();
    inputs_remain_unchanged();
    invalid_arguments();
    return EXIT_SUCCESS;
}
