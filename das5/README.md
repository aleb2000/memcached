# DAS-5 related scripts

In order to use these scripts, you might have to run
```
module load prun
```

## mynodes.sh
This script will list the number of reserved nodes by you on the DAS-5 that are ready for use.
It is a useful utility to write more complex scripts.

## newnode.sh
This script will attempt to reserve a node on the DAS-5 and then output the node number, if successful.
It is a useful utility to write more complex scripts.

## run_server.sh
This script is useful for quickly starting up a memcached instance on a DAS-5 node.
It will first scan for reserved nodes, if one or more are found it will use the first one on the list.
If no nodes are reserved, it will attempt to reserve a new node.
On success it will output the node it is using and then start a memcached instance on that node using ssh.
You can modify the script to change the arguments to pass to memcached.

## full-benchmark.py
This python script is used to run a series of benchmarks using mcrdma_bench.
YOU NEED to compile both memcached as well as mcrdma_bench before running this script, or there will be no executable to run.
YOU NEED to reserve two nodes prior to calling this script, failure to do so will result in an error message.
YOU NEED to start this script from the root directory of the repo, it makes things a lot simpler.
The output of the benchmark will be written to a file called bench_results.csv in the root of the repo.

Hint:
You can reserve two nodes with the following command.
```
preserve -np 2 900
```
Where 900 is the number of seconds of the reservation, change as you see fit and follow the DAS-5 policies.