#ifndef G15NETSPEED_NETWORK_H
#define G15NETSPEED_NETWORK_H

#include <stddef.h>
#include <stdio.h>

#define G15_IFACE_NAME_SIZE 128
#define G15_MAX_INTERFACES 32

typedef struct {
    unsigned long long rx_bytes;
    unsigned long long tx_bytes;
} G15NetworkCounters;

int g15_network_read_stream(FILE *stream, const char *interface,
                            G15NetworkCounters *counters);
int g15_network_read(const char *interface, G15NetworkCounters *counters);
size_t g15_network_scan_stream(FILE *stream,
                               char interfaces[][G15_IFACE_NAME_SIZE],
                               size_t capacity, int include_loopback);
size_t g15_network_scan(char interfaces[][G15_IFACE_NAME_SIZE],
                        size_t capacity, int include_loopback);
int g15_network_detect_default(char *interface, size_t length);
int g15_network_print_interfaces(FILE *output);

#endif
