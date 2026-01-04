#ifndef CLI_H
#define CLI_H

#include "common.h"

int parse_command(int argc, char **argv, cli_result *out);
void print_usage(const char *progname);

#endif
