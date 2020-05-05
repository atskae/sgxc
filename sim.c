#define _GNU_SOURCE
#include <stdio.h>

#define __STDC_FORMAT_MACROS // for printing uint64_t
#include <inttypes.h>

#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <libgen.h> // basename()
#include <time.h> // rand()

#include "sim.h"
#include "utils.h"
#include "cache.h"

// ex. saving the maximum or minimum
void set_stat_count(nstat_count_t* counts, int EVENT, int enclave_mode, uint64_t new_count) {
    if(EVENT >= NUM_EVENTS) return;
    counts[EVENT].count[enclave_mode] = new_count;
}

uint64_t get_stat_count(nstat_count_t* counts, int EVENT, int enclave_mode) {
    if(EVENT >= NUM_EVENTS) return 0;
    return counts[EVENT].count[enclave_mode];
}

void update_stat(sim_t* sim, nstat_count_t* counts, int EVENT, int enclave_mode) {
    if(EVENT >= NUM_EVENTS) return;

    if(sim->trace_n >= START_STAT) {
        counts[EVENT].count[enclave_mode]++;
    }
}

void update_stat_all(sim_t* sim, cache_t* c, int cache_type, process_t* p, int EVENT, int enclave_mode) {
    update_stat(sim, sim->nstat_counts, EVENT, enclave_mode);
    update_stat(sim, c->nstat_counts[cache_type], EVENT, enclave_mode);
    update_stat(sim, p->nstat_counts, EVENT, enclave_mode);
}

void update_stat_mem_access(sim_t* sim, nstat_count_t* counts, int op, int enclave_mode) {
    
    int event = STAT_INVALID;
    switch(op) {
        case LOAD_OP:
            event = STAT_LOAD;
            break;
        case STORE_OP:
            event = STAT_STORE;
            break;
        case INSN_OP:
            event = STAT_INSN;
            break;
        default:
            break;
    }
    update_stat(sim, counts, event, enclave_mode);
    update_stat(sim, counts, STAT_TRACE, enclave_mode);
}

void update_stat_partition_time(sim_t* sim, nstat_count_t* counts, int partition_factor, int enclave_mode) {
   
    if(!enclave_mode) return;

    int event = STAT_64P_PARTITION;
    switch(partition_factor) {
        case 0:
            event = STAT_NO_PARTITION;
            break;
        case 1:
            event = STAT_NO_PARTITION;
            break;
        case 2:
            event = STAT_2_PARTITION;
            break;
        case 4:
            event = STAT_4_PARTITION;
            break;
        case 8:
            event = STAT_8_PARTITION;
            break;
        case 16:
            event = STAT_16_PARTITION;
            break;
        case 32:
            event = STAT_32_PARTITION;
            break;
        default:
            if(partition_factor < 64) event = STAT_INVALID;
            break;
    }
    update_stat(sim, counts, event, enclave_mode);
}

void alloc_and_reset_counts(nstat_count_t** counts) {
    if(*counts != NULL) {
        printf("Counts may have allocated values. Free them before resetting counts.\n");
        return;
    }

    *counts = (nstat_count_t*) malloc(sizeof(nstat_count_t) * NUM_EVENTS);
    for(int i=0; i<NUM_EVENTS; i++) {
        memset( &(*counts)[i], 0, sizeof(nstat_count_t) );
    }
}

void write_all_stats(FILE* file, nstat_count_t* counts, char* name, int core_id) {
    for(int i=0; i<NUM_EVENTS; i++) {
        uint64_t non_enclave = counts[i].count[NON_ENCLAVE];
        uint64_t enclave = counts[i].count[ENCLAVE];
        uint64_t total = non_enclave + enclave;
        fprintf(file, "%s::core%i::%s::ne%lu::e%lu::t%lu%16c%s\n", name, core_id, all_stats[i].name, non_enclave, enclave, total, '#', all_stats[i].desc);
    }
}

// records the cache configuration used
void get_all_config(sim_t* sim) {
   
    FILE* st = fopen(sim->config_file, "w");	
    if(!st) {
        printf("Failed to open %s\n", sim->config_file);
        return;
    } 
	// cache configurations
	fprintf(st,
	"name,"
	"type,"
	"shared,"
	"inclu_policy,"
	"evict_policy,"
	"sgx_plru_rate,"
    "partition,"
	"level,"
	"size_kb,"
    "size_b,"
	"ways_n,"
	"sets_n,"
	"max_enclave_ways_n,"
	"line_size,"
	"set_partition,"
	"max way partition\n");
	for(int i=0; i<sim->config_n; i++) {
		cache_config_t* c = &sim->config[i];
		
		// name
		fprintf(st, "%s,", c->name);
		if(c->type == DATA_CACHE) fprintf(st,"data,");
		
		// type
		else if(c->type == INSN_CACHE) fprintf(st, "insn,");
		else if(c->type == UNIFIED_CACHE) fprintf(st, "unified,");
		else fprintf(st, "?,");

		// shared	
		fprintf(st, "%i,", c->shared);

		// inclu_policy
		if(c->inclu_policy == NON_INCLUSIVE) fprintf(st, "non-inclusive,");
		else if(c->inclu_policy == INCLUSIVE) fprintf(st, "inclusive,");
		else fprintf(st, "?,");

		// evict_policy	
		if(c->evict_policy == EVICT_PLRU) fprintf(st, "plru,");
		else if(c->evict_policy == EVICT_RAND) fprintf(st, "random,");
		else if(c->evict_policy == EVICT_SGX_PLRU) fprintf(st, "sgx_plru,");
        else fprintf(st, "?,");

		fprintf(st,
		"%.2f," // sgx_plru_rate
		"%i," // partition
        "%i," // level
		"%i," // size_kb
        "%i," // size_b
		"%i,"
		"%i,"
		"%i,"
		"%i bytes,"
		"%i,"
		"%i\n",	
		c->sgx_plru_rate,
        c->partition,
        c->level,
		c->size_kb,
        c->size_b,
		c->ways_n,
		c->sets_n,
		c->max_enclave_ways_n,
		c->line_size,
		c->set_partition,
		c->max_partition); 
    }

    fprintf(st,	
	"sim time (min),"	
	"start after (insn),"
    "max traces,"
    "cores,"
    "prefetch,"
    "dyn_threshold,"
    "dyn_rate,"
    "dyn_downsize_threshold,"
    "dyn_downsize_rate\n"
	"%.5f,"
    "%llu," // START_STAT
    "%llu," // total traces
	"%i," // number of cores
    "%i," // prefetch policy
    "%" PRIu64 "," // dyn_threshold
    "%" PRIu64 "," // dyn_rate
    "%" PRIu64 "," // dyn_downsize_threshold
    "%" PRIu64 "\n", // dyn_downsize_rate
	sim->elapsed/60,
    START_STAT,
    MAX_TRACES-START_STAT,
	sim->cores_n,
    sim->prefetch,
    sim->dyn_threshold,
    sim->dyn_rate,
    sim->dyn_downsize_threshold,
    sim->dyn_downsize_rate);

    int ret = fclose(st);
    if(ret != 0) printf("Failed to close %s\n", sim->config_file);
    else printf("Config written to %s\n", sim->config_file);
}

void get_all_stats(sim_t* sim) {
  
    FILE* file = fopen(sim->nstat_file, "w"); 
    if(!file) {
        printf("Failed to open %s\n", sim->nstat_file);
    }
    write_all_stats(file, sim->nstat_counts, "sim", 0); // core_id
    
    for(int i=0; i<sim->cores_n; i++) {
        // cache stats on this core
        core_t* core = &sim->cores[i];
        cache_t* c = core->cache;
        while(c) {
            if(c->unified) {
                write_all_stats(file, c->nstat_counts[UNIFIED_CACHE], c->config[UNIFIED_CACHE]->name, core->id);
            } else {
                write_all_stats(file, c->nstat_counts[INSN_CACHE], c->config[INSN_CACHE]->name, core->id);
                write_all_stats(file, c->nstat_counts[DATA_CACHE], c->config[DATA_CACHE]->name, core->id);
            }
            c = c->next;
        }

        // all of the process stats on this core
        for(int j=0; j<core->process_n; j++) {
            process_t* p = &core->processes[j];
            if(p->valid) {
                write_all_stats(file, p->nstat_counts, p->tracefile->filename, core->id);
            }
        }        
        
    }   
	
    int ret = fclose(file);
    if(ret != 0) printf("Failed to close %s\n", sim->nstat_file);
    else printf("Data written to %s\n", sim->nstat_file);
}

void print_all_events() {
    printf("Collecting %i statistics in this simulation:\n", NUM_EVENTS);
    for(int i=0; i<NUM_EVENTS; i++) {
        const nstat_t* n = &all_stats[i];
        printf("%3i %s %16c %s\n", i, n->name, '#', n->desc);
    }
}

// finds the beginning of a random memory trace in the file
long int get_rand_trace_offset(char* file_path, size_t size) {

	FILE* fd = fopen(file_path, "r");
	long int offset = rand() % size;
	fseek(fd, offset, SEEK_SET); // move to random offset relative to the beginning (SEEK_SET)
	// look for newline char or end of file
	int tries = 0;
	while(1) {
		char c = fgetc(fd);
		offset++;
		if(c == '\n') break;
		else if(c == EOF) {
			tries++;
			if(tries == 3) {
				offset = 0;
				break;
			}
			offset = rand() % size;
			fseek(fd, offset, SEEK_SET); // move to random offset relative to the beginning (SEEK_SET)
		}	
	}
	fclose(fd);

	return offset;
}

void set_next_process(core_t* core) {

	for(int i=0; i<core->process_n; i++) {
		core->current_process = (core->current_process + 1) % core->process_n;
		process_t* p = &core->processes[core->current_process];	
		if(!p->valid || p->done) continue;
		else return;
	}
	
	core->current_process = -1;
	return;	
}

tracefile_t* add_thread_to_core(sim_t* sim, int core_id) {
	
	core_t* core = &sim->cores[core_id];
	
	// find a thread to launch
	tracefile_t* t = NULL;
	for(int j=0; j<sim->tracefiles_n; j++) {	
		t = &sim->tracefiles[sim->tracefile_ptr];	
		sim->tracefile_ptr = (sim->tracefile_ptr + 1) % sim->tracefiles_n;
		
		if(t->threads_launched == t->threads_n) {
			t = NULL;
			continue;
		} else {
			t->threads_launched++;
			break;
		}
	}
	if(!t) return NULL;

	// initalize the process on this core
	process_t* p = &core->processes[core->process_n];
	p->valid = 1;
	p->eid = sim->next_eid++;
	p->done = 0;
	p->core = core;
	p->tracefile = t;	
    p->partition_factor = 0;
    alloc_and_reset_counts(&p->nstat_counts);

    if(t->threads_launched == 1) p->trace_offset = 0;
	else p->trace_offset = get_rand_trace_offset(t->file_path, t->size); 
	p->seen_offset_n = 0;	
	p->offset_table = sim->offset_table;
	
	core->process_n++;
	
	p->trace = fopen(p->tracefile->file_path, "r");
	if(!p->trace) {
		perror("Failed to open trace file.\n");
		exit(1);
	}
	fseek(p->trace, p->trace_offset, SEEK_SET); // move to its assigned offset
 
	printf("Process %i (%s) scheduled onto core %i (offset %li)\n", p->eid, p->tracefile->filename, core->id, p->trace_offset);
	return t;
}

void parse_files(sim_t* sim, char* config, char* prog_file) {

	sim->config = (cache_config_t*) malloc(MAX_CACHE_CONFIG * sizeof(cache_config_t));
	memset(sim->config, 0 , MAX_CACHE_CONFIG * sizeof(cache_config_t));
	
	int cache = -1;
	
	/* parse run.config file */	
	FILE* r = fopen(config, "r");	
	if(!r) {
        printf("Failed to open config file %s\n", config);
		perror("Failed to open config file\n");
		exit(1);
	} 

	sim->cores_n = -1; // if cores_n was not specified, will use # of threads as # of cores
	char system = 0;	
	while(!feof(r)) {
		char* line = NULL;	
		size_t n = 0;
	
		if(getline(&line, &n, r) > 0) {
			if(strcmp(line, "\n") == 0) continue; // I didn't need this before...
			line[strcspn(line, "\r\n")] = 0;
			if(strcmp("CACHE", line) == 0) {
				system = 0;
				cache++;
				
                // default config values
                cache_config_t* config = &sim->config[cache];
                config->sgx_plru_rate = 1.0; // by default, if sgx_plru is chosen as eviction policy, it is used 100% of the time 
                config->insert_policy = INSERT_PMRU; 
                continue;	
			} else if(strcmp("SYSTEM", line) == 0) {
				system = 1;
				continue;
			}
			
			if(system) {
				char* param_type = strtok(line, " "); // get token type
				char* param = strtok(NULL, " "); // get value
				if(strcmp("cores_n:", param_type) == 0) sim->cores_n = atoi(param);
				else if(strcmp("stop_early:", param_type) == 0) sim->stop_early = atoi(param); 
                else if(strcmp("test:", param_type) == 0) sim->test = atoi(param); // for testing new features while things are running... 
                else if(strcmp("prefetch:", param_type) == 0) {
                    if(strcmp(param, "1") == 0) {
                        printf("Using next 1 line prefetch.\n");
                        sim->prefetch = nextLine;
                    }
                    else if(strcmp(param, "2") == 0) {
                        printf("Using next 2 lines prefetch.\n");
                        sim->prefetch = nextTwoLines;
                    } else {
                        printf("Unknown prefetch policy.\n");
                        sim->prefetch = prefetchNone;
                    }
                }
                else if(strcmp("ignore_ne:", param_type) == 0) {
                    sim->ignore_ne = atoi(param);
                    if(sim->ignore_ne) printf("Will ignore all non-enclave memory accesses.\n");
                }
                else if(strcmp("cachelet_assoc:", param_type) == 0) {
                    sim->cachelet_assoc = atoi(param);
                    printf("Cachelet associativity: %i\n", sim->cachelet_assoc);
                }
                else if(strcmp("dyn_threshold:", param_type) == 0) {
                    sim->dyn_threshold = atoi(param);
                    if(sim->dyn_threshold) printf("Using dynamic cachelets with threshold=%lu\n", sim->dyn_threshold);
                }
                else if(strcmp("dyn_rate:", param_type) == 0) {
                    sim->dyn_rate = atoi(param);
                    if(sim->dyn_rate) printf("Using dynamic resizing, rate=%lu memory references\n", sim->dyn_rate);
                }
                else if(strcmp("dyn_downsize_threshold_frac:", param_type) == 0) {
                    sim->dyn_downsize_threshold = sim->dyn_threshold / atoi(param);
                    printf("Downsize threshold=%lu\n", sim->dyn_downsize_threshold);
                }
                else if(strcmp("dyn_downsize_rate_mult:", param_type) == 0) {
                    sim->dyn_downsize_rate = sim->dyn_rate * atoi(param);
                    printf("Downsize rate=%lu memory references\n", sim->dyn_downsize_rate);
                }

			} else {
				cache_config_t* config = &sim->config[cache];
				config->id = cache;
				char* param_type = strtok(line, " "); // get token type
				char* param = strtok(NULL, " "); // get value
				if(strcmp("name:", param_type) == 0) config->name = param;
				else if(strcmp("level:", param_type) == 0) config->level = atoi(param);
				else if(strcmp("type:", param_type) == 0) {
					if(strcmp("insn", param) == 0) config->type = INSN_CACHE;
					else if(strcmp("data", param) == 0) config->type = DATA_CACHE;
					else config->type = UNIFIED_CACHE;
				}
				else if(strcmp("shared:", param_type) == 0) {
					if(strcmp("1", param) == 0) config->shared = 1;
					else config->shared = 0;
				} else if(strcmp("size_kb:", param_type) == 0) config->size_kb = atoi(param);
                else if(strcmp("size_b:", param_type) == 0) config->size_b = atoi(param);
				else if(strcmp("line_size:", param_type) == 0) config->line_size = atoi(param);
				else if(strcmp("ways_n:", param_type) == 0) config->ways_n = atoi(param);
				else if(strcmp("enclave_ways_n:", param_type) == 0) config->max_enclave_ways_n = atoi(param);
				else if(strcmp("static_partition:", param_type) == 0) config->static_partition = atoi(param);
                else if(strcmp("evict:", param_type) == 0) {
					if(strcmp("plru", param) == 0) config->evict_policy = EVICT_PLRU;
					else if(strcmp("rand", param) == 0) config->evict_policy = EVICT_RAND;
                    else if(strcmp("sgx_plru", param) == 0) config->evict_policy = EVICT_SGX_PLRU; // sgx_plru
				}
                else if(strcmp("insert:", param_type) == 0) {
                    if(strcmp("plru", param) == 0) config->evict_policy = INSERT_PLRU;
                    else if(strcmp("pmru", param) == 0) config->evict_policy = INSERT_PMRU;
                }
                else if(strcmp("sgx_plru_rate:", param_type) == 0) config->sgx_plru_rate = atof(param);
				else if(strcmp("inclusion:", param_type) == 0) {
					if(strcmp("non-inclusive", param) == 0) config->inclu_policy = NON_INCLUSIVE;
					else if(strcmp("inclusive", param) == 0) {
                        config->inclu_policy = INCLUSIVE;
				        sim->uses_inclusive = 1;
                    }
                }
				else if(strcmp("partition:", param_type) == 0) {
					if(strcmp("1", param) == 0) config->partition= 1;
					else config->partition = 0;
				}
				else if(strcmp("set_partition:", param_type) == 0) {
					if(strcmp("1", param) == 0) config->set_partition = 1;
					else config->set_partition = 0;
				}
				else if(strcmp("max_partition:", param_type) == 0) {
                    config->max_partition = atoi(param); // max partitions PER enclave way
                }
                else if(strcmp("use_cachelet:", param_type) == 0) {
                    config->use_cachelet = atoi(param); // partition size is fixed throughout (no growing/shrinking)
                }
                else if(strcmp("static_cachelets:", param_type) == 0) {
                    config->static_cachelets = atoi(param);
                    printf("Pre-allocating %i cachelets.\n", config->static_cachelets);
                }
               	else printf("Unknown parameter type %s\n", param);
			} // !system ; end
		}
	}	
	fclose(r);

	sim->config_n = cache + 1;

	/* parse .prog file */
	r = fopen(prog_file, "r");	
	if(!r) {
		perror("Failed to open prog file.\n");
		exit(1);
        //return;
	} 
    
    char* traces_dir = "traces/";
	while(!feof(r)) {
		char* line = NULL;	
		size_t n = 0;
	
		if(getline(&line, &n, r) > 0) {
			if(strcmp(line, "\n") == 0) continue; // I didn't need this before...
			line[strcspn(line, "\r\n")] = 0;
			
			char* trace = strtok(line, " ");
			char* threads_n = strtok(NULL, " ");
            char* always = strtok(NULL, " "); // either always enclave or non-enclave
			if(sim->tracefiles_n == MAX_TRACEFILES) {
				printf("Reached maximum of %i traces.\n", MAX_TRACEFILES);
				break;
			}
			tracefile_t* t = &sim->tracefiles[sim->tracefiles_n];
			t->always = -1;
            if(always) {
                if(strcmp(always, "e") == 0) {
                    printf("%s will run entirely enclave.\n", trace);
                    t->always = ENCLAVE;
                }
                else if(strcmp(always, "ne") == 0) {
                    printf("%s will run entirely non-enclave.\n", trace);
                    t->always = NON_ENCLAVE;
                }
                else t->always = -1;
            } 
            strcpy(t->filename, trace); 
			
            t->threads_n = (threads_n) ? atoi(threads_n) : 1;
			t->threads_launched = 0;
	
            int path_len = strlen(traces_dir) + strlen(t->filename) + 1;	
            t->file_path = malloc(path_len);
            memset(t->file_path, 0, path_len);
            strcpy(t->file_path, traces_dir);
            strcat(t->file_path, t->filename);
			FILE* fd = fopen(t->file_path, "r");
			if(!fd) {
				printf("Failed to open file %s\n", t->file_path);
				exit(1);
			}

			fseek(fd, 0, SEEK_END);
			t->size = ftell(fd);
			fclose(fd);

			sim->tracefiles_n++;
			sim->prog_n += t->threads_n;
		} // getline	
	} // while not eof ; end
	fclose(r);

	if(sim->cores_n == -1) sim->cores_n = sim->prog_n;
}

void init_sim(sim_t* sim, char* argv[]) {	

    char* config = argv[1];
    char* prog_file = argv[2];
	memset(sim, 0, sizeof(sim_t));	
	alloc_and_reset_counts(&sim->nstat_counts);

	parse_files(sim, config, prog_file);	
	init_cache(sim);	
    sim->queue = malloc(sizeof(access_t) * sim->cores_n);
	
	// 2 traces , .results.csv contains all the compiled statistics, and .graph.csv for graph data
	char* b_config = basename(config);
	char* b_prog_file = basename(prog_file);

	remove_substring(b_config, ".config");
	remove_substring(b_prog_file, ".prog");
	char* trace_id = malloc(strlen(b_config) + strlen(b_prog_file) + 2); // <config>.<prog_file> + null terminator
	strcpy(trace_id, b_config);
	strcat(trace_id, ".");
	strcat(trace_id, b_prog_file);

    sim->nstat_file = malloc(strlen(trace_id) + strlen(".nstat.txt") + 1); // +1 null terminator
    strcpy(sim->nstat_file, trace_id);
    strcat(sim->nstat_file, ".nstat.txt");

    sim->config_file = malloc(strlen(trace_id) + strlen(".config.csv") + 1); // +1 null terminator
    strcpy(sim->config_file, trace_id);
    strcat(sim->config_file, ".config.csv");

    if(sim->dyn_threshold > 0) {
        char* f = malloc(strlen(trace_id) + strlen(".misses.csv") + 1); // +1 null terminator
        strcpy(f, trace_id);
        strcat(f, ".misses.csv");
        sim->miss_csv = fopen(f, "w");
        if(!sim->miss_csv) {
            printf("Failed to open %s\n", f);
        } else printf("Will write miss counts in %s\n", f);
        fprintf(sim->miss_csv, "trace,eid,num_cachelets,num_e_insn,miss_counter(t=%lu r=%lu)\n", sim->dyn_threshold, sim->dyn_rate);
        free(f);
    }

	// initialize and assign all processes to each core	
	for(int i=0; i<sim->cores_n; i++) {
		core_t* core = &sim->cores[i];
		for(int j=0; j<sim->progs_per_core; j++) {
			add_thread_to_core(sim, core->id); // initialize process
		}
		set_next_process(core); // set an active process
	}

    // save eid -> core_id mapping
    sim->eid_to_core_id = (int*) malloc(sim->prog_n * sizeof(int));
    memset(sim->eid_to_core_id, -1, sim->prog_n * sizeof(int));
    for(int i=0; i<sim->cores_n; i++) {
        core_t* core = &sim->cores[i];
        for(int j=0; j<core->process_n; j++) {
            process_t* p = &core->processes[j];
            sim->eid_to_core_id[p->eid] = core->id;
        }
    }

    if(sim->test) printf("Test mode\n");
    //printf("%3s %8s\n", "eid", "core_id");
    //for(int i=0; i<sim->prog_n; i++) {
    //    printf("%3i %8i\n", i, sim->eid_to_core_id[i]);
    //}
	
	free(trace_id);
}

