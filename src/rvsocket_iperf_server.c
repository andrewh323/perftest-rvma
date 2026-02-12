#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <rdma/rsocket.h>
#include <iperf_api.h>
#include <arpa/inet.h>

#include "rvma_socket.h"
#include "rvma_write.h"

#define PORT 7471
#define ROLE 's'

int main(int argc, char** argv) {
    struct iperf_test *test;
    test = iperf_new_test();
    if ( test == NULL ) {
        fprintf( stderr, "%s: failed to create test\n", argv0 );
        exit( EXIT_FAILURE );
    }
    iperf_defaults( test );
    iperf_set_test_role( test, 's' );
    iperf_set_test_server_port( test, port );
    for (;;) {
        if ( iperf_run_server( test ) < 0 )
            fprintf( stderr, "%s: error - %s\n\n", argv0, iperf_strerror( i_errn
o ) );
        iperf_reset_test( test );
    }
    iperf_free_test( test );
}