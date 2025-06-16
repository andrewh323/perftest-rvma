#!/bin/bash
#SBATCH --job-name=rvma_lat_benchmark
#SBATCH --output=results/%x-%j.out
#SBATCH --nodes=2
#SBATCH --mem=16G
#SBATCH --switches=1
#SBATCH --time=1:00:00

if [ ! -d "results" ]; then
    mkdir results
    echo "Created directory: results"
fi
if [ ! -d "results/temp" ]; then
    mkdir -p results/temp
    echo "Created directory: results/temp"
fi

SIZES=(2 4 8 16 32 64)
#SIZES=(128 256 512 1024 2048 4096)
#SIZES=(8192 16384 32768 65536 131072 262144)
#SIZES=(524288 1048576 2097152 4194304)
#SIZES=(2 4 8 16 32 64 128 256 512 1024 2048 4096 8192 16384 32768 65536 131072 262144 524288 1048576 2097152 4194304)
PATH_TO_BIN="/home/andrewh8/src/perftest-rvma"
BASE_PORT=18515
COUNTER=$(printf "%.0f" `echo "sqrt(${SIZES[0]}) - 1" | bc`)

# get the list of nodes
nodes=($(scontrol show hostnames $SLURM_JOB_NODELIST))
server=${nodes[0]}
client=${nodes[1]}
echo "Server Node: $server"
echo "Client Node: $client"

# getting the InfiniBand IP of the server
server_ip=$(ssh $server "ifconfig ib0 | grep 'inet ' | awk '{print \$2}'" | tail -n 1)
echo "Server IB HW IP: $server_ip"

for SIZE in ${SIZES[@]}; do
    for (( i=0; i<10; i++ )); do

        PORT=$((BASE_PORT + COUNTER))
        COUNTER=$((COUNTER+1))

        echo $(date +"%Y-%m-%d %H:%M:%S")
        echo -e "\nTesting Beginning for SIZE=${SIZE} and Trial $((i+1))\n"
        SERVER_OUT_PATH="$PATH_TO_BIN/results/temp/server-${SIZE}-$((i+1))-$SLURM_JOB_ID.out"
        CLIENT_OUT_PATH="$PATH_TO_BIN/results/temp/client-${SIZE}-$((i+1))-$SLURM_JOB_ID.out"
        # Run on server
        ssh $server "timeout 480 $PATH_TO_BIN/ib_write_lat -s $SIZE -p $PORT &> $SERVER_OUT_PATH" &
        server_pid=$!
        # Wait for 1 second
        sleep 1
        # Then run on client
        status=$(ssh $client "timeout 480 $PATH_TO_BIN/ib_write_lat $server_ip -s $SIZE -p $PORT &> $CLIENT_OUT_PATH; echo $?" )
        client_pid=$!
        wait $server_pid $client_pid

        # Check if command has finished within timeout and succeeded
        if [ $status -ne 0 ]; then
            echo "Test Failed for SIZE=${SIZE}, Trial $((i+1)). Check the output files for details."
            # Print output from server and client
            echo "Server output:"
            cat $SERVER_OUT_PATH
            echo -e "\nClient output:"
            cat $CLIENT_OUT_PATH
            continue
        else
            # Print output only when the test was successful
            echo "Test Successful for SIZE=${SIZE}, Trial $((i+1))."
            echo "Server output:"
            cat $SERVER_OUT_PATH
            echo -e "\nClient output:"
            cat $CLIENT_OUT_PATH
        fi
    done
done