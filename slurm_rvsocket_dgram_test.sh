#!/bin/bash

#SBATCH --job-name=rvsocket_dgram_test
#SBATCH --exclusive
#SBATCH --account=rrg-regrant
#SBATCH --output=results/%x-%j.out
#SBATCH --nodes=2
#SBATCH --mem=16G
#SBATCH --time=1:00:00

REPEATS=10
PATH_TO_BIN="/home/andrewh8/src/perftest-rvma"
CSV_FILE="$PATH_TO_BIN/results/rvsocket_dgram_exclude_warmup.csv"

# Create results directory if needed
mkdir -p "$PATH_TO_BIN/results/temp"

# Write CSV header once
if [ ! -f "$CSV_FILE" ]; then
    echo "timestamp,repetition,size_bytes,min_send,max_send,avg_send,std_dev_send,avg_buffer_setup,avg_frag_setup,avg_wr_setup,avg_poll,window_init,rvsocket_setup,postRecvPool,min_recv,max_recv,avg_recv,std_dev_recv,rvbind" > "$CSV_FILE"
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

SERVER_OUT_PATH="$PATH_TO_BIN/results/temp/server-dgram-$SLURM_JOB_ID.out"
CLIENT_OUT_PATH="$PATH_TO_BIN/results/temp/client-dgram-$SLURM_JOB_ID.out"

SERVER_EXEC="$PATH_TO_BIN/rvsocket_server_dgram"
CLIENT_EXEC="$PATH_TO_BIN/rvsocket_client_dgram"

declare -a SIZES=(1 4 16 64 256 1024 4050 16384 65536 262144 1048576) # 1B to 1MB

# Repeat the tests
for REP in $(seq 1 $REPEATS); do
    echo "=== Run $REP of $REPEATS ==="
    for SIZE in "${SIZES[@]}"; do
        > "$SERVER_OUT_PATH"
        > "$CLIENT_OUT_PATH"
        echo "Running test with message size: ${SIZE} bytes"
        
        # Run server
        $SERVER_EXEC > "$SERVER_OUT_PATH" &
        SERVER_PID=$!
        # Wait on server to setup
        sleep 0.2

        # Run client
        $CLIENT_EXEC $SERVER_IP $SIZE > "$CLIENT_OUT_PATH"
        wait $SERVER_PID 2>/dev/null

        # Extract times from client output
        MIN_SEND=$(grep "^Min send time:"   "$CLIENT_OUT_PATH" | awk '{print $(NF-1)}')
        MAX_SEND=$(grep "^Max send time:"   "$CLIENT_OUT_PATH" | awk '{print $(NF-1)}')
        AVG_SEND=$(grep "^Avg send time:"   "$CLIENT_OUT_PATH" | awk '{print $(NF-1)}')
        STD_DEV_SEND=$(grep "Send time stddev:" "$CLIENT_OUT_PATH" | awk '{print $(NF-1)}')
        BUFF_TIME=$(grep "Average buffer setup:" "$CLIENT_OUT_PATH" | awk '{print $(NF-1)}')
        FRAGMENT_SETUP=$(grep "Average frag setup:" "$CLIENT_OUT_PATH" | awk '{print $(NF-1)}')
        WR_TIME=$(grep "Average WR setup:" "$CLIENT_OUT_PATH" | awk '{print $(NF-1)}')
        POLL_TIME=$(grep "Average poll time:" "$CLIENT_OUT_PATH" | awk '{print $(NF-1)}')
        WINDOW_INIT=$(grep "Window init setup time:" "$CLIENT_OUT_PATH" | awk '{print $(NF-1)}')
        RVSOCKET_SETUP=$(grep "rvsocket total setup time:" "$CLIENT_OUT_PATH" | awk '{print $(NF-1)}')
        # Extract times from server output
        MIN_RECV=$(grep "^Min recv time:"   "$SERVER_OUT_PATH" | awk '{print $(NF-1)}')
        MAX_RECV=$(grep "^Max recv time:"   "$SERVER_OUT_PATH" | awk '{print $(NF-1)}')
        AVG_RECV=$(grep "Avg recv time:" "$SERVER_OUT_PATH" | awk '{print $(NF-1)}')
        STD_DEV_RECV=$(grep "Recv time stddev:" "$SERVER_OUT_PATH" | awk '{print $(NF-1)}')
        RVBIND=$(grep "rvbind total time:" "$SERVER_OUT_PATH" | awk '{print $(NF-1)}')
        POSTRECVPOOL=$(grep "postRecvPool time" "$SERVER_OUT_PATH" | awk '{print $(NF-1)}')
        
        
        # Append to CSV with repetition
        echo "$(date +"%H:%M:%S.%3N"),$REP,$SIZE,$MIN_SEND,$MAX_SEND,$AVG_SEND,$STD_DEV_SEND,$BUFF_TIME,$FRAGMENT_SETUP,$WR_TIME,$POLL_TIME,$WINDOW_INIT,$RVSOCKET_SETUP,$POSTRECVPOOL,$MIN_RECV,$MAX_RECV,$AVG_RECV,$STD_DEV_RECV,$RVBIND" >> "$CSV_FILE"
    done
    # Add empty line between repetitions
    echo "" >> "$CSV_FILE"
done

echo "All tests completed. Results saved in $CSV_FILE"
