#include "g15netspeed/network.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

static int skip_headers(FILE *stream, char *line, size_t length) {
    return fgets(line, (int)length, stream) != NULL &&
           fgets(line, (int)length, stream) != NULL;
}

static int parse_counters(char *text, unsigned long long values[16]) {
    size_t i;

    for (i = 0; i < 16; i++) {
        char *end;

        errno = 0;
        values[i] = strtoull(text, &end, 10);
        if (errno != 0 || end == text)
            return -1;
        text = end;
    }
    return 0;
}

int g15_network_read_stream(FILE *stream, const char *interface,
                            G15NetworkCounters *counters) {
    char line[512];

    if (!skip_headers(stream, line, sizeof(line)))
        return -1;

    while (fgets(line, sizeof(line), stream)) {
        char name[G15_IFACE_NAME_SIZE];
        unsigned long long values[16];
        char *colon = strchr(line, ':');
        char *begin = line;
        size_t name_length;

        if (!colon)
            continue;
        while (*begin == ' ' || *begin == '\t')
            begin++;
        name_length = (size_t)(colon - begin);
        if (name_length == 0 || name_length >= sizeof(name))
            continue;
        memcpy(name, begin, name_length);
        name[name_length] = '\0';

        if (strcmp(name, interface) == 0 &&
            parse_counters(colon + 1, values) == 0) {
            counters->rx_bytes = values[0];
            counters->tx_bytes = values[8];
            return 0;
        }
    }
    return -1;
}

int g15_network_read(const char *interface, G15NetworkCounters *counters) {
    FILE *stream = fopen("/proc/net/dev", "r");
    int result;

    if (!stream)
        return -1;
    result = g15_network_read_stream(stream, interface, counters);
    fclose(stream);
    return result;
}

size_t g15_network_scan_stream(FILE *stream,
                               char interfaces[][G15_IFACE_NAME_SIZE],
                               size_t capacity, int include_loopback) {
    char line[512];
    size_t count = 0;

    if (!skip_headers(stream, line, sizeof(line)))
        return 0;

    while (count < capacity && fgets(line, sizeof(line), stream)) {
        char name[G15_IFACE_NAME_SIZE];

        if (sscanf(line, " %127[^:]:", name) == 1 &&
            (include_loopback || strcmp(name, "lo") != 0)) {
            snprintf(interfaces[count], G15_IFACE_NAME_SIZE, "%s", name);
            count++;
        }
    }
    return count;
}

size_t g15_network_scan(char interfaces[][G15_IFACE_NAME_SIZE],
                        size_t capacity, int include_loopback) {
    FILE *stream = fopen("/proc/net/dev", "r");
    size_t count;

    if (!stream)
        return 0;
    count = g15_network_scan_stream(stream, interfaces, capacity, include_loopback);
    fclose(stream);
    return count;
}

int g15_network_detect_default(char *interface, size_t length) {
    FILE *stream = fopen("/proc/net/route", "r");
    char line[256];

    if (stream) {
        if (fgets(line, sizeof(line), stream)) {
            while (fgets(line, sizeof(line), stream)) {
                char name[G15_IFACE_NAME_SIZE];
                char destination_text[32], gateway_text[32], flags_text[32];
                char *end;
                unsigned long destination, flags;

                if (sscanf(line, "%127s %31s %31s %31s",
                           name, destination_text, gateway_text, flags_text) == 4) {
                    (void)gateway_text;
                    errno = 0;
                    destination = strtoul(destination_text, &end, 16);
                    if (errno != 0 || *end != '\0')
                        continue;
                    flags = strtoul(flags_text, &end, 16);
                    if (errno != 0 || *end != '\0')
                        continue;
                } else {
                    continue;
                }

                if (destination == 0 && (flags & 0x1UL) != 0 &&
                    strcmp(name, "lo") != 0) {
                    snprintf(interface, length, "%s", name);
                    fclose(stream);
                    return 0;
                }
            }
        }
        fclose(stream);
    }

    {
        char interfaces[G15_MAX_INTERFACES][G15_IFACE_NAME_SIZE];
        size_t count = g15_network_scan(interfaces, G15_MAX_INTERFACES, 0);
        size_t i;

        for (i = 0; i < count; i++) {
            char path[256], state[32];
            FILE *state_stream;

            snprintf(path, sizeof(path), "/sys/class/net/%s/operstate", interfaces[i]);
            state_stream = fopen(path, "r");
            if (state_stream && fgets(state, sizeof(state), state_stream) &&
                (strncmp(state, "up", 2) == 0 ||
                 strncmp(state, "unknown", 7) == 0)) {
                snprintf(interface, length, "%s", interfaces[i]);
                fclose(state_stream);
                return 0;
            }
            if (state_stream)
                fclose(state_stream);
        }
    }
    return -1;
}

int g15_network_print_interfaces(FILE *output) {
    char interfaces[G15_MAX_INTERFACES][G15_IFACE_NAME_SIZE];
    size_t count = g15_network_scan(interfaces, G15_MAX_INTERFACES, 1);
    size_t i;

    if (count == 0)
        return -1;
    for (i = 0; i < count; i++)
        fprintf(output, "%s\n", interfaces[i]);
    return 0;
}
