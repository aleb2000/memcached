#!/bin/bash

# Make sure prun is loaded
module load prun

# Check if we have reserved nodes
nodes=$(./`dirname $0`/mynodes.sh)

if [ -z "$nodes" ]
then
    echo "No nodes reserved, reserving a new node"
    node=$(./`dirname $0`/newnode.sh)
    if [ -z node ]
    then
        echo "ERROR: could not reserve a new node"
        exit 1
    fi
else
    echo "Found nodes already reserved"
    # Get the first node available
    node=$(echo $nodes | sed -r "s/^([0-9]+)+.*$/\1/")
fi

# Make sure to add the correct number of zeroes to the nodename
if [ "${#node}" -eq 1 ];
then
    nodename="node00$node"
else
    nodename="node0$node"
fi

echo "Using node $nodename"

# Run memcached server on the node with the following arguments
MEMCACHED_ARGS="--rdma -l 10.149.0.$node -v"
ssh -t $nodename "memcached $MEMCACHED_ARGS"
