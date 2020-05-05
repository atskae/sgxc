#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <math.h>

#define __STDC_FORMAT_MACROS // for printing uint64_t
#include <inttypes.h>

#include "cache.h"
#include "utils.h"

void edit_line(int action, sim_t* sim, process_t* p, cache_t* c, cache_config_t* config, cacheline_t* set, int set_idx, int way_idx);
int get_enclave_set(sim_t* sim, process_t* p, cache_t* c, int cache_type, uint64_t addr, uint64_t* tag);
int search_set(sim_t* sim, process_t* p, cache_config_t* config, cacheline_t* set, int set_idx, int* free, int eid, uint64_t tag);
int find_free_offset(sim_t* sim, process_t* p, cache_config_t* config);
int get_dyn_enclave_set_and_tag(process_t* p, cache_t* c, int cache_type, uint64_t addr, uint64_t* tag);

cache_t* alloc_cache(cache_t* cache, cache_config_t* config) {
	
	cache_t* c;
	if(!cache) {
		c = malloc(sizeof(cache_t));
		memset(c, 0, sizeof(cache_t));
        for(int i=0; i<3; i++) {
			c->config[i] = NULL;
			c->cache[i] = NULL;
			c->plru[i] = NULL;
		}
		c->unified = (config->type == UNIFIED_CACHE);
	}
	else c = cache;
	
	/* allocate cache memory */
	c->cache[config->type] = malloc(config->sets_n * sizeof(cacheline_t*));
	c->plru[config->type] = malloc(config->sets_n * sizeof(char*));

	for(int s=0; s<config->sets_n; s++) {
		c->cache[config->type][s] = (cacheline_t*) malloc(config->ways_n * sizeof(cacheline_t));
		c->plru[config->type][s] = malloc( (config->ways_n-1) * sizeof(char));
		memset(c->plru[config->type][s], 0, (config->ways_n-1) * sizeof(char));
		for(int w=0; w<config->ways_n; w++) {
			c->cache[config->type][s][w].valid = 0;
			c->cache[config->type][s][w].eid = -1;
		    c->cache[config->type][s][w].dirty = 0;
            c->cache[config->type][s][w].enclave_mode = 0;
        }
	}
	c->config[config->type] = config;
	c->next = NULL;

	// reset statistics
	alloc_and_reset_counts(&c->nstat_counts[config->type]);

	return c;
}

void init_cache(sim_t* sim) {

	int max_partition = 0;
	// precompute additional config values for each cache	
	for(int i=0; i<sim->config_n; i++) {		
		cache_config_t* c = &sim->config[i];
		if(c->max_partition > max_partition) max_partition = c->max_partition;
       
		// enclave way	
		c->enclave_ways_n = 0;
        if(c->partition && c->static_partition) c->enclave_ways_n = c->max_enclave_ways_n;
        if(c->set_partition && c->max_enclave_ways_n > 0) { 
            int eway_size = c->max_enclave_ways_n * sizeof(enclave_way_info_t);
			c->eway_info = (enclave_way_info_t*) malloc(eway_size);
			for(int w=0; w<c->max_enclave_ways_n; w++) {
				c->eway_info[w].valid = 0;
				c->eway_info[w].alloc_n = 0;
				c->eway_info[w].set_bits_n = 0;
				c->eway_info[w].sat = malloc(c->max_partition * sizeof(sat_entry_t));
				c->eway_info[w].sat_plru = malloc(c->max_partition - 1);
				memset(c->eway_info[w].sat, 0, sizeof(sat_entry_t) * c->max_partition);
				memset(c->eway_info[w].sat_plru, 0, (c->max_partition-1) );
			}
		}
	
		c->addr_bits_n = ADDR_BITS;
		c->offset_bits_n = log2(c->line_size);
		c->sets_n = (c->size_kb * 1024)/(c->ways_n * c->line_size);
        if(c->sets_n == 0 && c->size_b > 0) { // measured cache size by bytes
            printf("Measured cache size in bytes.\n");
            c->sets_n = (c->size_b)/(c->ways_n * c->line_size);
        }

        c->set_bits_n = log2(c->sets_n);
        c->tag_bits_n = (c->addr_bits_n) - (c->set_bits_n + c->offset_bits_n);
        // generate a set bit mask
        c->set_mask = (uint64_t) pow(2, c->set_bits_n) - 1;
        c->set_mask = c->set_mask << (c->offset_bits_n);
        // generate a tag bit mask
        c->tag_mask = (uint64_t) pow(2, c->tag_bits_n) - 1;
        c->tag_mask = c->tag_mask << (c->offset_bits_n + c->set_bits_n);

        // for replacement with cachelets
        if(c->use_cachelet) {
            c->way_bitmaps = (uint64_t*) malloc(sizeof(uint64_t) * c->max_partition);
            memset(c->way_bitmaps, 0, sizeof(uint64_t) * c->max_partition); // all ways are initially for non-enclaves
            
        }
		
	}	
	
	sim->offset_table = (int**) malloc(sizeof(int*) * max_partition);
	for(int i=0; i<max_partition; i++) {
		sim->offset_table[i] = (int*) malloc(sizeof(int) * sim->config_n);
		memset(sim->offset_table[i], 0, sizeof(int) * sim->config_n);
	}
	
	// Compute offsets of global offset table 
	for(int c=0; c<sim->config_n; c++) {
		int id = sim->config[c].id;
		int up = 0;
		int num_sets = sim->config[c].sets_n;
		sim->offset_table[0][id] = 0;
		for(int i=0; i<sim->config[c].max_partition; i++) {
			if( i != 0 && ((i & (i-1)) == 0) ) {
				if(up == 0) up = 1; // is a power of 2
				else up *= 2;
				num_sets /= 2;
			}
			if(i != 0) sim->offset_table[i][id] = sim->offset_table[i-up][id] + num_sets; // config->sat[i-up].offset + num_sets;
		}
	}
    //if(max_partition > 0) print_offset_table(sim, max_partition);

    // statically allocate cachelets, if any ; need to do this after offset table is initialized
    for(int i=0; i<sim->config_n; i++) {
        cache_config_t* c = &sim->config[i];
        if(c->use_cachelet) {
            if(c->static_cachelets && (c->static_cachelets <= c->max_enclave_ways_n/sim->cachelet_assoc * c->max_partition)) {
                // set these ways as reserved
                process_t p; // dummy process
                p.eid = -1; // will not match any process
                p.offset_table = sim->offset_table;
                for(int i=0; i<c->static_cachelets; i++) {
                    int ret = find_free_offset(sim, &p, c);
                    assert(ret != -1); // -1 means cache is full
                }
                printf("Statically allocated %i cachelets\n", c->static_cachelets);
            }
        }
    }

	cache_t* ptr = NULL;
	// create shared caches	
	for(int i=0; i<sim->config_n; i++) {
		cache_config_t* config = &sim->config[i];
		if(config->shared) {	
			if(!ptr) { // this is the first cache to be allocated
				sim->cache = alloc_cache(0, config);
				ptr = sim->cache;
				continue;
			}
			
			// search previously created caches to see if levels match
			cache_t* match_ptr = sim->cache;
			char match_found = 0;
			while(match_ptr) {
				if(match_ptr->config[0]->level == config->level) {
					alloc_cache(match_ptr, config); // allocates cache space for an existing level of cache
					match_found = 1;
					break;
				}
				match_ptr = match_ptr->next;
			}
			if(!match_found) {
				ptr->next = alloc_cache(0, config);
				ptr = ptr->next;	
			}
		}
	}

	// private caches
	sim->cores = malloc(sizeof(core_t) * sim->cores_n);
	sim->progs_per_core = ceil(sim->prog_n/sim->cores_n);
	for(int i=0; i<sim->cores_n; i++) {
		core_t* core = &sim->cores[i];
		core->id = i;
		core->process_n = 0;
		core->clock = 0;
		core->offset_table = sim->offset_table;
		//alloc_and_reset_counts(&core->nstat_counts);

        core->processes = malloc(sim->progs_per_core * sizeof(process_t));
		memset(core->processes, 0, sim->progs_per_core * sizeof(process_t));
		core->current_process = -1;
	
		cache_t* ptr = NULL;
		for(int j=0; j<sim->config_n; j++) {
			cache_config_t* config = &sim->config[j];
			if(config->shared) continue;
		
			if(!ptr) { // this is the first cache to be allocated
				core->cache = alloc_cache(0, config);
				ptr = core->cache;
				continue;
			}
			
			// search previously created caches to see if levels match
			cache_t* match_ptr = core->cache;
			char match_found = 0;
			while(match_ptr) {
				if(!match_ptr->unified) { 
                    if(match_ptr->config[0]->level == config->level) {
				    	alloc_cache(match_ptr, config); // allocates cache space for an existing level of cache
				    	match_found = 1;
				    	break;
				    }
                }
				match_ptr = match_ptr->next;
			}
			if(!match_found) {
				ptr->next = alloc_cache(0, config);
				ptr = ptr->next;	
			}
		} // for each config ; end
	}

	// link the private caches to the shared cache
	for(int i=0; i<sim->cores_n; i++) {
		core_t* core = &sim->cores[i];
		cache_t* ptr = core->cache;
		cache_t* prev = NULL;
		while(ptr) { // get to the last private cache
			prev = ptr;
			ptr = ptr->next;
		}
		if(prev) prev->next = sim->cache;
	}

	return;
}

int get_cache_type(cache_t* c, int op) {
	if(c->unified) return UNIFIED_CACHE;
	else {
		if(op == INSN_OP) return INSN_CACHE; 
		if(op == LOAD_OP || op == STORE_OP) return DATA_CACHE; 
	}
	return -1;
}

// reconstructs the address given the tag and set index
uint64_t get_addr(uint64_t tag, int idx, int offset_bits_n) {
    
    uint64_t set_idx = idx;
    set_idx = set_idx << offset_bits_n; 
    uint64_t addr = tag | set_idx;

    return addr;
}

void clear_way_bitmap(uint64_t* bitmap, int way) {
    *bitmap = (*bitmap) & (~(1 << way));
}

uint64_t read_way_bitmap(uint64_t* bitmap, int way) {
    return (*bitmap) & (1 << way);
}

void set_way_bitmap(uint64_t* bitmap, int way) {
    *bitmap = (*bitmap) | (1 << way);
}

// read the upper bits of the set index
int get_way_bitmap_idx(cache_config_t* config, int set_idx) {
    if(config->max_partition == 0) return -1;
    uint64_t mask = (uint64_t) config->max_partition - 1;
    int pbits_n = log2(config->max_partition);
    mask = mask << (config->set_bits_n - pbits_n);
    return (set_idx & mask) >> (config->set_bits_n - pbits_n);
}

uint64_t* get_bitmap(process_t* p, cache_config_t* config) {
    if(!config->use_cachelet) return 0;
    int offset = p->offset_table[p->sat_idx][config->id];
    return &config->way_bitmaps[get_way_bitmap_idx(config, offset)];
}

// slot = either a way or a set (enclave way)
void update_plru(char* plru, int slots_n, int slot_accessed) { 
    
    int t = log2(slots_n) - 1; // number of traversals
    
    int p_idx  = 0;
    for(int i=t; i>=0; i--) {
        unsigned int dir = slot_accessed & (1 << i);
        if(dir) { // go right
            plru[p_idx] = 0; // set bit to opposite of taken path
            p_idx = 2*p_idx + 2; // go to right child 
        } else {
            plru[p_idx] = 1; // set bit to opposite of taken path
            p_idx = 2*p_idx + 1; // go to left child  
        }
    }
}

void set_line(process_t* p, cacheline_t* cl, uint64_t tag) {

    assert(cl->valid == 0);

    access_t* a = p->access;
    cl->valid = 1;
    cl->tag = tag;
    assert(a->eid == p->eid);
    cl->eid = p->eid;
    cl->dirty = (a->op == STORE_OP);
    cl->enclave_mode = a->enclave_mode;
}

uint64_t get_tag(process_t* p, cache_config_t* config, uint64_t addr) {
     
    if(p->access->enclave_mode && config->set_partition) {
        enclave_way_info_t* eway = &config->eway_info[p->eway_idx]; // obtain the assigned enclave way
	    
        int tag_bits_n = (config->addr_bits_n - (eway->set_bits_n + config->offset_bits_n) );
	    uint64_t tag_mask = (uint64_t) pow(2, tag_bits_n) - 1;
	    tag_mask = tag_mask << (config->offset_bits_n + eway->set_bits_n);
	    return addr & tag_mask;
    } else return addr & config->tag_mask;
}


int get_set_and_tag(sim_t* sim, process_t* p, cache_t* c, int cache_type, cache_config_t* config, uint64_t addr, int enclave_mode, uint64_t* tag) {
    
    int set_idx;
    if(config->use_cachelet && sim->dyn_threshold > 0 && enclave_mode) set_idx = get_dyn_enclave_set_and_tag(p, c, cache_type, addr, tag);
    else if(enclave_mode && config->set_partition) set_idx = get_enclave_set(sim, p, c, cache_type, addr, tag);
    else set_idx = (addr & config->set_mask) >> config->offset_bits_n;
    
    if(sim->dyn_threshold == 0) *tag = get_tag(p, config, addr);

    return set_idx;
}

// start_id ; id of the cache where this line was initially chosen to be evicted
char search_and_edit(int action, sim_t* sim, process_t* p, cache_t* c, char* evicted, int start_id, char start_is_inclu, int cache_type, int eid, uint64_t addr, int enclave_mode) { 
    
    cache_config_t* config = c->config[cache_type];
    if(config->id == start_id) return 0; // want to evict/set where we started from afterwards
    if(action == SET_LINE) {
        if(start_is_inclu && config->level != 1) return 0; // we only need to place it in the first level cache
        else if (!start_is_inclu && config->inclu_policy != INCLUSIVE) return 0; // if we start from private level cache, we only need to set line in inclusive cache
    } else if(action == EVICT_LINE) {
        if(!start_is_inclu) return 1; // we don't need to evict extra lines unless we are evicting from the L3
    }

    // must recalculate tag and set for each level of cache
    uint64_t tag;
    int set_idx = get_set_and_tag(sim, p, c, cache_type, config, addr, enclave_mode, &tag);
    
    cacheline_t* set = c->cache[cache_type][set_idx];
    int free = -1;
    int w = -1; // indicates hit or miss
    if(action == EVICT_LINE) {
        if(!start_is_inclu && config->id != start_id && config->inclu_policy == NON_INCLUSIVE) return 1; // no need to evict elsewhere ; there is a copy in this cache
        
        w = search_set(sim, p, config, set, set_idx, &free, eid, tag);
        if(w != -1) { // cache hit
            cacheline_t* cl = &set[w];
            if(cl->valid && cl->dirty) update_stat(sim, p->nstat_counts, STAT_DIRTY_LINES, cl->enclave_mode);

            if(sim->uses_inclusive) {
                *evicted = 1;
                // find the process whose line was evicted due to inclusion
                int core_id = sim->eid_to_core_id[cl->eid];
                core_t* core = &sim->cores[core_id]; // the core where this cache line originated
                process_t* victim = NULL;
                for(int i=0; i<core->process_n; i++) {
                    if(core->processes[i].eid == cl->eid) {
                        victim = &core->processes[i];
                        break;
                    }
                }
                assert(victim != NULL);
                update_stat(sim, victim->nstat_counts, STAT_IS_INCLUSION_VICTIM, cl->enclave_mode); // vicitm's line was removed
                update_stat(sim, p->nstat_counts, STAT_EVICT_INCLUSION_VICTIM, cl->enclave_mode); // this process evicted the line

                if(victim->eid != p->eid) {
                    update_stat(sim, victim->nstat_counts, STAT_IS_INCLUSION_VICTIM_OTHER, cl->enclave_mode); // vicitm's line was removed by another process that is not the victim
                    update_stat(sim, p->nstat_counts, STAT_EVICT_INCLUSION_VICTIM_OTHER, cl->enclave_mode); // this process evicted the line that is not their line
                }
            }

            cl->valid = 0;
        }
        if(config->inclu_policy == INCLUSIVE) return 1;

    } else if(action == SET_LINE) {
        w = search_set(sim, p, config, set, set_idx, &free, eid, tag);
        if(w != -1) return 1; // cache hit ; this is possible, example, line hits in L2 (and is already in L3) and missed in L1 ; tried to place in L3 and its already there (hits)
        if(free == -1) { // there are no free cache ways ; must evict 
            int evict_idx;
            if( (enclave_mode && config->set_partition && !config->use_cachelet) || 
                (enclave_mode && config->use_cachelet && sim->cachelet_assoc <= 1) ) evict_idx = p->eway_idx; // direct-mapped cache
            else evict_idx = pick_victim_way(sim, p, c, config, cache_type, set_idx, enclave_mode); // performs plru
            edit_line(EVICT_LINE, sim, p, c, config, set, set_idx, evict_idx);	 
            free = evict_idx;
        }
        assert(free != -1); // at this point there must be a free spot
        cacheline_t* cl = &set[free];
        set_line(p, cl, tag);
        if(config->evict_policy == EVICT_PLRU || config->evict_policy == EVICT_SGX_PLRU) update_plru(c->plru[cache_type][set_idx], config->ways_n, free);

        return 1;
    }
   
    return 0;
}

// either invalidates a line or sets a line, based on action
void edit_line(int action, sim_t* sim, process_t* p, cache_t* c, cache_config_t* config, cacheline_t* set, int set_idx, int way_idx) {
  
    // this cache line is either the line that is to be set or evicted 
    cacheline_t* cl = &set[way_idx];
    if(!cl->valid && action == EVICT_LINE) return; 
  
    if(sim->uses_inclusive) { // inclusive cache is used ; first evict/set line from all caches to maintain inclusive-ness
        
        // see which core to check
        cache_t* c = NULL;  
        uint64_t addr;        
        int eid;
        int enclave_mode; 
        access_t* a = p->access; // if setting a line only
        
        if(action == EVICT_LINE) { // evict the victim cache line from all caches in the victim's core
            int core_id = sim->eid_to_core_id[cl->eid];
            core_t* core = &sim->cores[core_id]; // the core where this cache line originated
            c = core->cache;
            addr = get_addr(cl->tag, set_idx, config->offset_bits_n); // reconstruct the victim address
            eid = cl->eid;
            enclave_mode = cl->enclave_mode; 
        } else if(action == SET_LINE) { // set the process's access in all caches
            c = p->core->cache; // start from the beginning of this process's caches (L1) and go toward upper level caches
            addr = a->addr;
            eid = p->eid;
            enclave_mode = a->enclave_mode; 
        }
        assert(c != NULL);
 
        // if evicting a line from L3, we definitely need to evict from private level caches;
        // if evicting from private level, there is a chance that another private level cache has a copy, then we don't have to evict additional lines
        // if no copy in another private level cache, then must be evicted from L3
        char start_is_inclu = config->inclu_policy == INCLUSIVE; 
        char evicted = 0;

        // evict/set this line from other caches first 
        char done = 0;
        while(c && !done) {
                    
            if(c->unified) done = search_and_edit(action, sim, p, c, &evicted, config->id, start_is_inclu, UNIFIED_CACHE, eid, addr, enclave_mode);
            else { // must check both insn and data cache
                done = search_and_edit(action, sim, p, c, &evicted, config->id, start_is_inclu, INSN_CACHE, eid, addr, enclave_mode); 
                if(done) break;
                done = search_and_edit(action, sim, p, c, &evicted, config->id, start_is_inclu, DATA_CACHE, eid, addr, enclave_mode); 
            }
                
            c = c->next;
        } 
    }

    // for inclusive and non-inclusive ; evict this line from the current cache
    if(action == EVICT_LINE) {
        assert(cl->valid);
        if(cl->dirty) update_stat(sim, p->nstat_counts, STAT_DIRTY_LINES, cl->enclave_mode);
        if(cl->eid != p->eid) update_stat(sim, p->nstat_counts, STAT_EVICT_OTHER, cl->enclave_mode);
        cl->valid = 0;
    }
    else if(action == SET_LINE) {
        uint64_t tag = get_tag(p, config, p->access->addr);
        set_line(p, cl, tag);
        if(config->evict_policy == EVICT_PLRU || config->evict_policy == EVICT_SGX_PLRU) update_plru(c->plru[get_cache_type(c, p->access->op)][set_idx], config->ways_n, way_idx);
    }

}

void invalidate_sets(sim_t* sim, process_t* p, cache_t* c, int cache_type, char process_finished) { // if the process is completed, then the eid must match for the set to be cleared

	cache_config_t* config = c->config[cache_type];
	
	// invalidate cache lines in this partition
	enclave_way_info_t* eway = &config->eway_info[p->eway_idx];
	sat_entry_t* sat = eway->sat;	
	if(process_finished && sat[p->sat_idx].eid != p->eid) return; // can only clear its own partition 

	int offset = p->offset_table[p->sat_idx][config->id];
	int max = offset + pow(2, eway->set_bits_n);

    int incr_ways = 1;
    if(config->use_cachelet && sim->cachelet_assoc >= 1) incr_ways = sim->cachelet_assoc;
	
	for(int set_idx=offset; set_idx<max; set_idx++) { // for each set allocated to this enclave	
        cacheline_t* set = c->cache[cache_type][set_idx];
        for(int w=0; w<incr_ways; w++) { // if using cachelet, remove line across associativity
            edit_line(EVICT_LINE, sim, p, c, config, set, set_idx, p->eway_idx+w); // removes line in this cache ; if inclusive then it will remove from other cache levels 
        }
	}

	if(process_finished) {
		sat[p->sat_idx].valid = 0; // free this entry 
		sat[p->sat_idx].eid = -1;
		eway->alloc_n--;
        config->way_bitmaps[p->sat_idx] = 0;
	}
}

// fix this so that when a program completes, it can call this function
void free_partition(sim_t* sim, process_t* p, char process_finished) {
	cache_t* c = p->core->cache;
	while(c) {
		if(c->unified) { // unified cache
			if(c->config[UNIFIED_CACHE]->set_partition) invalidate_sets(sim, p, c, UNIFIED_CACHE, process_finished);			
		} else { // must remove from both insn and data caches
			if(c->config[INSN_CACHE]->set_partition) invalidate_sets(sim, p, c, INSN_CACHE, process_finished);			
			if(c->config[DATA_CACHE]->set_partition) invalidate_sets(sim, p, c, DATA_CACHE, process_finished);		
		}
		c = c->next; // move to next level of cache
	}	
}

// finds a free entry in the Set Allocation Table (sat)
int find_free_offset(sim_t* sim, process_t* p, cache_config_t* config) {

	//cache_config_t* config = c->config[cache_type];
    int way_incr = 1;
    if(config->use_cachelet && sim->cachelet_assoc >= 1) way_incr = sim->cachelet_assoc;
	
	for(int w=0; w<config->max_enclave_ways_n; w = w + way_incr) {
		enclave_way_info_t* eway = &config->eway_info[w];
		if(eway->valid) {
			if(eway->alloc_n == config->max_partition) continue; // this way is full..

			for(int i=0; i<config->max_partition; i++) { // search down this enclave way
				if(eway->sat[i].valid == 0) {
					eway->sat[i].valid = 1;
					eway->sat[i].eid = p->eid;
					p->eway_idx = w;
                    p->sat_idx = i;
                    
                    if(!config->use_cachelet) { // dynamically change number of addressable sets if NOT using cachelet
					    if( (eway->alloc_n != 0) && (eway->alloc_n & (eway->alloc_n - 1)) == 0) { // is a power of 2
					    	eway->set_bits_n--;
					    }
                    }

                    // update the bit map
                    if(config->use_cachelet) {
                        uint64_t* bitmap = get_bitmap(p, config);
                        for(int k=0; k<sim->cachelet_assoc; k++) {
                            set_way_bitmap(bitmap, w+k);
                        }
                    }

					eway->alloc_n++;	
					//invalidate_sets(sim, p, c, cache_type, 0);
					return i;
				}
			}
		} else { // must allocate another enclave way
			config->enclave_ways_n++;
			eway->valid = 1;
            // if cachelet is fixed
            if(config->use_cachelet) {
                eway->set_bits_n = log2(config->sets_n/config->max_partition); // fixed, will not change
                // update the bit map
                for(int k=0; k<sim->cachelet_assoc; k++) {
                    set_way_bitmap(&config->way_bitmaps[0], w+k);
                }
            } else {
			    eway->set_bits_n = config->set_bits_n;
			}
            eway->alloc_n = 1;
		
			// allocate a set	
			eway->sat[0].valid = 1;
			eway->sat[0].eid = p->eid;
			p->eway_idx = w;
			p->sat_idx = 0;
            //invalidate_sets(sim, p, c, cache_type, 0);

			return 0;
		}	
	}
	return -1; // completely full....
}

void evict_sat_plru(sim_t* sim, process_t* p, cache_t* c, int cache_type) {

	cache_config_t* config = c->config[cache_type];

	// for now, pick a random victim way
	p->eway_idx = rand() % config->enclave_ways_n;
	enclave_way_info_t* eway = &config->eway_info[p->eway_idx];
	sat_entry_t* sat = eway->sat;
	char* plru = eway->sat_plru;	

  	int t = log2(config->max_partition); // number of times we traverse the plru binary search tree
  	int p_idx = 0; // index into plru bst   
  	int evict_idx = 0;

  	for(int i=t-1; i>=0; i--) {
  	     if(plru[p_idx] == 0) {
  	        plru[p_idx] = 1; // set direction      
  	     	p_idx = 2*p_idx + 1; // left child 
  	     }
  	     else { // go right
  	        plru[p_idx] = 0; // set direction
  	        p_idx = 2*p_idx + 2; // right child     
  	        evict_idx |= (1 << i);  
  	     }
  	}

	p->sat_idx = evict_idx;	
	free_partition(sim, p, 0); // process did not complete yet, so =0	
 	sat[evict_idx].eid = p->eid; 

}

// calculates the offset into cache way of this enclave and returns set_idx and tag
int get_enclave_set(sim_t* sim, process_t* p, cache_t* c, int cache_type, uint64_t addr, uint64_t* tag) {

	cache_config_t* config = c->config[cache_type];
	enclave_way_info_t* eway = &config->eway_info[p->eway_idx]; // obtain the assigned enclave way
	sat_entry_t* sat = &eway->sat[p->sat_idx]; // obtain offset and set bits

	if(sat->valid && sat->eid == p->eid) update_plru(eway->sat_plru, config->max_partition, p->sat_idx); 
	else {
		// Process was replaced or this is first assignment
		p->sat_idx = find_free_offset(sim, p, config); // find a free sat index and sets the appropriate bits	
		if(p->sat_idx == -1) evict_sat_plru(sim, p, c, cache_type); // evicts an sat entry, obtain an eway_idx, sets it, invalidates cache lines, assigns a new sat_idx
    }	

    *tag = get_tag(p, config, addr);
	
	int offset = p->offset_table[p->sat_idx][config->id];
	uint64_t set_mask = (uint64_t) pow(2, eway->set_bits_n) - 1;
	set_mask = set_mask << (config->offset_bits_n);
	int set_idx = (addr & set_mask) >> config->offset_bits_n; 
    
    // recalculate calculate partition factor
    p->partition_factor = (int) pow(2, config->set_bits_n - eway->set_bits_n);
	
    set_idx = offset + set_idx;
	return set_idx;
}

int get_dyn_enclave_set_and_tag(process_t* p, cache_t* c, int cache_type, uint64_t addr, uint64_t* tag) {
   
    if(p->num_cachelets == 0) p->num_cachelets++; 
    
    cache_config_t* config = c->config[cache_type];
    
    // set
    int set_bits_n = log2((config->sets_n/config->max_partition) * p->num_cachelets);
    uint64_t set_mask = (uint64_t) pow(2, set_bits_n) - 1;
    set_mask = set_mask << (config->offset_bits_n);
    int set_idx = (addr & set_mask) >> config->offset_bits_n;
     
	// compute the tag    
    int tag_bits_n = (config->addr_bits_n - (set_bits_n + config->offset_bits_n) );
	uint64_t tag_mask = (uint64_t) pow(2, tag_bits_n) - 1;
	tag_mask = tag_mask << (config->offset_bits_n + set_bits_n);
	*tag = addr & tag_mask;

    return set_idx;
}

int evict_plru(cache_t* c, cache_config_t* config, int cache_type, int set_idx, int enclave_mode) {

    /* calculates the range of cache ways depending on if this access is in enclave_mode or not */
	int low_w = 0; // inclusive
    int high_w = config->ways_n; // exclusive
	
	if(config->partition) {
		if(enclave_mode) high_w = config->enclave_ways_n; // exclusive
       	else low_w =  config->enclave_ways_n;
	}
 	
	// no ways to search ; why did I need this again?
	if(low_w == high_w) return -1;

	char* plru = c->plru[cache_type][set_idx];

	int t = log2(config->ways_n); // number of times we traverse the plru binary search tree
    int p_idx = 0; // index into plru bst   
    int evict_idx = 0;

    for(int i=t-1; i>=0; i--) {
    	if(plru[p_idx] == 0) {
    		if(!enclave_mode && ((i != 0) && ( evict_idx | (1 << (i-1)) ) < low_w)) { // checks if there is NOT a chance to find a non-enclave way in the sub-tree
    			p_idx = 2*p_idx + 2; // must go to right child
    			evict_idx |= (1 << i);
    		} else if(!enclave_mode && (i == 0) && evict_idx < low_w) {
    			p_idx = 2*p_idx + 2; // must go to right child
    			evict_idx |= (1 << i);
    		} else {
    			plru[p_idx] = 1; // flip direction      
    			p_idx = 2*p_idx + 1; // left child
    		}
    	}
    	else { // go right
    		if(enclave_mode && (evict_idx | (1 << i)) >= high_w) {
    			// we have to go left without flipping bits
    			p_idx = 2*p_idx + 1;
    		} else {
    			plru[p_idx] = 0; // flip direction
    			p_idx = 2*p_idx + 2; // right child     
    			evict_idx |= (1 << i);
    		}
    	}
    } 
	
    if(enclave_mode && config->partition) assert(evict_idx < config->enclave_ways_n);
	else if(!enclave_mode && config->partition && config->enclave_ways_n > 0) assert(evict_idx >= config->enclave_ways_n);

    return evict_idx;

}

int evict_plru_cachelet(process_t* p, int cachelet_assoc, cache_t* c, cache_config_t* config, int cache_type, int set_idx, int enclave_mode) {
 	
	char* plru = c->plru[cache_type][set_idx];

	int t = log2(config->ways_n); // number of times we traverse the plru binary search tree
    int p_idx = 0; // index into plru bst   
    int evict_idx = 0;
    int assoc = config->ways_n; // search associativity
    
    if(enclave_mode) {

        for(int i=t-1; i>=0; i--) {
            if(assoc > cachelet_assoc) { // must force the search to go toward the enclave's cachelet, regardless of plru bits
                if( p->eway_idx < (evict_idx | (1 << i)) ) { // the cachelet is on the left side
                    //plru[p_idx] = 1; // flip direction so it points away from most-recently used
                    p_idx = 2*p_idx + 1; // left child
                } else { // cachelet is on the right side
                   	//plru[p_idx] = 0; // flip direction
        	    	p_idx = 2*p_idx + 2; // right child     
        	    	evict_idx |= (1 << i);
                }
            }
            else { // search space is completely inside cachelet; perform normal search and update plru bits
        	    if(plru[p_idx] == 0) {
        	    	plru[p_idx] = 1; // flip direction      
        	    	p_idx = 2*p_idx + 1; // left child
        	    }
        	    else { // go right
        	    	plru[p_idx] = 0; // flip direction
        	    	p_idx = 2*p_idx + 2; // right child     
        	    	evict_idx |= (1 << i);
        	    }
            }
            assoc = (assoc >> 1); // search associativity splits in half
        } // for each traversal

    } else { // non-enclave mode
        
        uint64_t way_bitmap = config->way_bitmaps[get_way_bitmap_idx(config, set_idx)];
        for(int i=t-1; i>=0; i--) {
            // first get the AND values of the left half and right half of the way_bitmap
            // the mask reads assoc/2 bits
            uint64_t mask = (1 << (assoc >> 1)) - 1; // assoc >> 1 divides it by 2 ; +1 is so when you subtract 1, the other bits become 1 (want assoc/2 "1" bits)
            uint64_t left = way_bitmap & mask; // way_bitmap is in the reverse order...
            uint64_t right = way_bitmap & (mask << (assoc >> 1));

            if( (right == mask) && (left == mask)) {
                printf("All ways are occupired by enclaves...\n");
                assert(0);
            }

            // normal search
            if(right != mask && left != mask) {
                 if(plru[p_idx] == 0) {
                 	plru[p_idx] = 1; // flip direction      
        	        p_idx = 2*p_idx + 1; // left childi
                    way_bitmap = left;
                } else {
                	plru[p_idx] = 0; // flip direction
        	        p_idx = 2*p_idx + 2; // right child     
        	        evict_idx |= (1 << i);
                    way_bitmap = right;
                }
            } else { // one half is completely occupied by enclaves
                if(right == mask) { // go left ; the right half is completely occupied by enclaves
                 	plru[p_idx] = 1; // flip direction      
        	        p_idx = 2*p_idx + 1; // left childi
                    way_bitmap = left;
                } else if(left == mask) { // go right ; the left half is completely occupied by enclaves
                	plru[p_idx] = 0; // flip direction
        	        p_idx = 2*p_idx + 2; // right child     
        	        evict_idx |= (1 << i);
                    way_bitmap = right;
                } else {
                    printf("right and left bits don't equal the mask\n");
                    assert(0);
                }
            }

            assoc = (assoc >> 1); // search associativity splits in half
        } // for each traversal

    }
	
    return evict_idx;

}

// this eviction policy favors enclave lines to stay in the cache ; the evict_idx is more likely to point to a non-enclave line
int evict_sgx_plru(cache_t* c, cache_config_t* config, int cache_type, int set_idx) {

    char* plru = c->plru[cache_type][set_idx];

	int t = log2(config->ways_n); // number of times we traverse the plru binary search tree
  	int p_idx = 0; // index into plru bst   
  	int evict_idx = 0;

  	for(int i=t-1; i>=0; i--) {
  	    
        // last level of plru ; bias (wants to evict non-enclave lines first)
        if(i == 0) {
            int left = evict_idx;
            int right = evict_idx | (1 << i);
            cacheline_t* set = c->cache[cache_type][set_idx];

            // want to evict the non-enclave line first
            if(set[left].enclave_mode != set[right].enclave_mode) {
                if(!set[left].enclave_mode) evict_idx = left;
                else evict_idx = right;
 
                break; 
            }
            // else, proceed as normally (pick PLRU)
        } 
        if(plru[p_idx] == 0) {
  	        plru[p_idx] = 1; // set direction      
  	     	p_idx = 2*p_idx + 1; // left child 
  	     }
  	     else { // go right
  	        plru[p_idx] = 0; // set direction
  	        p_idx = 2*p_idx + 2; // right child     
  	        evict_idx |= (1 << i);  
  	     }
  	}

    assert(evict_idx >= 0 && evict_idx < config->ways_n);	
    return evict_idx;	

}

// returns the way where the line was evicted
int pick_victim_way(sim_t* sim, process_t* p, cache_t* c, cache_config_t* config, int cache_type, int set_idx, int enclave_mode) {
    int evict_idx = -1;
    switch(config->evict_policy) {
        case EVICT_PLRU:
            if(config->use_cachelet && sim->cachelet_assoc >= 1) {
                evict_idx = evict_plru_cachelet(p, sim->cachelet_assoc, c, config, cache_type, set_idx, enclave_mode);
            }
            else evict_idx = evict_plru(c, config, cache_type, set_idx, enclave_mode);
            break;
        case EVICT_SGX_PLRU:
            ;
            float prob = (float) rand() / RAND_MAX;
            if(prob <= config->sgx_plru_rate) {
                evict_idx = evict_sgx_plru(c, config, cache_type, set_idx); 
                update_stat(sim, c->nstat_counts[cache_type], STAT_EVICT_SGX_PLRU, c->cache[cache_type][set_idx][evict_idx].enclave_mode);
            } else {
                evict_idx = evict_plru(c, config, cache_type, set_idx, enclave_mode);
                update_stat(sim, c->nstat_counts[cache_type], STAT_EVICT_PLRU, c->cache[cache_type][set_idx][evict_idx].enclave_mode);
            } 
            break;
        default:
            printf("Eviction policy not set!\n");
            break;
    }
    return evict_idx;
}

// returns the way where the cache line is ; if there is a free splot, free is set
int search_set(sim_t* sim, process_t* p, cache_config_t* config, cacheline_t* set, int set_idx, int* free, int eid, uint64_t tag) {

    int enclave_mode = p->access->enclave_mode;
	
	if( (enclave_mode && config->set_partition && !config->use_cachelet) || 
        (enclave_mode && config->use_cachelet && sim->cachelet_assoc <= 1) ) { // direct-mapped
        cacheline_t* cl = &set[p->eway_idx];
        if(cl->valid && cl->tag == tag && cl->eid == p->eid) return p->eway_idx; // cache hit
		else if(!cl->valid) *free = p->eway_idx;
        return -1; 
	}

	/* calculates the range of cache ways depending on if this access is in enclave_mode or not */
	int low_w = 0; // inclusive
    int high_w = config->ways_n; // exclusive	
	if(config->use_cachelet && sim->cachelet_assoc > 1) {
        if(enclave_mode) {
            low_w = p->eway_idx;
            high_w = low_w + sim->cachelet_assoc;
        }
    } else {
        if(config->partition) {
	    	if(enclave_mode) high_w = config->enclave_ways_n;
	    	else low_w = config->enclave_ways_n;
	    } 
    }

    *free = -1;
    uint64_t* way_bitmap = &config->way_bitmaps[get_way_bitmap_idx(config, set_idx)];
	for(int w=low_w; w<high_w; w++) {
		cacheline_t* cl = &set[w];
        if(config->use_cachelet && !enclave_mode && read_way_bitmap(way_bitmap, w)) {
            continue; // must check if the way is allocated to an enclave
        }
        
	    if(!cl->valid && *free == -1) *free = w;
        if(cl->valid && cl->enclave_mode == enclave_mode && cl->tag == tag && cl->eid == eid) return w;
        
	}	
	
    return -1;

}

int search_cache(int action, sim_t* sim, process_t* p, cache_t* c, int* free) {
   
    access_t* a = p->access;
     
    int cache_type = get_cache_type(c, a->op); // data, insn, or unifid	
	cache_config_t* config = c->config[cache_type];
 
	uint64_t tag;
	int set_idx = get_set_and_tag(sim, p, c, cache_type, config, a->addr, a->enclave_mode, &tag);
	
    *free = -1;
    int hit = -1;	
    cacheline_t* set = c->cache[cache_type][set_idx];	
	
    if(     (a->enclave_mode && config->set_partition && !config->use_cachelet) || 
            (a->enclave_mode && config->use_cachelet && sim->cachelet_assoc <= 1) ) { // no need to search the ways ; is a direct-mapped cache
        cacheline_t* cl = &set[p->eway_idx];
        if(cl->valid && cl->tag == tag && cl->eid == p->eid) hit = p->eway_idx; // cache hit
        else if(!cl->valid) *free = p->eway_idx;
    } else hit = search_set(sim, p, config, set, set_idx, free, p->eid, tag);
    
    
    if(hit != -1) { // cache hit
         if(config->evict_policy == EVICT_PLRU || config->evict_policy == EVICT_SGX_PLRU) update_plru(c->plru[cache_type][set_idx], config->ways_n, hit);
         return hit;
    }

    /* Optional actions after the search */
    
    if(action == PLACE_LINE) { // there was no free spot ; must evict
        if(*free != -1) { // free spot in the cache
            edit_line(SET_LINE, sim, p, c, config, set, set_idx, *free); // sets a line in this cache ; if inclusive then it will set in other cache levels	
        } else {
            int evict_idx;
            if( (a->enclave_mode && config->set_partition && !config->use_cachelet) || 
                (a->enclave_mode && config->use_cachelet && sim->cachelet_assoc <= 1) ) evict_idx = p->eway_idx; // direct-mapped
            else {
                evict_idx = pick_victim_way(sim, p, c, config, cache_type, set_idx, a->enclave_mode);
            }

            edit_line(EVICT_LINE, sim, p, c, config, set, set_idx, evict_idx); // removes line in this cache ; if inclusive then it will remove from other cache levels	
            edit_line(SET_LINE, sim, p, c, config, set, set_idx, evict_idx); // sets a line in this cache ; if inclusive then it will set in other cache levels	
        }
    }
    return hit;
}

void access_cache(sim_t* sim, process_t* p) {
     
	core_t* core = p->core;	
	cache_t* c = core->cache; // first level private cache

    access_t* a = p->access;
    int enclave_mode = a->enclave_mode;
    int op = a->op;
   
    // stats 
    update_stat_mem_access(sim, p->nstat_counts, op, enclave_mode);
	
    while(c) { // search each level of cache
        
        int cache_type = get_cache_type(c, op); // data, insn, or unified	
		cache_config_t* config = c->config[cache_type];

        // stats 
        update_stat_mem_access(sim, c->nstat_counts[cache_type], op, enclave_mode);
        if(!c->next) update_stat(sim, p->nstat_counts, STAT_LLC_ACCESS, enclave_mode);
       
        int free = -1;
        int hit = -1;

        hit = search_cache(SEARCH_LINE, sim, p, c, &free); // searches the cache ; hits update plru
        // stats partition factor time
        if(config->set_partition) update_stat_partition_time(sim, p->nstat_counts, p->partition_factor, enclave_mode);

		if(hit != -1) {
            // stats
            update_stat_all(sim, c, cache_type, p, STAT_CACHE_HIT, enclave_mode);
            if(!c->next) update_stat(sim, p->nstat_counts, STAT_LLC_HIT, enclave_mode);
			
            if(config->level != 1) search_cache(PLACE_LINE, sim, p, p->core->cache, &free); // place into first level cache
			//break;
		} 
        else { // cache miss
            // stats
            update_stat(sim, c->nstat_counts[cache_type], STAT_CACHE_MISS, enclave_mode);
            
            if(c->next == NULL) { // last level cache ; put line into all caches	
                // stats
                update_stat(sim, p->nstat_counts, STAT_CACHE_MISS, enclave_mode);
                if(free != -1) update_stat(sim, p->nstat_counts, STAT_LLC_COLD_MISS, enclave_mode);

                // dynamic cachelets
                if(sim->dyn_threshold > 0 && config->use_cachelet && enclave_mode) {
                    if(op == LOAD_OP || op == STORE_OP) p->miss_counter++;
                }

                cache_t* cache_ptr = p->core->cache;	
				while(cache_ptr) { // place line in all caches
				    search_cache(PLACE_LINE, sim, p, cache_ptr, &free);
                    if(free != -1) update_stat(sim, cache_ptr->nstat_counts[get_cache_type(cache_ptr, op)], STAT_CACHE_COLD_MISS, enclave_mode);
                    cache_ptr = cache_ptr->next;
				}
                
                // prefetch lines on a cache miss
                if(sim->prefetch) {
                    if(sim->prefetch == nextLine || sim->prefetch == nextTwoLines) {
                        cache_t* cache_ptr = p->core->cache;
                        int rounds = sim->prefetch; // either 1 or 2
				        uint64_t addr = p->access->addr;
                        while(cache_ptr) { // place line in all caches
                            int line_size = cache_ptr->config[get_cache_type(cache_ptr, op)]->line_size;
                            for(int r=0; r<rounds; r++) {
                                p->access->addr += line_size; // update address
                                search_cache(PLACE_LINE, sim, p, cache_ptr, &free);
                            }
                            p->access->addr = addr; // restore original address for next level of cache
                            cache_ptr = cache_ptr->next;
				        }
                    }
                } // if(sim->prefetch)
			} // last level cache

            // dynamic cachelets
            //if(sim->dyn_threshold > 0 && config->use_cachelet && enclave_mode) {
            //    if(op == LOAD_OP || op == STORE_OP) p->miss_counter++;
            //    //uint64_t e_insn = get_stat_count(p->nstat_counts, STAT_INSN, enclave_mode);
            //    //// check if resize is necessary
            //    //if(e_insn > 0 && e_insn % sim->dyn_rate == 0) {
            //    //    //fprintf(sim->miss_csv, "%s,%i,%i,%llu,%llu\n", p->tracefile->filename, p->eid, p->num_cachelets, e_insn, p->miss_counter);
            //    //    if(p->miss_counter >= sim->dyn_threshold) {
            //    //        update_stat(sim, p->nstat_counts, STAT_REACHED_RESIZE_THRESHOLD, enclave_mode);
            //    //        // update max seen
            //    //        if( p->miss_counter > get_stat_count(p->nstat_counts, STAT_MAX_MISS_COUNTER, enclave_mode) ) {
            //    //            set_stat_count(p->nstat_counts, STAT_MAX_MISS_COUNTER, enclave_mode, p->miss_counter);
            //    //        }

            //    //        // increase enclave cache space if enough space
            //    //        if(p->num_cachelets*2 < config->max_partition) {
            //    //            printf("%s resizing at %llu misses\n", p->tracefile->filename, p->miss_counter);
            //    //            p->num_cachelets *= 2; // double the amount of cachelets
            //    //            // clear the increased cache space
            //    //            int sets_n = (config->sets_n/config->max_partition) * p->num_cachelets;
            //    //            for(int s=0; s<sets_n; s++) {
            //    //                cacheline_t* set = c->cache[cache_type][s];
            //    //                for(int w=0; w<sim->cachelet_assoc; w++) {
            //    //                    edit_line(EVICT_LINE, sim, p, c, config, set, s, w); // removes line in this cache ; if inclusive then it will remove from other cache levels	
            //    //                    //set[w].valid = 0;
            //    //                }
            //    //            }
            //    //            update_stat(sim, p->nstat_counts, STAT_RESIZED, enclave_mode);
            //    //        }
            //    //    }
            //    //} // check if resize ; end
            //} // dynamic cachelets ; end

            
		} // cache miss ; end

        // check for dynamic cachelet expansion
        uint64_t e_insn = get_stat_count(p->nstat_counts, STAT_INSN, ENCLAVE);
        
        // check for dynamic caches downsizing
        if(sim->dyn_downsize_threshold > 0 && config->use_cachelet && enclave_mode && e_insn > 0 && e_insn % sim->dyn_downsize_rate == 0) {
            // check if resize is necessary
            if(p->miss_counter <= sim->dyn_downsize_threshold) {
                update_stat(sim, p->nstat_counts, STAT_REACHED_DOWNSIZE_THRESHOLD, enclave_mode);
                
                // decrease enclave cache space if possible
                if(p->num_cachelets > 1) {
                    printf("%s downsizing at %" PRIu64 "misses to %i cachelets\n", p->tracefile->filename, p->miss_counter, p->num_cachelets/2);
                    // clear the cache space first
                    int sets_n = (config->sets_n/config->max_partition) * p->num_cachelets;
                    for(int s=0; s<sets_n; s++) {
                        cacheline_t* set = c->cache[cache_type][s];
                        for(int w=0; w<sim->cachelet_assoc; w++) {
                            edit_line(EVICT_LINE, sim, p, c, config, set, s, w); // removes line in this cache ; if inclusive then it will remove from other cache levels	
                        }
                    }
                    p->num_cachelets /= 2; // halven the amount of cachelets
                    update_stat(sim, p->nstat_counts, STAT_DOWNSIZED, enclave_mode);
                } // changed enclave cache size
            } // check if a resize is necessary
        }

        if(sim->dyn_threshold > 0 && config->use_cachelet && enclave_mode && e_insn > 0 && e_insn % sim->dyn_rate == 0) { // check and reset miss counter
            // check if resize is necessary
            if(p->miss_counter >= sim->dyn_threshold) {
                update_stat(sim, p->nstat_counts, STAT_REACHED_RESIZE_THRESHOLD, enclave_mode);
                
                // increase enclave cache space if enough space
                if(p->num_cachelets*2 <= config->max_partition) {
                    printf("%s resizing at %" PRIu64 " misses to %i cachelets\n", p->tracefile->filename, p->miss_counter, p->num_cachelets*2);
                    p->num_cachelets *= 2; // double the amount of cachelets
                    // clear the increased cache space
                    int sets_n = (config->sets_n/config->max_partition) * p->num_cachelets;
                    for(int s=0; s<sets_n; s++) {
                        cacheline_t* set = c->cache[cache_type][s];
                        for(int w=0; w<sim->cachelet_assoc; w++) {
                            edit_line(EVICT_LINE, sim, p, c, config, set, s, w); // removes line in this cache ; if inclusive then it will remove from other cache levels	
                            //set[w].valid = 0;
                        }
                    }
                    update_stat(sim, p->nstat_counts, STAT_RESIZED, enclave_mode);
                } // change enclave cache size
            } // resize possible ; end
            
            // update max seen
            if( p->miss_counter > get_stat_count(p->nstat_counts, STAT_MAX_MISS_COUNTER, enclave_mode) ) {
                set_stat_count(p->nstat_counts, STAT_MAX_MISS_COUNTER, enclave_mode, p->miss_counter);
            }

            if(sim->trace_n >= START_STAT) {
                fprintf(sim->miss_csv, "%s,%i,%i,%" PRIu64 ",%" PRIu64 "\n", p->tracefile->filename, p->eid, p->num_cachelets, e_insn, p->miss_counter);
            }
            p->miss_counter = 0; // reset
        }
       
        if(hit != -1) break;
        c = c->next;

	} // while(c) ; end

}
