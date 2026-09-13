#ifndef G15NETSPEED_CONFIG_H
#define G15NETSPEED_CONFIG_H

#include <stddef.h>
#include <stdio.h>

#include "g15netspeed/network.h"

#define G15_DEFAULT_REFRESH_MS 150
#define G15_MIN_REFRESH_MS 50
#define G15_MAX_REFRESH_MS 5000
#define G15_DEFAULT_RECONNECT_MS 5000
#define G15_PATH_SIZE 4096

typedef struct {
    char interface[G15_IFACE_NAME_SIZE];
    int automatic_interface;
    int refresh_ms;
    int reconnect_ms;
    int verbose;
    int adaptive_scale;
} G15Config;

typedef enum {
    G15_ACTION_RUN,
    G15_ACTION_HELP,
    G15_ACTION_VERSION,
    G15_ACTION_LIST_INTERFACES
} G15Action;

void g15_config_defaults(G15Config *config);
int g15_config_load_file(const char *path, G15Config *config,
                         char *error, size_t error_length);
int g15_config_prepare(int argc, char **argv, G15Config *config,
                       G15Action *action, char *error, size_t error_length);
void g15_config_print_usage(FILE *stream, const char *program);

#endif
