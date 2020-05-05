# must run with Python 3.5 or newer
# runs every combination of config and prog files
import subprocess 
import os, sys
import glob, re
import math # next power of 2

def get_next_power2(num):
    pos = math.ceil(math.log2(num))
    return int(math.pow(2, pos))

config_dir = 'config/inclusive/cachelet/'
prog_dir = 'prog/crypto/'

failed = 0
config_path = config_dir + '*.config'
prog_path = prog_dir + '*.prog'

ws_pattern = re.compile('\d+ws\d+') # these configs have restrictions on what prog files it can run with
prog_pattern = re.compile('\d+\-\d+')

failed = []
executed = []
progs = []
for config in glob.glob(config_path):
	c = os.path.basename(config)

	for prog_file in glob.glob(prog_path):	
		p = os.path.basename(prog_file)
		if ws_pattern.match(c):
			nums = re.findall('\d+', c)
			ways_n = int(nums[0]) # number of enclave ways
			max_partition = int(nums[1]) # number of partitions per way
			threads_n = 1; # for individual runs like <program name>.prog
			if prog_pattern.match(p):
				threads_n = int(re.findall('\d+', p)[0]) # obtains the number of enclave programs
			if get_next_power2(threads_n) != ways_n * max_partition: # the next power of two of thread_n should = ways_n * max_partition 
				continue
	
		args = ['./sgxc', config, prog_file]
		
		print('Will execute: ', args)
		try:
			#subprocess.run(args)
			progs.append(subprocess.Popen(args)) # run in parallel
		except:
			print("Running %s failed." % config)
			failed.append(args)
			continue
	
		executed.append(args)

# wait for all programs to finish
for p in progs:
    p.wait()

if len(failed) > 0:
    print('%i configs failed to run' % len(failed))
    if len(executed) == 0:
        print("All runs failed. Did you run the script with python3?")
    else: 
        print(failed)
else:
    print("All %i configs successfully ran." % len(executed))
    print(executed)
