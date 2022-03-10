#!/bin/bash

## Start a reservation with 1 node for 900 seconds and print the node number
## 
## Usage: ./newnode.sh
##

# Make sure prun is loaded
module load prun

# Start a reservation and get the reservation number
resnum=$(preserve -np 1 900 | sed -nr "s/Reservation number ([0-9]+):/\1/p")

if [ -z $resnum ]
then
    # We didn't get a reservation number, the command probably didn't succeed, exit with error
    exit 1
fi

# Sleep some time so that we are sure the reserved nodes should be ready by now
sleep 1

# Find out node number form the reservation number
preserve -llist \
    | sed -nr "/^$resnum/s/.*((:?node[0-9]+\s*)+)$/\1/p" \
    | sed "s/node0*//"