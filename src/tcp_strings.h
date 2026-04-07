#ifndef TCP_STRINGS_H
#define TCP_STRINGS_H

#include <string.h>

const char *sockopt_to_str(int level, int optname);
int str_to_sockopt(int leve, const char *name);

#endif // !TCP_STRINGS_H
#define TCP_STRINGS_H
