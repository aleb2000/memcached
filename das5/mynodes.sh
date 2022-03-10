#!/bin/bash

## List all nodes currently reserved by the current user, separated by a single space
##
## Usage: ./mynodes.sh
##

# Make sure prun is loaded
module load prun

# Get node number of all nodes reserved by the current user
nodes=$(preserve -llist \
    | sed -nr "/^[0-9]+\s$(whoami)/s/.*R\s+[0-9]+\s((:?node[0-9]+\s*)+)$/\1/p" \
    | sed "s/node0*//g")

# Change all whitespaces (including tabs, newlines, ecc...) to single space
echo $nodes | sed "s/\s+/ /g"