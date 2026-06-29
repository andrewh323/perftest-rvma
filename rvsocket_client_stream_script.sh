#!/bin/bash

SERVER=$1
SIZE=1024
NUM_CLIENTS=$3

for ((i=0; i<NUM_CLIENTS; i++)); do
    ./rvsocket_client_stream "$SERVER" "$SIZE" "$i" &
done

wait