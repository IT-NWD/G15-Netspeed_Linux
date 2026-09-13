#ifndef G15NETSPEED_METRICS_H
#define G15NETSPEED_METRICS_H

#include <stdio.h>
#include <time.h>

typedef struct {
    unsigned long long previous_total;
    unsigned long long previous_idle;
    int initialized;
} G15CpuState;

typedef struct {
    G15CpuState cpu;
    char temperature_path[256];
    int temperature_searched;
    int cached_temperature;
    int temperature_available;
    struct timespec last_temperature_read;
} G15MetricsState;

typedef struct {
    int cpu_percent;
    int ram_percent;
    int swap_percent;
    int cpu_temperature;
    int cpu_temperature_available;
} G15SystemMetrics;

void g15_metrics_state_init(G15MetricsState *state);
int g15_metrics_parse_cpu(FILE *stream, G15CpuState *state, int *percent);
int g15_metrics_parse_memory(FILE *stream, int *ram_percent, int *swap_percent);
int g15_metrics_read(G15MetricsState *state, G15SystemMetrics *metrics);

#endif
