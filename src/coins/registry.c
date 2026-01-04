#include "registry.h"

#include <strings.h>

const char *coin_type_to_name(coin_type t) {
    switch (t) {
        case COIN_BTC: return "bitcoin";
        case COIN_LTC: return "litecoin";
        case COIN_DOGE: return "dogecoin";
        default: return "unknown";
    }
}

coin_type coin_type_from_name(const char *name) {
    if (!name) return COIN_UNKNOWN;
    if (strcasecmp(name, "bitcoin") == 0 || strcasecmp(name, "btc") == 0) return COIN_BTC;
    if (strcasecmp(name, "litecoin") == 0 || strcasecmp(name, "ltc") == 0) return COIN_LTC;
    if (strcasecmp(name, "dogecoin") == 0 || strcasecmp(name, "doge") == 0) return COIN_DOGE;
    return COIN_UNKNOWN;
}
