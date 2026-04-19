#ifndef RUN_NN_H_
#define RUN_NN_H_

#include <stdint.h>
#include "bma400_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Run inference on accel_data and set biggest_idx (0-4 for EI model classes). */
void run_nn_infer(struct bma400_fifo_sensor_data *accel_data, uint16_t count);

/* Set by run_nn_infer: 0=idle, 1=clap, 2=sixseven, 3=spinning, 4=walking */
extern int biggest_idx;

#ifdef __cplusplus
}
#endif

#endif /* RUN_NN_H_ */
