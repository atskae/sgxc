import sys
import re
from scipy import stats as sci_stats # harmonic mean
import csv
import glob
import pandas as pd
import os

if len(sys.argv) < 2:
    print('This script compiles the data from all nstat files into one .csv file.')
    print('Usage: nstat-to-graph.py <nstat directory>')
    sys.exit()

nstat_dir = sys.argv[1]
if nstat_dir[-1] != '/':
    nstat_dir += '/'

# original stats from sgxc
nstat_stats = [
    'STAT_LLC_ACCESS',
    'STAT_LLC_HIT',
    'STAT_INSN',
    'STAT_TRACE',
    'STAT_CACHE_MISS',
    'STAT_LLC_COLD_MISS',
    'STAT_IS_INCLUSION_VICTIM',
    'STAT_RESIZED',
    'STAT_MAX_MISS_COUNTER',
    'STAT_DOWNSIZED',
    'STAT_REACHED_DOWNSIZE_THRESHOLD'
    # partition time
    #'STAT_2_PARTITION',
    #'STAT_4_PARTITION',
    #'STAT_8_PARTITION',
    #'STAT_16_PARTITION',
    #'STAT_32_PARTITION',
    #'STAT_64P_PARTITION',
]

derived_stats = [
    'STAT_LLC_MISS_RATE',
    'STAT_MPKI',
    'STAT_LLC_COLD_MISS_PERCENT',
    'STAT_MISS_RATE',
    'STAT_CPI'
]

# parameters
ne_miss_penalty_cycles = 250
e_miss_penalty_cycles = 350

stat_llc_mr = 0
stat_mpki = 1
stat_llc_cold_p = 2
stat_mr = 3
stat_cpi = 4

# keys to programs
k_num_threads = 'num threads'
k_t = 'total'
k_ne = 'non-enclave'
k_e = 'enclave'

enclave_modes = [k_ne, k_e, k_t]

num_files = 0
for nstat in glob.glob(nstat_dir + "*.nstat.txt"):

    stats = nstat_stats[:]
    nstat_file = None
    try:
        nstat_file = open(nstat, 'r')
    except:
        print('Failed to open %s' % (f))
        continue

    programs = {} # p -> stats 
    for line in nstat_file:
        p = line.split('::')[0]
        stat = line.split('::')[2]
        if '.out' not in p or stat not in stats:
            continue

        # remove the .out
        p = p.replace('.out', '')    
        if p not in programs.keys():
            programs[p] = {}
            programs[p][k_num_threads] = 0
            for s in stats:
                programs[p][s+k_t] = []
                programs[p][s+k_ne] = []
                programs[p][s+k_e] = []
     
        programs[p][stat+k_ne].append(int(re.findall('\d+', line.split('::')[3])[0]))
        programs[p][stat+k_e].append(int(re.findall('\d+', line.split('::')[4])[0]))
        programs[p][stat+k_t].append(int(re.findall('\d+', line.split('::')[5])[0]))
        
    nstat_file.close()
    
    # calculate number of threads
    for p in programs.keys():
        programs[p][k_num_threads] = len(programs[p][stats[0]+k_t])
    
    # calculate derived statistics
    for p in programs.keys():
        stats_p = programs[p]
        num_threads = programs[p][k_num_threads]
        # print('%s (%i threads)' % (p, num_threads))   
    
        for d in derived_stats:
            for e in enclave_modes:
                stats_p[d+e] = []
    
        for i in range(0, num_threads):
            for e in enclave_modes:
                total_misses = stats_p['STAT_CACHE_MISS'+e][i] 
   
                if stats_p['STAT_LLC_ACCESS'+e][i] != 0:
                    llc_misses = stats_p['STAT_LLC_ACCESS'+e][i] - stats_p['STAT_LLC_HIT'+e][i]
                    mr = float(llc_misses) / float(stats_p['STAT_LLC_ACCESS'+e][i]) * 100
                    if mr > 100:
                        print('LLC miss rate of %f does not make sense...' % mr)
                    stats_p[derived_stats[stat_llc_mr]+e].append(mr)

                    # percentage of llc cold misses
                    llc_cold_p = 0.0
                    if stats_p['STAT_LLC_COLD_MISS'+e][i] > 0 and llc_misses > 0:
                        llc_cold_p = float(stats_p['STAT_LLC_COLD_MISS'+e][i]) / float(llc_misses) * 100
                        stats_p[derived_stats[stat_llc_cold_p]+e].append(llc_cold_p)

                total_insn = stats_p['STAT_INSN'+e][i]
                if total_insn != 0 and total_misses != 0:
                    mpki = float(total_misses)*1000 / float(total_insn)
                    stats_p[derived_stats[stat_mpki]+e].append(mpki)

                # overall miss rate
                total_traces = stats_p['STAT_TRACE'+e][i]
                if total_traces > 0:
                    mr = float(total_misses) / float(total_traces) * 100
                    stats_p[derived_stats[stat_mr]+e].append(mr)

                # CPI
                total_cycles = 0
                if e == 'enclave':
                    total_cycles = total_insn + total_misses * e_miss_penalty_cycles
                elif e == 'non-enclave':
                    total_cycles = total_insn + total_misses * ne_miss_penalty_cycles
                elif e == 'total':
                    ne_misses = stats_p['STAT_CACHE_MISS'+k_ne][i]
                    e_misses = stats_p['STAT_CACHE_MISS'+k_e][i]
                    total_cycles =  total_insn + ne_misses * ne_miss_penalty_cycles + e_misses * e_miss_penalty_cycles
                cpi = 0
                try:
                    cpi = float(total_cycles) / float(total_insn)
                except:
                    print('CPI total insn %i' % total_insn)
                stats_p[derived_stats[stat_cpi]+e].append(cpi)
    
    # add derived class keys to stats
    stats.append(derived_stats[stat_llc_mr])
    stats.append(derived_stats[stat_mpki])
    stats.append(derived_stats[stat_llc_cold_p])
    stats.append(derived_stats[stat_mr])
    stats.append(derived_stats[stat_cpi])

    #try:
    #    partition = nstat.split('.')[0].split('ws')[1]
    #    print('\n=== Partition factor %s ===' % partition)
    #except:
    #    print('\n=== Partition factor 0 ===')
    #
    #for p in programs.keys():
    #    # mpki and partition time
    #    partition_times = []
    #    for i in range(0, 6):
    #        partition_factor = int(pow(2, i))
    #        if partition_factor == 1:
    #            key = 'STAT_64P_PARTITION' + k_e
    #        else:
    #            key = 'STAT_' + str(partition_factor) + '_PARTITION' + k_e
    #        partition_times.append(programs[p][key])
    #    print('partition times', p, partition_times)
    #    print('mpkis (unsorted)', programs[p]['STAT_MPKI'+k_e])
    #    
    #    mpkis = []
    #    mpkis = sorted(programs[p]['STAT_MPKI'+k_e])
    #    print('%s %s' % (nstat, p))
    #    if len(mpkis) == 0:
    #        print('MPKI_non_enclave')
    #        mpkis = sorted(programs[p]['STAT_MPKI'+k_ne])
    #    print(mpkis)
    #    print('Highest/Lowest = %f, hm=%f' % (float(mpkis[len(mpkis)-1]/mpkis[0]), sci_stats.hmean(mpkis)) )
    #    print('---')
    
    #print all stats, included derived stats
    #for p in programs.keys():
    #    print(p)
    #    for s in stats:
    #        print('stat: %s' % s)
    #        print(programs[p][s+k_ne])
    #        print(programs[p][s+k_e])
    #        print(programs[p][s+k_t])
    
    # create .csv file
    out_file = nstat.replace('.nstat.txt', '.graph.csv')
    with open(out_file, 'wb') as f:
        csv_file = csv.writer(f, delimiter=',')
        header = []
        header.append('trace')
        header.append('num threads')
        for s in stats:
            for e in enclave_modes:
                h = s + '_' + e
                h = h.replace('STAT_', '')
                header.append(h)
        csv_file.writerow(header)
    
        for p in programs:
            row = []
            row.append(p)
            row.append(programs[p][k_num_threads])
            for s in stats:
                for e in enclave_modes:
                    if s in derived_stats:
                        hm = 0.0
                        try:
                            hm = sci_stats.hmean(programs[p][s+e])
                        except:
                            print('Cannot computer harmonic mean of %s' % s+'_'+e)
                            print('%s, %s' % (s, p))
                            print(programs[p][s+e])
                        row.append(hm)
                    else:
                        #print('total for %s %s' % (s, e))
                        total = 0
                        for d in programs[p][s+e]:
                            total += d 
                        row.append(total)
            csv_file.writerow(row)

    print('Data written to %s' % out_file)
    num_files += 1

print('Converted %i nstat.txt to graph.csv' % num_files)

# compile all graph.csv files to 1 .csv with normalized values to baseline
all_stats = nstat_stats + derived_stats # calculate normalized values of these
for i, s in enumerate(all_stats):
    all_stats[i] = all_stats[i].replace('STAT_', '')

def get_key(stat, enclave_mode):
    return stat + '_' + enclave_mode

def get_num_threads(s):
    if '.sgxc2' in s:
        s = s.replace('.sgxc2', '')
    temp = s.split('.') 
    if len(temp) < 3:
        return ''
    
    return temp[len(temp)-3]

# get baseline values first
base = {} # program -> num_threads -> stat -> value 
#total_traces = []
for b in glob.glob(nstat_dir + "*basic*.graph.csv"):
    num_threads = get_num_threads(b)
 
    df = pd.read_csv(b)
    for index, row in df.iterrows():
        p = row['trace']
        if p not in base.keys():
            base[p] = {}
        if num_threads not in base[p].keys():    
            base[p][num_threads] = {}
        
        p_base = base[p][num_threads]
        for stat in all_stats:
            for e in enclave_modes:
                key = get_key(stat, e)
                #print('base key %s for %s' % (key))
                p_base[key] = row[key]
#                if stat == 'TRACE' and e == 'total':
#                    total_traces.append(int(row[key]))

#total_traces.sort()
#print('total_traces', total_traces)
#print('base', base)

#print('Baseline keys')
#for p in base.keys():
#    for prog in base[p].keys():
#        for k in base[p][prog].keys():
#            print('p=%s, prog=%s, key=%s' % (p, prog, k))

data = {} # program -> config ->stat -> value
for graph in glob.glob(nstat_dir + "*.graph.csv"):
    #if 'basic' in graph:
    #    continue

    num_threads = get_num_threads(graph)
    #if int(num_threads.split('-')[0]) + int(num_threads.split('-')[1]) == 64:
    #    print('Omitting 64-threaded workload')
    #    continue

    config = os.path.basename(graph.split('.')[0])
    print('file=%s,config=%s' % (graph, config))

    df = pd.read_csv(graph)
    for index, row in df.iterrows():
        p = row['trace']
        if p not in data.keys():
            data[p] = {}
        if config not in data[p].keys():    
            data[p][config + '.' + num_threads] = {}

        p_data = data[p][config + '.' + num_threads]
        
        # absolute values
        for stat in all_stats:
            for e in enclave_modes:
                key = get_key(stat, e)
                p_data[key] = row[key]
        
        # normalized
        for stat in all_stats:
            for e in enclave_modes:
                key = get_key(stat, e)
                key_n = key + '_normalized'
                # calculate normalized value
                b = None
                try:
                    b = base[p][num_threads][key]
                except:
                    print('%s base for %s, %s, key=%s not found' % (graph, p, num_threads, key))
                    p_data[key_n] = 0.0
                    continue

                try:
                    p_data[key_n] = float(row[key]) / float(b)
                    #print('p=%s, config=%s, base=%s, stat=%s, %f' % (p, config, num_threads, key, p_data[key]))
                except:
                    if base != 0:
                        print('%s normalized was not computed for %s,%s' % (key, p, config))
                    p_data[key_n] = 0.0

csv_header = []
csv_header.append('trace')
csv_header.append('config')
csv_header.append('progfile')
for s in all_stats:
    for e in enclave_modes:
        csv_header.append(s + '_' + e) # absoute
        csv_header.append(s + '_' + e + '_normalized') # normalized

def config_sort(config):
    if 'basic' in config:
        num_threads = config.split('.')[1]
        nums = re.findall("\d+", num_threads)
        if len(nums) == 0:
            return -1
        return int(nums[0]) + int(nums[1])


    nums = re.findall("\d+", config)
    if len(nums) == 1:
        return int(nums[0])
    elif re.match('\d+\-\d+\-\d+', config): # dynamic cachelets
        #return int(nums[0]) + int(nums[1]) + int(nums[2])
        return (int(nums[0]), int(nums[1]), int(nums[2]))
    else:
        return int(nums[0]) * 100 + int(nums[1])

csv_rows = []
for p in data.keys():
    for config in sorted(data[p].keys(), key=config_sort):
        row = []
        row.append(p)
        row.append(config.split('.')[0])
        row.append(config.split('.')[1])      
 
        for stat in all_stats:
            for e in enclave_modes:
                # absolute
                key = get_key(stat, e)
                d = data[p][config][key]
                row.append(d)
                # normalized
                key = get_key(stat, e) + '_normalized'
                d = data[p][config][key]
                row.append(d)

        csv_rows.append(row)

# write graph data 
out_file = nstat_dir + 'nstat-graph-all.csv'
with open(out_file, 'wb') as csvfile: 
    filewriter = csv.writer(csvfile, delimiter=',', quotechar='|', quoting=csv.QUOTE_MINIMAL)   
    filewriter.writerow(csv_header)
    for r in csv_rows:
        filewriter.writerow(r)
csvfile.close()

print('All nstats written to %s' % out_file)
