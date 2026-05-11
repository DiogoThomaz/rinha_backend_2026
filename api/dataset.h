// dataset.h
#ifndef DATASET_H
#define DATASET_H

#include <stdint.h>

typedef struct {
    float vector[14];
    uint8_t label;   // 0 = legit, 1 = fraud
} reference_t;

typedef struct {
    reference_t *data;
    uint32_t count;
} dataset_t;

int load_dataset(const char *filename, dataset_t *ds);
void free_dataset(dataset_t *ds);

// Encontra os 5 vizinhos mais próximos e retorna o fraud_score
float find_fraud_score(const dataset_t *ds, const float *query);

#endif