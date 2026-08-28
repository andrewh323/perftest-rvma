#!/bin/bash
#SBATCH --job-name=rsocket_stream_bw
#SBATCH --exclusive
#SBATCH --account=def-regrant
#SBATCH --output=results/%x-%j.out
#SBATCH --nodes=2
#SBATCH --mem=16G
#SBATCH --time=1:00:00

export RDMA_CORE_LIB="$HOME/src/rdma-core/build/lib"
PATH_TO_BIN="/home/andrewh8/src/perftest-rvma"
CSV_FILE="$PATH_TO_BIN/results/csv_tables/rsocket_stream_bw2.csv"
REPEATS=10

# Create results directory if needed
mkdir -p "$PATH_TO_BIN/results/temp"

# Write CSV header once
if [ ! -f "$CSV_FILE" ]; then
    echo "timestamp,repetition,bandwidth,stddev,msg_size" > "$CSV_FILE"
fi

# get the list of nodes
nodes=($(scontrol show hostnames $SLURM_JOB_NODELIST))
server=${nodes[0]}
client=${nodes[1]}
echo "Server Node: $server"
echo "Client Node: $client"

# getting the InfiniBand IP of the server
SERVER_IP=$(ssh $server "ifconfig ib0 | grep 'inet ' | awk '{print \$2}'" | tail -n 1)
CLIENT_IP=$(ssh $client "ifconfig ib0 | grep 'inet ' | awk '{print \$2}'" | tail -n 1)
echo "Server IB HW IP: $SERVER_IP"
echo "Client IB HW IP: $CLIENT_IP"

SERVER_OUT_PATH="$PATH_TO_BIN/results/temp/server-bw-$SLURM_JOB_ID.out"
CLIENT_OUT_PATH="$PATH_TO_BIN/results/temp/client-bw-$SLURM_JOB_ID.out"

SERVER_EXEC="$PATH_TO_BIN/rsocket_server_bw"
CLIENT_EXEC="$PATH_TO_BIN/rsocket_client_bw"

declare -a SIZES=(1024 2048 4096 8192 16384 32768 65536 131072 262144 524288 1048576) # 1KB to 1MB


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
        pkill -9 rsocket_server_bw 2>/dev/null
        
        # Extract times from client output
        BANDWIDTH=$(grep "Average bandwidth: " "$CLIENT_OUT_PATH" | awk '{print $(NF-1)}')
        STDDEV=$(grep "Standard deviation: " "$CLIENT_OUT_PATH" | awk '{print $(NF-1)}')
        # Append to CSV with repetition
        echo "$(date +"%H:%M:%S.%3N"),$REP,$BANDWIDTH,$STDDEV,$SIZE" >> "$CSV_FILE"
    done
    # Add empty line between repetitions
    echo "" >> "$CSV_FILE"
done