#ifndef COINS_REGISTRY_H
#define COINS_REGISTRY_H

typedef enum {
    COIN_BTC,
    COIN_LTC,
    COIN_DOGE,
    COIN_UNKNOWN
} coin_type;

const char *coin_type_to_name(coin_type t);
coin_type coin_type_from_name(const char *name);

#endif
