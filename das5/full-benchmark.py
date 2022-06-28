import sys
import subprocess
import time
import re
from enum import Enum
import csv

# Important notes:
#   - This program must be run from the rdma memcached root directory
#   - You might want to adjust the BENCH_BIN_PATH variable if your path is different

BENCH_BIN_PATH = "memcached/mcrdma_bench/mcrdma_bench"

CLIENTS_RANGE = [1, 2, 4, 8, 16, 32]
MESSAGE_SIZES = [64, 256, 1024, 4096, 16384, 65536, 262144, 1000000]
ITERS = 10000

memcached_proc = None
bench_results = [["NET", "CLIENTS", "ITERS", "MESSAGE SIZE", "TEST", "RESULT", "MIN TIMES", "MAX TIMES"]]

class NetType(Enum):
    TCP = 0,
    RDMA = 1

class Test(Enum):
    SET_GET = 0,
    PING_PONG = 1,
    MESSAGE_SIZE = 2

def node_number_to_name(n):
    fmt = "node0{}"
    if len(n) == 1:
        fmt = "node00{}".format(n)
    return fmt.format(n)

# Obtain two DAS nodes

process = subprocess.Popen(['das5/mynodes.sh'],
                    stdout = subprocess.PIPE)

stdout, stderr = process.communicate()

ssh_nodes = stdout.split()

ssh_nodes = list(map(lambda x: x.decode("UTF-8"), ssh_nodes))

if len(ssh_nodes) != 2:
    print("2 DAS nodes need to be ready")
    print("Current nodes: {}".format(ssh_nodes))
    sys.exit()

memcached_n = ssh_nodes[0]
memcached_node = node_number_to_name(memcached_n)
memcached_ip = "10.149.0.{}".format(memcached_n)

bench_n = ssh_nodes[1]
bench_node = node_number_to_name(bench_n)

def start_memcached_instance(net_type):
    global memcached_proc
    assert memcached_proc == None

    net_args = "" if net_type == NetType.TCP else "--rdma"
    memcached_proc = subprocess.Popen('ssh -t {} "memcached {} -l {}"'.format(memcached_node, net_args, memcached_ip),
                        shell = True,
                        stdout = subprocess.PIPE,
                        stderr = subprocess.PIPE)


def stop_memcached_instance():
    global memcached_proc
    assert memcached_proc != None
    memcached_proc.terminate()
    memcached_proc = None

def scalability_bench(net_type, test, clients, iters):
    start_memcached_instance(net_type)

    # Wait 1 second to make sure the server is ready
    time.sleep(1)

    net_arg = "--tcp" if net_type == NetType.TCP else "--rdma"
    test_arg = "--set-get" if test == Test.SET_GET else "--ping-pong"

    msg_size = 24

    bench_proc = subprocess.Popen('ssh -t {} "{} {} {} {} 11211 {} {} {}"'.format(bench_node, BENCH_BIN_PATH, net_arg, test_arg, memcached_ip, clients, iters, msg_size),
                        shell = True,
                        stdout = subprocess.PIPE,
                        stderr = subprocess.PIPE)


    stdout, stderr = bench_proc.communicate()
    stdout = stdout.decode("UTF-8")

    net_id = "tcp" if net_type == NetType.TCP else "rdma"
    try:
        if test == Test.SET_GET:
            # Get min times
            min_set_matches = re.findall(r'\[\d+\] min time set: \d+', stdout)
            min_set = list(map(lambda x: re.search(r'\d+$', x).group(), min_set_matches))
            min_get_matches = re.findall(r'\[\d+\] min time get: \d+', stdout)
            min_get = list(map(lambda x: re.search(r'\d+$', x).group(), min_get_matches))

            # Get max times
            max_set_matches = re.findall(r'\[\d+\] max time set: \d+', stdout)
            max_set = list(map(lambda x: re.search(r'\d+$', x).group(), max_set_matches))
            max_get_matches = re.findall(r'\[\d+\] max time get: \d+', stdout)
            max_get = list(map(lambda x: re.search(r'\d+$', x).group(), max_get_matches))

            # Get final mean times
            set_match = re.search(r'Final mean time set: \d+', stdout).group()
            set_mean = re.search(r'\d+', set_match).group()
            get_match = re.search(r'Final mean time get: \d+', stdout).group()
            get_mean = re.search(r'\d+', get_match).group()

            bench_results.append([net_id, clients, iters, msg_size, "set", set_mean, " ".join(min_set), " ".join(max_set)])
            bench_results.append([net_id, clients, iters, msg_size, "get", get_mean, " ".join(min_get), " ".join(max_get)])
        else:
            # Get min times
            min_ping_matches = re.findall(r'\[\d+\] min time ping: \d+', stdout)
            min_ping = list(map(lambda x: re.search(r'\d+$', x).group(), min_ping_matches))

            # Get max times
            max_ping_matches = re.findall(r'\[\d+\] max time ping: \d+', stdout)
            max_ping = list(map(lambda x: re.search(r'\d+$', x).group(), max_ping_matches))

            # Get final mean times
            ping_match = re.search(r'Final mean time ping: \d+', stdout).group()
            ping_mean = re.search(r'\d+', ping_match).group()

            bench_results.append([net_id, clients, iters, 0, "ping", ping_mean, " ".join(min_ping), " ".join(max_ping)])
    except Exception as e:
        print(e)
        print("Exception catched, benchmark output:")
        print(stdout)
        stop_memcached_instance()
        sys.exit()

    stop_memcached_instance()

def message_size_bench(net_type, iters, msg_size):
    start_memcached_instance(net_type)

    # Wait 1 second to make sure the server is ready
    time.sleep(1)

    net_arg = "--tcp" if net_type == NetType.TCP else "--rdma"

    bench_proc = subprocess.Popen('ssh -t {} "{} {} --set-get {} 11211 1 {} {}"'.format(bench_node, BENCH_BIN_PATH, net_arg, memcached_ip, iters, msg_size),
                        shell = True,
                        stdout = subprocess.PIPE,
                        stderr = subprocess.PIPE)


    stdout, stderr = bench_proc.communicate()
    stdout = stdout.decode("UTF-8")

    net_id = "tcp" if net_type == NetType.TCP else "rdma"
    try:
        # Get min times
        min_set_matches = re.findall(r'\[\d+\] min time set: \d+', stdout)
        min_set = list(map(lambda x: re.search(r'\d+$', x).group(), min_set_matches))
        min_get_matches = re.findall(r'\[\d+\] min time get: \d+', stdout)
        min_get = list(map(lambda x: re.search(r'\d+$', x).group(), min_get_matches))

        # Get max times
        max_set_matches = re.findall(r'\[\d+\] max time set: \d+', stdout)
        max_set = list(map(lambda x: re.search(r'\d+$', x).group(), max_set_matches))
        max_get_matches = re.findall(r'\[\d+\] max time get: \d+', stdout)
        max_get = list(map(lambda x: re.search(r'\d+$', x).group(), max_get_matches))

        # Get final mean times
        set_match = re.search(r'Final mean time set: \d+', stdout).group()
        set_mean = re.search(r'\d+', set_match).group()
        get_match = re.search(r'Final mean time get: \d+', stdout).group()
        get_mean = re.search(r'\d+', get_match).group()

        bench_results.append([net_id, 1, iters, msg_size, "msg_size_set", set_mean, " ".join(min_set), " ".join(max_set)])
        bench_results.append([net_id, 1, iters, msg_size, "msg_size_get", get_mean, " ".join(min_get), " ".join(max_get)])
    except Exception as e:
        print(e)
        print("Exception catched, benchmark output:")
        print(stdout)
        stop_memcached_instance()
        sys.exit()
    
    stop_memcached_instance()

def run_bench(net_type, test):
    if test == Test.SET_GET or test == Test.PING_PONG:
        for clients in CLIENTS_RANGE:
            time.sleep(1)
            print("Benchmarking with {} clients.".format(clients))
            scalability_bench(net_type, test, clients, ITERS)
    elif test == Test.MESSAGE_SIZE:
        for size in MESSAGE_SIZES:
            time.sleep(1)
            print("Benchmarking with msg size {}.".format(size))
            message_size_bench(net_type, ITERS, size)

# RDMA tests
print("Running RDMA scalability benchmark SET_GET")
run_bench(NetType.RDMA, Test.SET_GET)
print("Running RDMA scalability benchmark PING_PONG")
run_bench(NetType.RDMA, Test.PING_PONG)
print("Running RDMA message size benchmark")
run_bench(NetType.RDMA, Test.MESSAGE_SIZE)

# TCP tests
print("Running TCP scalability benchmark SET_GET")
run_bench(NetType.TCP, Test.SET_GET)
print("Running TCP scalability benchmark PING_PONG")
run_bench(NetType.TCP, Test.PING_PONG)
print("Running TCP message size benchmark")
run_bench(NetType.TCP, Test.MESSAGE_SIZE)

with open("bench_results.csv", "w", newline="") as file:
    writer = csv.writer(file)
    writer.writerows(bench_results)