#ifndef UTILS_H
#define UTILS_H

#define _GNU_SOURCE

#include <stdlib.h>
#include "cache.h"

void error(char* msg);

void swap(access_t* a, access_t* b);
int partition(access_t* queue, int l, int h);
void quicksort(access_t* queue, int l, int h);

int next_pow2(int n);
void remove_substring(char* str, char* sub);

void print_all_config(sim_t* sim);
void print_offset_table(sim_t* sim, int max_partition);

#endif /* UTILS_H */
