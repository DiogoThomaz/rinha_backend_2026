// dataset.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "dataset.h"

int load_dataset(const char *filename, dataset_t *ds) {
    FILE *f = fopen(filename, "rb");
    if (!f) {
        perror("fopen");
        return -1;
    }

    if (fread(&ds->count, sizeof(uint32_t), 1, f) != 1) {
        fclose(f);
        return -1;
    }

    ds->data = (reference_t *)malloc(ds->count * sizeof(reference_t));
    if (!ds->data) {
        fclose(f);
        return -1;
    }

    for (uint32_t i = 0; i < ds->count; i++) {
        if (fread(ds->data[i].vector, sizeof(float), 14, f) != 14 ||
            fread(&ds->data[i].label, sizeof(uint8_t), 1, f) != 1) {
            free(ds->data);
            fclose(f);
            return -1;
        }
    }

    fclose(f);
    return 0;
}

void free_dataset(dataset_t *ds) {
    free(ds->data);
    ds->data = NULL;
    ds->count = 0;
}

// Mantém um heap máximo dos 5 vizinhos com menor distância
typedef struct {
    float dist;
    uint8_t label;
} neighbor_t;

static void heap_insert(neighbor_t *heap, int *size, float dist, uint8_t label) {
    if (*size < 5) {
        // Insere ordenado (maior distância no topo se size==5)
        int i = *size;
        while (i > 0 && heap[(i-1)/2].dist < dist) {
            heap[i] = heap[(i-1)/2];
            i = (i-1)/2;
        }
        heap[i].dist = dist;
        heap[i].label = label;
        (*size)++;
    } else if (dist < heap[0].dist) {
        // Substitui o topo (maior distância)
        int i = 0;
        heap[0].dist = dist;
        heap[0].label = label;

        // Reorganiza mantendo o maior elemento no topo.
        while (1) {
            int left = 2*i + 1;
            int right = 2*i + 2;
            int largest = i;
            if (left < 5 && heap[left].dist > heap[largest].dist)
                largest = left;
            if (right < 5 && heap[right].dist > heap[largest].dist)
                largest = right;
            if (largest == i) break;
            neighbor_t tmp = heap[i];
            heap[i] = heap[largest];
            heap[largest] = tmp;
            i = largest;
        }
    }
}

float find_fraud_score(const dataset_t *ds, const float *query) {
    neighbor_t heap[5];
    int heap_size = 0;

    for (uint32_t i = 0; i < ds->count; i++) {
        float dist = 0.0f;
        // Loop manual para favorecer SIMD (opcional)
        for (int d = 0; d < 14; d++) {
            float diff = query[d] - ds->data[i].vector[d];
            dist += diff * diff;
        }
        heap_insert(heap, &heap_size, dist, ds->data[i].label);
    }

    int fraud_count = 0;
    for (int i = 0; i < heap_size; i++) {
        if (heap[i].label == 1) fraud_count++;
    }

    if (heap_size == 0) return 0.0f;
    return fraud_count / (float)heap_size;
}