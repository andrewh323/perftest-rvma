#!/bin/bash

SERVER=$1
NUM_CLIENTS=$2
SIZE=1024

for ((i=0; i<NUM_CLIENTS; i++)); do
    echo "Starting client $i"
    ./rvsocket_client_stream "$SERVER" "$SIZE" "$i" &
done

wait