#!/bin/bash

#SBATCH --job-name=rvma_lat
#SBATCH --exclusive
#SBATCH --account=def-regrant
#SBATCH --output=results/%x-%j.out
#SBATCH --nodes=2
#SBATCH --time=1:00:00

REPEATS=10
PATH_TO_BIN="/home/andrewh8/src/perftest-rvma"
CSV_FILE="$PATH_TO_BIN/results/rvma_lat.csv"

# Create results directory if needed
mkdir -p "$PATH_TO_BIN/results/temp"

# Write CSV header once
if [ ! -f "$CSV_FILE" ]; then
    echo "timestamp,repetition,size_bytes,avg rtt,min,max,stddev" > "$CSV_FILE"
fi

# Get nodes
nodes=($(scontrol show hostnames $SLURM_JOB_NODELIST))
server=${nodes[0]}
client=${nodes[1]}
echo "Server Node: $server"
echo "Client Node: $client"

# InfiniBand IPs
SERVER_IP=$(ssh $server "ifconfig ib0 | grep 'inet ' | awk '{print \$2}'" | tail -n 1)
CLIENT_IP=$(ssh $client "ifconfig ib0 | grep 'inet ' | awk '{print \$2}'" | tail -n 1)
echo "Server IB HW IP: $SERVER_IP"
echo "Client IB HW IP: $CLIENT_IP"

SERVER_OUT_PATH="$PATH_TO_BIN/results/temp/rvma_server_lat-$SLURM_JOB_ID.out"
CLIENT_OUT_PATH="$PATH_TO_BIN/results/temp/rvma_client_lat-$SLURM_JOB_ID.out"

SERVER_EXEC="$PATH_TO_BIN/rvma_mailbox_server"
CLIENT_EXEC="$PATH_TO_BIN/rvma_mailbox_client"

declare -a SIZES=(1 4 16 64 256 1024 4096 16384 65536 262144 1048576) # 1B to 1MB

# Repeat the tests
for REP in $(seq 1 $REPEATS); do
    echo "=== Run $REP of $REPEATS ==="
    for SIZE in "${SIZES[@]}"; do
        > "$SERVER_OUT_PATH"
        > "$CLIENT_OUT_PATH"
        echo "Running test with message size: ${SIZE} bytes"
        
        # Run server
        $SERVER_EXEC $SIZE > "$SERVER_OUT_PATH" &
        SERVER_PID=$!
        sleep 1

        # Run client
        $CLIENT_EXEC $SERVER_IP $SIZE > "$CLIENT_OUT_PATH"
        wait $SERVER_PID 2>/dev/null

        sleep 0.5
        
        # Extract times from client output
        AVG_SEND=$(grep "Mean:"   "$CLIENT_OUT_PATH" | awk '{print $(NF-1)}')
        SEND_STDDEV=$(grep "Stddev:"   "$CLIENT_OUT_PATH" | awk '{print $(NF-1)}')
        MIN=$(grep "Min:"   "$CLIENT_OUT_PATH" | awk '{print $(NF-1)}')
        MAX=$(grep "Max:"   "$CLIENT_OUT_PATH" | awk '{print $(NF-1)}')

        # Append to CSV with repetition
        echo "$(date +"%H:%M:%S.%3N"),$REP,$SIZE,$AVG_SEND,$MIN,$MAX,$SEND_STDDEV" >> "$CSV_FILE"
    done
    # Add empty line between repetitions
    echo "" >> "$CSV_FILE"
done

echo "All tests completed. Results saved in $CSV_FILE"
