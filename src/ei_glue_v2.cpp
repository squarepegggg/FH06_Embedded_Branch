/**
 * ei_glue_v2.cpp - C-callable wrapper for the ei-v2 Edge Impulse model
 * 
 * This file uses the ei-v2 model which expects 75 input features.
 * Input shape: (3, 25, 1) - 3 features, 25 time steps, 1 channel.
 * TensorFlow flattens this column-by-column (all of feature 0, then feature 1, then feature 2).
 * For testing, we use real demo data from the training set.
 */

#include "edge-impulse-sdk/classifier/ei_run_classifier.h"
#include "edge-impulse-sdk/dsp/numpy.hpp"
#include "model-parameters/model_metadata.h"

// Demo data: shape (3, 25, 1) flattened in C order (column-major from original table)
// Order: Column 1 (all 25 rows), Column 2 (all 25 rows), Column 3 (all 25 rows)
float demo_data[75] = {};

extern "C" int ei_v2_classify_test(const char **out_label, float *out_score)
{
    // Copy demo data to mutable buffer (required by signal_from_buffer)
    static float features[75];
    for (int i = 0; i < 75; i++) {
        features[i] = demo_data[i];
    }

    // Wrap features in a signal
    signal_t signal;
    if (numpy::signal_from_buffer(features, 75, &signal) != 0) {
        return -98;
    }

    ei_impulse_result_t result = {};
    EI_IMPULSE_ERROR err = run_classifier(&signal, &result, /*debug*/ false);
    if (err != EI_IMPULSE_OK) {
        return static_cast<int>(err);
    }

    // Find the top class
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
