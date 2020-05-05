#include <string.h>

#include "utils.h"

void error(char* msg) {
	perror(msg);
	exit(1);	
}

void swap(access_t* a, access_t* b) {
	access_t t = *a;
	*a = *b;
	*b = t;
}

int partition(access_t* queue, int l, int h) {
	double pivot = queue[h].timestamp;
	int i = l-1;

	for(int j=l; j<=h-1; j++) {
		if(queue[j].timestamp <= pivot) {
			i++;
			swap(&queue[i], &queue[j]);
		}
	}

	swap(&queue[i+1], &queue[h]);
	return (i+1);
}

void quicksort(access_t* queue, int l, int h) {
	if(l < h) {
		int p = partition(queue, l, h); // p = partitioning index
		quicksort(queue, l, p-1);
		quicksort(queue, p+1, h);
	}
}

// https://stackoverflow.com/questions/1322510/given-an-integer-how-do-i-find-the-next-largest-power-of-two-using-bit-twiddlin
int next_pow2(int n) {
	
	if( (n & (n-1)) == 0) return n; // is already a power of 2

	// printf("The next power of 2 after %i is ", n);	
	int a = n;
	a--;
	
	a |= a >> 1;
	a |= a >> 2;
	a |= a >> 4;
	a |= a >> 8;
	a |= a >> 16;
	
	a++;
	//printf("%i\n", a);
	return a;
}

// from c-programming board ; not mine...
void remove_substring(char* str, char* sub) {
	char* match;
	int len = strlen(sub);
	
	while( (match = strstr(str, sub)) ) {
		*match = '\0';
		strcat(str, match+len);
	}
}

void print_all_config(sim_t* sim) {

	printf("type: insn %i, data %i, unified %i || ", INSN_CACHE, DATA_CACHE, UNIFIED_CACHE);
	printf("inclu: non-inclusive %i, inclusive %i || ", NON_INCLUSIVE, INCLUSIVE);
	printf("evict: plru %i, random %i, sgx_plru %i\n", EVICT_PLRU, EVICT_RAND, EVICT_SGX_PLRU);

	printf("%-10s %-10s %-10s %-10s %-10s %-10s %-10s %-10s %-10s %-10s %-10s %-10s %-10s %-10s %-10s %-10s %-10s\n",
	"name", "id", "level", "type", "shared", "size_kb", "linesize", "inclu", "evict", "sgxp_r", "ways", "e_ways", "sets", "partition", "set_p", "max_set_p", "static_partition");
	
	for(int i=0; i<sim->config_n; i++) {
		cache_config_t* c = &sim->config[i];
		printf("%-10s %-10d %-10d %-10d %-10d %-10d %-10d %-10d %-10d %-10.2f %-10d %-10d %-10d %-10d %-10d %-10d %-10d\n",
		c->name, c->id, c->level, c->type, c->shared, c->size_kb, c->line_size, c->inclu_policy, c->evict_policy, c->sgx_plru_rate, c->ways_n, c->max_enclave_ways_n, c->sets_n,  c->partition, c->set_partition, c->max_partition, c->static_partition);
	}
}

void print_offset_table(sim_t* sim, int max_partition) {
	printf("Global Offset Table\n");
	for(int i=0; i<sim->config_n; i++) {
		printf("%-9s ", sim->config[i].name);
	}
	printf("\n");
	for(int i=0; i<max_partition; i++) {
		for(int j=0; j<sim->config_n; j++) {	
			printf("%-9i ", sim->offset_table[i][j]);	
		}
		printf("\n");
    }
}
