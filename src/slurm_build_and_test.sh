#!/bin/bash
##Resource Request
#SBATCH --account=def-regrant
#SBATCH --job-name build_and_test
#SBATCH --output %x-%j.out
#SBATCH --ntasks=1
#SBATCH --mem=32G
#SBATCH --time=0-01:00:00

make
gcc -g -c ./src/rvma_buffer_queue_test.c
gcc -g -c ./src/rvma_mailbox_hashmap_test.c
gcc -g -c ./src/rvma_write_test.c
gcc -o test.exe src/test_main.c rvma*.o -Wall
./test