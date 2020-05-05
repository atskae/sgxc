#ifndef SIM_H
#define SIM_H

#define _GNU_SOURCE
#include <stdint.h>

#include "cache.h"

#define MAX_LEVEL 4
#define MAX_CACHE_CONFIG MAX_LEVEL * 2
#define ADDR_BITS 64

#define MAX_TRACEFILES 32 // maximum number of unique trace files
#define TRACE_N_CONTEXT_SWITCH 1000 // arbitrary

#define START_STAT 100000000ull // start collecting stats after this many traces
#define MAX_TRACES (START_STAT + 10000000000ull) // when to stop simulation

typedef struct cache_config_t cache_config_t;
typedef struct cache_t cache_t;
typedef struct core_t core_t;

typedef struct nstat_t {
    char name[256];
    char desc[512];
} nstat_t;

// holds the counters for the statistics ; non-enclave and enclave
typedef struct nstat_count_t {
    uint64_t count[2];
} nstat_count_t;

// source: http://www.linux-pages.com/2013/02/how-to-map-enum-to-strings-in-c/
// create the events enum from events.h
#undef ADD_EVENT
#define ADD_EVENT(_event, _desc) _event
enum
{
    #include "events.h"
    NUM_EVENTS
};
#undef ADD_EVENT

// creates enum -> nstat_t mappings 
#undef ADD_EVENT
#define ADD_EVENT(_event, _desc) [_event]={#_event, _desc}
static const nstat_t all_stats[NUM_EVENTS] =
{
    #include "events.h"
};
#undef ADD_EVENT

enum PrefetchPolicy{prefetchNone=0, nextLine=1, nextTwoLines=2};

typedef struct tracefile_t {
	char filename[256];
	char* file_path;
    size_t size; // for choosing a random offset within range
    int always; // treat either as always enclave mode or not ; -1 if trace mixes

	int threads_n;
	int threads_launched; // number of threads that were scheduled onto a core
    int threads_completed; // number of threads that read through entire trace file
} tracefile_t;

typedef struct access_t {
	int eid;
	int core_id;

	double interval;	
	int enclave_mode;
	uint64_t addr;
	int op;
	
	double timestamp;
} access_t;

typedef struct process_t {	
	char valid; // a core may have have room for more processes then there are processes
	int eid; // unique id across cores and run
	char done;
    access_t* access; // holds the current access
	
	FILE* trace; // file stream		
	tracefile_t* tracefile; // tracefile info	
	long int trace_offset; // starting offset into the trace file
	int seen_offset_n; // how many times looped around trace file
	
	core_t* core;

	int sat_idx; // use the same index into Set Allocation Table for each cache
	int eway_idx; // which way this enclave accesses
	int** offset_table; // pointer to global offset table	

    nstat_count_t* nstat_counts;
    int partition_factor;
    
    // dynamic cachelets
    uint64_t miss_counter; // indicates when the size expands
    int num_cachelets; // used to compute the range of accessible cache sets
} process_t;

typedef struct core_t {
	int id;	
	double clock; // added intervals of memory traces

	int process_n; // number of processes on this core
	int current_process; // index into processes
	process_t* processes; // context-switched out

	int** offset_table;
	cache_t* cache; // private to core
    //nstat_count_t* nstat_counts;
} core_t;

typedef struct sim_t {	
	char stop_early; // stop simulation when the first program completes
	int next_eid; // the next eid to assign to a new process
    char test; // if testing a new feature while things are running, the config files should specify test: 1
    enum PrefetchPolicy prefetch; // prefetching policy 
    char ignore_ne; // if true, ignore all non-enclave accesses
    uint64_t trace_n; // indicates when to start simulating
    int cachelet_assoc; // how many ways each cachelet gets
    int max_partition;
	
	tracefile_t tracefiles[MAX_TRACEFILES];
	int tracefiles_n;
	int tracefile_ptr; // which thread to schedule next
	access_t* queue; // queue of traces
	
	cache_config_t* config;
    int config_n; // number of cache configs
	int prog_n;	// total number of programs in the simulation

	core_t* cores; // CPU core, with private cache ; use core_id to index into	
	int cores_n;
	int progs_per_core;
    int* eid_to_core_id; // index using eid ; obtain the id of the core this process is located ; used when evicting other lines with inclusive policy

    char uses_inclusive; // if true, then there is an inclusive cache somewhere in the cache hierarchy, so evictions may cause additional evictions
	cache_t* cache; // shared cache		
	int** offset_table; // precalculated ; values never change ; enclaves can use the same sat_idx to index into this table
	    
	double elapsed; // duration of simulation

    char* nstat_file; // <config>.<prog>.nstat.csv
    char* config_file; // <config>.<prog>.config.csv
    nstat_count_t* nstat_counts;

    // dynamic cachelets
    uint64_t dyn_threshold; // when miss_counter reaches this number, expand enclave cache size
    uint64_t dyn_rate; // how often to check miss_counter and threshold to upsize
    uint64_t dyn_downsize_threshold; // when miss_counter reaches this number, halfen enclave cache size
    uint64_t dyn_downsize_rate; // how often to check miss_counter and threshold to downsize

    FILE* miss_csv; // csv of misses over time
} sim_t;

// prints all events and their numbers from events.h
void print_all_events();

void get_all_stats(sim_t* sim);
void get_all_config(sim_t* sim);

void set_stat_count(nstat_count_t* counts, int EVENT, int enclave_mode, uint64_t new_count);
uint64_t get_stat_count(nstat_count_t* counts, int EVENT, int enclave_mode);
void update_stat_all(sim_t* sim, cache_t* c, int cache_type, process_t* p, int EVENT, int enclave_mode);
void update_stat(sim_t* sim, nstat_count_t* counts, int EVENT, int enclave_mode);
void update_stat_mem_access(sim_t* sim, nstat_count_t* counts, int op, int enclave_mode);
void update_stat_partition_time(sim_t* sim, nstat_count_t* counts, int partition_factor, int enclave_mode);
void alloc_and_reset_counts(nstat_count_t** counts);

void parse_files(sim_t* sim, char* config, char* prog_file);
void set_next_process(core_t* core);
void init_sim(sim_t* sim, char* argv[]);

#endif /* SIM_H */
