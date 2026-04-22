/**
 * run_nn.c - Bridge from BMA400 accel data to Edge Impulse classifier.
 * Fills demo_data, calls ei_v2_classify_test, sets biggest_idx.
 */
#include "run_nn.h"
#include "glueV2.h"
#include <string.h>

int biggest_idx = 0;

void run_nn_infer(struct bma400_fifo_sensor_data *accel_data, uint16_t count)
{
	if (count < 25) {
		biggest_idx = 0;
		return;
	}

	/* Copy 25 samples into demo_data: X at 0-24, Z at 25-49, Y at 50-74 (matches main.c layout) */
	for (int i = 0; i < 25; i++) {
		demo_data[i]      = (float)accel_data[i].x;
		demo_data[i + 25] = (float)accel_data[i].z;
		demo_data[i + 50] = (float)accel_data[i].y;
	}

	const char *label = NULL;
	float score = 0.0f;
	int err = ei_v2_classify_test(&label, &score);

	if (err != 0 || label == NULL) {
		biggest_idx = 0;
		return;
	}

	/* Map EI labels to index 0-4 (matches ML/model.py CLASSES) */
	if (strcmp(label, "idle") == 0)            biggest_idx = 0;
	else if (strcmp(label, "walk") == 0)       biggest_idx = 1;
	else if (strcmp(label, "sixseven") == 0)   biggest_idx = 2;
	else if (strcmp(label, "wave") == 0)       biggest_idx = 3;
	else if (strcmp(label, "clap") == 0)       biggest_idx = 4;
	else biggest_idx = 0;
}
