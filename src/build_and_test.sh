make
gcc -g -c src/rvma_common.c
gcc -g -c src/rvma_test_common.c
gcc -g -c src/rvma_buffer_queue.c
gcc -g -c src/rvma_buffer_queue_test.c
gcc -g -c src/rvma_mailbox_hashmap_test.c
gcc -g -c src/rvma_mailbox_hashmap.c
gcc -g -c src/rvma_write.c
gcc -g -c src/rvma_write_test.c
gcc -o test.exe src/test_main.c rvma*.o -g -Wall
./test.exe