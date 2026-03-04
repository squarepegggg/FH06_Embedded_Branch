#ifndef GLUEV3_H_
#define GLUEV3_H_

#ifdef __cplusplus
extern "C" {
#endif

int ei_v3_classify_test(const char **out_label, float *out_score);
extern float demo_data[75];

extern const unsigned int ei_model_arena_size;
extern const unsigned int ei_model_tflite_len;

#ifdef __cplusplus
}
#endif

#endif
