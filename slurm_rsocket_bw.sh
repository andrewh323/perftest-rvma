#!/bin/bash
#SBATCH --job-name=rsocket_bw_test
#SBATCH --output=results/%x-%j.out\
#SBATCH --nodes=2
#SBATCH --mem=16G
#SBATCH --switches=1
#SBATCH --time=1:00:00

export RDMA_CORE_LIB="$HOME/rdma-core/build/lib"

if [ ! -d "results" ]; then
    mkdir results
    echo "Created directory: results"
fi
if [ ! -d "results/temp" ]; then
    mkdir -p results/temp
    echo "Created directory: results/temp"
fi

PATH_TO_BIN="/home/rysilve/perftest-rvma"

# get the list of nodes
nodes=($(scontrol show hostnames $SLURM_JOB_NODELIST))
server=${nodes[0]}
client=${nodes[1]}
echo "Server Node: $server"
echo "Client Node: $client"

# getting the InfiniBand IP of the server
server_ip=$(ssh $server "ifconfig ib0 | grep 'inet ' | awk '{print \$2}'" | tail -n 1)
echo "Server IB HW IP: $server_ip"

SERVER_OUT_PATH="$PATH_TO_BIN/results/temp/server-bw-$SLURM_JOB_ID.out"
CLIENT_OUT_PATH="$PATH_TO_BIN/results/temp/client-bw-$SLURM_JOB_ID.out"

echo $(date +"%Y-%m-%d %H:%M:%S")

# Run server
ssh $server "export LD_LIBRARY_PATH=$RDMA_CORE_LIB:\$LD_LIBRARY_PATH; timeout 480 $PATH_TO_BIN/rsocket_server_bw &> $SERVER_OUT_PATH" &
server_pid=$!

# Wait for 1 second
sleep 1

# Run client
status=$(ssh $client "export LD_LIBRARY_PATH=$RDMA_CORE_LIB:\$LD_LIBRARY_PATH; timeout 480 $PATH_TO_BIN/rsocket_client_bw $server_ip &> $CLIENT_OUT_PATH; echo \$?" | tail -n 1)
client_pid=$!
wait $server_pid $client_pid

if [ $status -ne 0 ]; then
    echo "Test Failed. Check the output files for details."
    # Print output from server and client
    echo "Server output:"
    cat $SERVER_OUT_PATH
    echo -e "\nClient output:"
    cat $CLIENT_OUT_PATH
    continue
else
    # Print output only when the test was successful
    echo "Test Successful."
    echo "Server output:"
    cat $SERVER_OUT_PATH
    echo -e "\nClient output:"
    cat $CLIENT_OUT_PATH
fi