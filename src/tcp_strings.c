#include <sys/socket.h>
#include <netinet/tcp.h>
#include <stddef.h>
#include "tcp_strings.h"

typedef struct {
    int         value;
    const char *name;
} sockopt_name_t;

static const sockopt_name_t sol_socket_opts[] = {
    { SO_DEBUG,       "SO_DEBUG"       },
    { SO_REUSEADDR,   "SO_REUSEADDR"   },
    { SO_TYPE,        "SO_TYPE"        },
    { SO_ERROR,       "SO_ERROR"       },
    { SO_SNDBUF,      "SO_SNDBUF"      },
    { SO_RCVBUF,      "SO_RCVBUF"      },
    { SO_KEEPALIVE,   "SO_KEEPALIVE"   },
    { SO_LINGER,      "SO_LINGER"      },
    { SO_REUSEPORT,   "SO_REUSEPORT"   },
    { SO_SNDBUFFORCE, "SO_SNDBUFFORCE" },
    { SO_RCVBUFFORCE, "SO_RCVBUFFORCE" }
};

static const sockopt_name_t tcp_opts[] = {
    { TCP_NODELAY,      "TCP_NODELAY"      },
    { TCP_MAXSEG,       "TCP_MAXSEG"       },
    { TCP_KEEPIDLE,     "TCP_KEEPIDLE"     },
    { TCP_KEEPINTVL,    "TCP_KEEPINTVL"    },
    { TCP_KEEPCNT,      "TCP_KEEPCNT"      },
    { TCP_CONGESTION,   "TCP_CONGESTION"   },
    { TCP_USER_TIMEOUT, "TCP_USER_TIMEOUT" }
};

const char *sockopt_to_str(int level, int optname) {
    const sockopt_name_t *table = (level == SOL_SOCKET) ? sol_socket_opts : tcp_opts;
    for (int i = 0; table[i].name != NULL; i++)
        if (table[i].value == optname)
            return table[i].name;
    return "UNKNOWN";
}

int str_to_sockopt(int level, const char *name) {
    const sockopt_name_t *table = (level == SOL_SOCKET) ? sol_socket_opts : tcp_opts;
    for (int i = 0; table[i].name != NULL; i++)
        if (strcmp(table[i].name, name) == 0)
            return table[i].value;
    return -1;
}
