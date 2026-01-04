#ifndef WALLET_H
#define WALLET_H

#include "common.h"

int load_wallet(const wallet_options *opts, wallet_info *info);
int save_wallet(const wallet_options *opts, const wallet_info *info);
int ensure_wallet(const wallet_options *opts, wallet_info *info, int reset);
void print_wallet(const wallet_info *info);
void generate_address(char out[65]);

#endif
