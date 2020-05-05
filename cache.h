#ifndef CACHE_H
#define CACHE_H

#define _GNU_SOURCE

#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include <assert.h>

#include "sim.h"

/* cache types */
#define INSN_CACHE 0
#define DATA_CACHE 1
#define UNIFIED_CACHE 2

/* cache inclusion policies*/
#define NON_INCLUSIVE 0
#define INCLUSIVE 1

/* cache eviction policies */
#define EVICT_PLRU 0
#define EVICT_RAND 1
#define EVICT_SGX_PLRU 2

/* cache insertion policies */
#define INSERT_PMRU 0 // default
#define INSERT_PLRU 1 

/* memory access types */
#define LOAD_OP 0
#define STORE_OP 1
#define INSN_OP 2
#define NOP 3 // for simply searching a set and not doing anything (for invalidating lines) ; does not update plru

/* memory acccess modes */
#define NON_ENCLAVE 0
#define ENCLAVE 1

/* stats */
#define CACHE_MISS 0
#define CACHE_COLD_MISS 1
#define CACHE_HIT 2

// edit_line() operations ; if inclusive it will evict/set in other caches
#define EVICT_LINE 0
#define SET_LINE 1

// search_cache(() ; this function always returns a hit or a miss ; can specify an action after the search is done
#define SEARCH_LINE 1// simply returns which way a cache line is ; cache content is not modified
#define PLACE_LINE 2 // evicts a line AND sets a line

typedef struct sim_t sim_t;
//typedef struct stat_t stat_t;
typedef struct nstat_t nstat_t;
//typedef struct all_nstats_t all_nstats_t;
typedef struct nstat_count_t nstat_count_t;

typedef struct access_t access_t;
typedef struct process_t process_t;
typedef struct core_t core_t;
typedef struct cache_t cache_t;
typedef struct cache_config_t cache_config_t;

typedef struct cacheline_t {
	char valid;
	int eid;
	uint64_t tag;
    int enclave_mode; // enclave line or not ; need to know when evicting
    char dirty; // if a dirty enclave line gets evicted, an encryption overhead occurs
} cacheline_t;

typedef struct sat_entry_t {
	char valid;
	int eid;
} sat_entry_t;

// information about enclave way
typedef struct enclave_way_info_t {
	char valid;
	int set_bits_n;
	int alloc_n; // number of partitions allocated to enclaves 
	sat_entry_t* sat;	
	char* sat_plru;	
} enclave_way_info_t;

typedef struct cache_config_t {
	
	/* cache info */	
	int id;

	char* name; // for printing
	int type; // data, insn, unified
	char shared; // "0" private ; "1" shared
	int inclu_policy;
	int evict_policy;
	int insert_policy;
    char partition; // "0" ignore enclave_ways_n ; "1" give enclave_ways_n ways to enclaves
	char set_partition; // "0" all enclaves share a way ; "1" each enclave gets a chunk of cache way
    char static_partition; // "0" number of enclaves ways is allocated at the beginning (no growing/shrinking)
    char use_cachelet; // "1" use a fixed partition size throughout (no growing/shrinking)
    float sgx_plru_rate; // probablility that sgx_plru eviction policy is used ; should be between 0.0 - 1.0
	
	int level;
	int size_kb;
    int size_b; // can specify cache size in bytes 
	int ways_n;
	int sets_n; 
	int line_size; // bytes

	/* address calculation */
	int addr_bits_n; // number of bits in address
	int offset_bits_n;
	int set_bits_n;
	int tag_bits_n;
	uint64_t set_mask;
	uint64_t tag_mask;
//aaaajjjjjjjjaaaaasssssssshinanhhi ynam eiashi name ijjjjjjjhi name itiaohi thihhh  ; <--- testing a mechanical keyboard

    /* set information for cachelet support */
    int static_cachelets; // preallocated space ; cannot be used during simulation (for testing non-enclave programs)
    uint64_t* way_bitmaps; // size max_partition ; upper bits of set index bits -> a bitmap of size ways_n , indicates which ways are occupied by enclaves ; use at replacement for non-enclaves
	
	/* enclave way */
	int max_enclave_ways_n;	
    int max_partition; // per way
	int enclave_ways_n; // current rumber of ways allocated to enclaves ; changes over time
	enclave_way_info_t* eway_info;

} cache_config_t;

typedef struct cache_t {
	
	char unified;

	cache_config_t* config[3];	
	cacheline_t** cache[3]; // actual cache content ; index using cache type (insn, data, unified)
	char** plru[3]; // binary search tree for eviction	
    nstat_count_t* nstat_counts[3];

	cache_t* next; // next level of cache
			
} cache_t;

void init_cache(sim_t* sim);

int pick_victim_way(sim_t* sim, process_t* p, cache_t* c, cache_config_t* config, int cache_type, int set_idx, int enclave_mode);
void update_plru(char* plru, int slots_n, int slot_accessed);

int evict_sgx_plru(cache_t* c, cache_config_t* config, int cache_type, int set_idx);
void set_line(process_t* p, cacheline_t* cl, uint64_t tag);

void free_partition(sim_t* sim, process_t* p, char process_finished);
void access_cache(sim_t* sim, process_t* p);

#endif /* CACHE_H */
