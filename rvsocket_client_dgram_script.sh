#!/bin/bash

SERVER=$1
SIZE=$2
NUM_CLIENTS=$3

# Bash ignores SIGINT/SIGTERM for background (`&`) jobs in a non-interactive
# script, so Ctrl+C here would otherwise only stop this script's `wait` and
# leave every client process running. Track their PIDs and forward the
# signal explicitly so Ctrl+C actually stops all of them.
pids=()
trap 'kill "${pids[@]}" 2>/dev/null' INT TERM

for ((i=0; i<NUM_CLIENTS; i++)); do
    ./rvsocket_client_dgram "$SERVER" "$SIZE" "$i" &
    pids+=($!)
done

wait
