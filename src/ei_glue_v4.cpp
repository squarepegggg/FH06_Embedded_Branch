/**
 * ei_glue_v4.cpp - C-callable wrapper for the ei-v4 Edge Impulse model
 *
 * This file uses the ei-v4 model which expects 75 input features.
 * Input shape: (3, 25, 1) - 3 features, 25 time steps, 1 channel.
 * TensorFlow flattens this column-by-column (all of feature 0, then feature 1, then feature 2).
 */

#include "edge-impulse-sdk/classifier/ei_run_classifier.h"
#include "edge-impulse-sdk/dsp/numpy.hpp"
#include "model-parameters/model_metadata.h"
#include "tflite-model/tflite_learn_801862_22.h"

float demo_data[75] = {};
extern "C" {
    extern const unsigned int ei_model_arena_size  = EI_CLASSIFIER_TFLITE_LARGEST_ARENA_SIZE;
    extern const unsigned int ei_model_tflite_len  = tflite_learn_801862_22_len;
}

extern "C" int ei_v4_classify_test(const char **out_label, float *out_score)
{
    static float features[75];
    for (int i = 0; i < 75; i++) {
        features[i] = demo_data[i];
    }

    signal_t signal;
    if (numpy::signal_from_buffer(features, 75, &signal) != 0) {
        return -98;
    }

    ei_impulse_result_t result = {};
    EI_IMPULSE_ERROR err = run_classifier(&signal, &result, /*debug*/ false);
    if (err != EI_IMPULSE_OK) {
        return static_cast<int>(err);
    }

    size_t best_i = 0;
    float  best_v = result.classification[0].value;
    for (size_t i = 1; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
        if (result.classification[i].value > best_v) {
            best_v = result.classification[i].value;
            best_i = i;
        }
    }

    if (out_label) *out_label = result.classification[best_i].label;
    if (out_score) *out_score = best_v;
    return 0;
}

