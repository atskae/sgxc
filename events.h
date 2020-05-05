/* Events to collect statistics on */

ADD_EVENT(STAT_INVALID, "Invalid"),

ADD_EVENT(STAT_TRACE, "A memory reference"),

ADD_EVENT(STAT_LOAD, "Load op"),
ADD_EVENT(STAT_STORE, "Store op"),
ADD_EVENT(STAT_INSN, "Instruction op"),

ADD_EVENT(STAT_CACHE_HIT, "Cache hit"),
ADD_EVENT(STAT_CACHE_MISS, "Cache miss, includes cold misses"),
ADD_EVENT(STAT_CACHE_COLD_MISS, "Cold miss that occupied a free spot in the cache"), // only makes sense for cache_t, not process_t... use LLC_COLD_MISS for process_t

// only meaningful per process only
ADD_EVENT(STAT_LLC_ACCESS, "Accesses to the last-level cache"),
ADD_EVENT(STAT_LLC_HIT, "Cache hit on the last-level cache"),
ADD_EVENT(STAT_LLC_COLD_MISS, "Cold miss on the last-level cache"),

ADD_EVENT(STAT_EVICT_INCLUSION_VICTIM, "Evicted a line to maintain inclusive property"),
ADD_EVENT(STAT_EVICT_INCLUSION_VICTIM_OTHER, "Evicted another process's line to maintain inclusive property"),
ADD_EVENT(STAT_IS_INCLUSION_VICTIM, "Their line evicted to maintain inclusive cache property"),
ADD_EVENT(STAT_IS_INCLUSION_VICTIM_OTHER, "Their line evicted by another process to maintain inclusive cache property"),

ADD_EVENT(STAT_NO_PARTITION, "The number of memory references made with no partitioning"),
ADD_EVENT(STAT_2_PARTITION, "The number of memory references made with 1/2 way"),
ADD_EVENT(STAT_4_PARTITION, "The number of memory references made with 1/4 way"),
ADD_EVENT(STAT_8_PARTITION, "The number of memory references made with 1/8 way"),
ADD_EVENT(STAT_16_PARTITION, "The number of memory references made with 1/16 way"),
ADD_EVENT(STAT_32_PARTITION, "The number of memory references made with 1/32 way"),
ADD_EVENT(STAT_64P_PARTITION, "The number of memory references made with 1/64 way or less"), // 64 "plus"

ADD_EVENT(STAT_DIRTY_LINES, "Dirty cache lines that are evicted"),
ADD_EVENT(STAT_EVICT_OTHER, "Process evicts a cache line that isn't their cache line"),

ADD_EVENT(STAT_EVICT_PLRU, "Evicted line using plru (with sgx_plru probability of < 1.0)"),
ADD_EVENT(STAT_EVICT_SGX_PLRU, "Evicted line using sgx_plru"),

ADD_EVENT(STAT_REACHED_RESIZE_THRESHOLD, "Number of times that the number of misses from memory references reached the resize threshold for dynamic cachelets"),
ADD_EVENT(STAT_RESIZED, "Number of times that the enclave cache space increased due to reaching the threshold"),
ADD_EVENT(STAT_MAX_MISS_COUNTER, "Maximum miss counter value seen"),

ADD_EVENT(STAT_REACHED_DOWNSIZE_THRESHOLD, "Number of times that the number of misses from memory references reached the downsize threshold for dynamic cachelets"),
ADD_EVENT(STAT_DOWNSIZED, "Number of times that the enclave cache space decreased due to reaching the threshold"),
