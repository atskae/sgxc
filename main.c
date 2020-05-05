#define _GNU_SOURCE
#include <stdio.h>

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h> // rand()
#include <assert.h>

#include "sim.h"
#include "utils.h" 

int main(int argc, char* argv[]) {

    if(argc < 3) {
		printf("./sgxc <.config> <.prog>\n");
		return 1;
	}	
	
    //print_all_events();
	
    sim_t sim;	
	init_sim(&sim, argv);

    // dynamic cachelets currently only work for 1 thread workloads
    if(sim.prog_n != 1 && (sim.dyn_threshold > 0 || sim.dyn_rate > 0) ) {
        printf("Dynamic cachelets only supported for 1-thread workloads\n");
        return 0;
    }
	
	print_all_config(&sim);
	if(sim.stop_early) printf("Will stop simulation when the first program completes.\n");
	printf("(%i cores, %i progs) After %llu traces, will collect statistics for %llu traces\n", sim.cores_n, sim.prog_n, START_STAT, MAX_TRACES-START_STAT);
		
	/*

		Main simulation loop		

	*/
	
	srand(time(0));	
	
    char* line = NULL;
	size_t size = 0;
	int num_done = 0;	
	
    // time program
	clock_t start, end;
	start = clock();
	while(1) {
		
		int queue_items_n = 0; // reset
		for(int i=0; i<sim.cores_n; i++) { // fetch a trace from each core
		
			core_t* core = &sim.cores[i];
			if(core->current_process < 0) continue;
			process_t* p = &core->processes[core->current_process];
	
			long int pos = ftell(p->trace);		
			ssize_t ret = getline(&line, &size, p->trace);
			
            // loop file pointer to beginning
			if(ret < 0) {
				rewind(p->trace);
				pos = ftell(p->trace);
				ret = getline(&line, &size, p->trace);	
			}
	
			if(pos == p->trace_offset) p->seen_offset_n++;
			if(p->seen_offset_n > 1 && !p->done) { // if completed, process rewinds and continues until the final process completes
				p->done = 1;
				num_done++;
				printf("Process %i completed trace. Will rewind.\n", p->eid);
				if(num_done == sim.prog_n || sim.stop_early) break;	
			}

			access_t* a = &sim.queue[queue_items_n];
			a->eid = p->eid;
			a->core_id = core->id;
			sscanf(line, "%lf %i %p %i\n", &a->interval, &a->enclave_mode, (void**) &a->addr, &a->op);
			core->clock += a->interval;
			a->timestamp = core->clock;
            if(p->tracefile->always != -1) a->enclave_mode = p->tracefile->always; // if always is set, the entire trace is either always enclave mode or not
			queue_items_n++;

		} // each core ; end
		
        if(num_done == sim.prog_n || (num_done > 0 && sim.stop_early)) break;

		// order traces by time stamp, with trace at index 0 to be earliest in time	
		if(queue_items_n > 1) quicksort(sim.queue, 0, queue_items_n-1);
		
        for(int i=0; i<queue_items_n; i++) {
			access_t* a = &sim.queue[i];
			if(sim.ignore_ne && a->enclave_mode == 0) {
                sim.trace_n++;
                continue;
            }

            core_t* core = &sim.cores[a->core_id];	
			process_t* p = &core->processes[core->current_process];
			assert(p->valid);
            
            p->access = a;
            access_cache(&sim, p); // send cache access to sim 
	            
            // stats
            sim.trace_n++;
            update_stat_mem_access(&sim, sim.nstat_counts, a->op, a->enclave_mode);

            if(sim.trace_n >= MAX_TRACES) break;
		}
        if(sim.trace_n >= MAX_TRACES) break;
        if(sim.trace_n % 100000000 == 0) printf("Reached %lu accesses in %.2f minutes\n", sim.trace_n, (((double) (clock() - start)) / CLOCKS_PER_SEC)/60.0);

	} // main loop ; end

	end = clock();
	sim.elapsed = ((double) (end - start)) / CLOCKS_PER_SEC;	
	printf("---\n%i/%i processes completed in %.5f min\n", num_done, sim.prog_n, sim.elapsed/60.0);

    get_all_stats(&sim);
    get_all_config(&sim);

    // dynamic caches
    if(sim.dyn_threshold > 0) {
        fclose(sim.miss_csv);
    }

	return 0;
}
