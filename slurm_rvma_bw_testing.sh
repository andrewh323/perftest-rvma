#!/bin/bash
#SBATCH --job-name=rvma_bw_testing
#SBATCH --output=%x-%j.out
#SBATCH --mail-type=ALL
#SBATCH --mail-user=18srh5@queensu.ca
#SBATCH --nodes=2
#SBATCH --mem=16G
#SBATCH --switches=1
#SBATCH --time=00:30:00

# Package SIZE default 65536, options # for bytes, #K for KB, #M for MB
SIZE=65536
PATH_TO_BIN="."

# get the list of nodes
nodes=($(scontrol show hostnames $SLURM_JOB_NODELIST))

server=${nodes[0]}
client=${nodes[1]}

echo "Server Node: $server"
echo "Client Node: $client"

# getting the InfiniBand IP of the server
server_ip=$(ssh $server "ifconfig ib0 | grep 'inet ' | awk '{print \$2}'")

echo "Server IB HW IP: $server_ip"

echo -e "Testing Beginning\n\n"

SERVER_OUT_PATH="$PATH_TO_BIN/temp/server-%j.out"
CLIENT_OUT_PATH="$PATH_TO_BIN/temp/client-%j.out"

# Run on server
ssh $server "$PATH_TO_BIN/ib_write_bw -s $SIZE &> $SERVER_OUT_PATH" &
server_pid=$!

# Wait for 1 second
sleep 1

# Then run on client
ssh $client "$PATH_TO_BIN/ib_write_bw $server_ip -s $SIZE &> $CLIENT_OUT_PATH"
client_pid=$!

wait $server_pid $client_pid
# Print output from server and client
echo "Server output:"
cat $SERVER_OUT_PATH
echo -e "\n\nClient output:"
cat $CLIENT_OUT_PATH