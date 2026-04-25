/**
 * run_nn.c - Bridge from BMA400 accel data to Edge Impulse classifier.
 * Fills demo_data, calls ei_v4_classify_test, sets biggest_idx.
 */
#include "run_nn.h"
#include "glueV4.h"
#include <string.h>

int biggest_idx = 0;
float biggest_score = 0.0f;

void run_nn_infer(struct bma400_fifo_sensor_data *accel_data, uint16_t count)
{
	if (count < 25) {
		biggest_idx = 0;
		biggest_score = 0.0f;
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
	int err = ei_v4_classify_test(&label, &score);

	if (err != 0 || label == NULL) {
		biggest_idx = 0;
		biggest_score = 0.0f;
		return;
	}


				   if (strcmp(label, "class 1") == 0)
                       biggest_idx = 0;  /* idle     */
                   else if (strcmp(label, "class 2") == 0)
                       biggest_idx = 1;  /* walk   */
                   else if (strcmp(label, "class 3") == 0)
                       biggest_idx = 2;  /* sixseven    */
                   else if (strcmp(label, "class 4") == 0)
                       biggest_idx = 3;  /* wave  */
                   else if (strcmp(label, "class 5") == 0)
                       biggest_idx = 4;  /* clap  */


	

	else biggest_idx = 0;
	biggest_score = score;
}
