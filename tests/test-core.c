#include "g15netspeed/config.h"
#include "g15netspeed/history.h"
#include "g15netspeed/metrics.h"
#include "g15netspeed/network.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "Fehlgeschlagen: %s (%s:%d)\n", \
                #condition, __FILE__, __LINE__); \
        return 1; \
    } \
} while (0)

static FILE *fixture(const char *contents) {
    FILE *stream = tmpfile();
    if (!stream)
        return NULL;
    fputs(contents, stream);
    rewind(stream);
    return stream;
}

static int test_history(void) {
    G15History history;
    size_t i;

    g15_history_init(&history);
    for (i = 0; i < G15_HISTORY_CAPACITY + 5; i++)
        g15_history_push(&history, (double)i);
    CHECK(history.count == G15_HISTORY_CAPACITY);
    CHECK(g15_history_get(&history, 0) == 5.0);
    CHECK(g15_history_get(&history, history.count - 1) ==
          (double)(G15_HISTORY_CAPACITY + 4));

    g15_history_clear(&history);
    for (i = 0; i < 39; i++)
        g15_history_push(&history, 10.0);
    g15_history_push(&history, 1000.0);
    CHECK(g15_history_scale(&history, 1.0, 1) < 20.0);
    CHECK(g15_history_scale(&history, 1.0, 0) > 1000.0);
    return 0;
}

static int test_network(void) {
    static const char data[] =
        "Inter-| Receive | Transmit\n"
        " face |bytes packets errs drop fifo frame compressed multicast|bytes packets errs drop fifo colls carrier compressed\n"
        " lo: 100 1 0 0 0 0 0 0 200 2 0 0 0 0 0 0\n"
        " eth0: 4096 4 0 0 0 0 0 0 8192 8 0 0 0 0 0 0\n";
    char interfaces[4][G15_IFACE_NAME_SIZE];
    G15NetworkCounters counters;
    FILE *stream = fixture(data);

    CHECK(stream != NULL);
    CHECK(g15_network_read_stream(stream, "eth0", &counters) == 0);
    CHECK(counters.rx_bytes == 4096 && counters.tx_bytes == 8192);
    fclose(stream);

    stream = fixture(data);
    CHECK(stream != NULL);
    CHECK(g15_network_scan_stream(stream, interfaces, 4, 0) == 1);
    CHECK(strcmp(interfaces[0], "eth0") == 0);
    fclose(stream);
    return 0;
}

static int test_metrics(void) {
    G15CpuState cpu = {0};
    int cpu_percent, ram_percent, swap_percent;
    FILE *stream = fixture("cpu 100 0 0 900 0 0 0 0\n");

    CHECK(stream != NULL);
    CHECK(g15_metrics_parse_cpu(stream, &cpu, &cpu_percent) == 0);
    CHECK(cpu_percent == 0);
    fclose(stream);
    stream = fixture("cpu 150 0 0 950 0 0 0 0\n");
    CHECK(stream != NULL);
    CHECK(g15_metrics_parse_cpu(stream, &cpu, &cpu_percent) == 0);
    CHECK(cpu_percent == 50);
    fclose(stream);

    stream = fixture(
        "MemTotal: 8000000 kB\nMemAvailable: 2000000 kB\n"
        "SwapTotal: 2000000 kB\nSwapFree: 1000000 kB\n");
    CHECK(stream != NULL);
    CHECK(g15_metrics_parse_memory(stream, &ram_percent, &swap_percent) == 0);
    CHECK(ram_percent == 75 && swap_percent == 50);
    fclose(stream);
    return 0;
}

static int test_config(void) {
    char path[] = "/tmp/g15netspeed-config-XXXXXX";
    char error[256];
    G15Config config;
    G15Action action;
    int descriptor = mkstemp(path);
    FILE *stream;

    CHECK(descriptor >= 0);
    stream = fdopen(descriptor, "w");
    CHECK(stream != NULL);
    fputs("interface=eth0\nrefresh_ms=500\nreconnect_ms=3000\n"
          "verbose=true\nscale=maximum\n", stream);
    CHECK(fclose(stream) == 0);

    g15_config_defaults(&config);
    CHECK(g15_config_load_file(path, &config, error, sizeof(error)) == 0);
    CHECK(strcmp(config.interface, "eth0") == 0);
    CHECK(config.automatic_interface == 0);
    CHECK(config.refresh_ms == 500 && config.reconnect_ms == 3000);
    CHECK(config.verbose == 1 && config.adaptive_scale == 0);

    {
        char *arguments[] = {"test", "-c", path, "--refresh", "250", NULL};
        CHECK(g15_config_prepare(5, arguments, &config, &action,
                                 error, sizeof(error)) == 0);
        CHECK(action == G15_ACTION_RUN);
        CHECK(strcmp(config.interface, "eth0") == 0);
        CHECK(config.refresh_ms == 250);
    }

    {
        char *arguments[] = {"test", "-n", "--interface", "auto", NULL};
        CHECK(g15_config_prepare(4, arguments, &config, &action,
                                 error, sizeof(error)) == 0);
        CHECK(config.automatic_interface == 1);
    }
    CHECK(unlink(path) == 0);
    return 0;
}

int main(void) {
    CHECK(test_history() == 0);
    CHECK(test_network() == 0);
    CHECK(test_metrics() == 0);
    CHECK(test_config() == 0);
    puts("Core-Tests erfolgreich.");
    return 0;
}
