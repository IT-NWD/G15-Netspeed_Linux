#include "g15netspeed/metrics.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static int percentage(unsigned long long used, unsigned long long total) {
    if (total == 0)
        return 0;
    return (int)(100.0 * (double)used / (double)total + 0.5);
}

static int parse_unsigned_values(char *text, unsigned long long *values,
                                 size_t count) {
    size_t i;

    for (i = 0; i < count; i++) {
        char *end;

        errno = 0;
        values[i] = strtoull(text, &end, 10);
        if (errno != 0 || end == text)
            return -1;
        text = end;
    }
    return 0;
}

static int parse_prefixed_value(const char *line, const char *prefix,
                                unsigned long long *value) {
    char *end;
    const size_t prefix_length = strlen(prefix);

    if (strncmp(line, prefix, prefix_length) != 0)
        return 0;
    errno = 0;
    *value = strtoull(line + prefix_length, &end, 10);
    return errno == 0 && end != line + prefix_length ? 1 : -1;
}

void g15_metrics_state_init(G15MetricsState *state) {
    memset(state, 0, sizeof(*state));
}

int g15_metrics_parse_cpu(FILE *stream, G15CpuState *state, int *percent) {
    char line[256];
    unsigned long long values[8];
    unsigned long long total, idle_total;

    if (!fgets(line, sizeof(line), stream) || strncmp(line, "cpu ", 4) != 0 ||
        parse_unsigned_values(line + 4, values, 8) != 0)
        return -1;

    idle_total = values[3] + values[4];
    total = values[0] + values[1] + values[2] + values[3] +
            values[4] + values[5] + values[6] + values[7];
    *percent = 0;

    if (state->initialized && total >= state->previous_total &&
        idle_total >= state->previous_idle) {
        const unsigned long long total_delta = total - state->previous_total;
        const unsigned long long idle_delta = idle_total - state->previous_idle;

        if (total_delta > 0 && idle_delta <= total_delta)
            *percent = percentage(total_delta - idle_delta, total_delta);
    }

    state->previous_total = total;
    state->previous_idle = idle_total;
    state->initialized = 1;
    return 0;
}

int g15_metrics_parse_memory(FILE *stream, int *ram_percent, int *swap_percent) {
    char line[256];
    unsigned long long mem_total = 0, mem_available = 0;
    unsigned long long swap_total = 0, swap_free = 0;
    int have_mem_total = 0, have_mem_available = 0;
    int have_swap_total = 0, have_swap_free = 0;

    while (fgets(line, sizeof(line), stream)) {
        int parsed;

        parsed = parse_prefixed_value(line, "MemTotal:", &mem_total);
        if (parsed < 0)
            return -1;
        if (parsed > 0)
            have_mem_total = 1;
        parsed = parse_prefixed_value(line, "MemAvailable:", &mem_available);
        if (parsed < 0)
            return -1;
        if (parsed > 0)
            have_mem_available = 1;
        parsed = parse_prefixed_value(line, "SwapTotal:", &swap_total);
        if (parsed < 0)
            return -1;
        if (parsed > 0)
            have_swap_total = 1;
        parsed = parse_prefixed_value(line, "SwapFree:", &swap_free);
        if (parsed < 0)
            return -1;
        if (parsed > 0)
            have_swap_free = 1;
    }

    if (!have_mem_total || !have_mem_available || !have_swap_total ||
        !have_swap_free || mem_total == 0 || mem_available > mem_total ||
        swap_free > swap_total)
        return -1;

    *ram_percent = percentage(mem_total - mem_available, mem_total);
    *swap_percent = swap_total == 0 ? 0 : percentage(swap_total - swap_free, swap_total);
    return 0;
}

static int find_hwmon_temperature(char *path, size_t length) {
    DIR *directory = opendir("/sys/class/hwmon");
    struct dirent *entry;

    if (!directory)
        return -1;

    while ((entry = readdir(directory)) != NULL) {
        char name_path[256], name[64];
        FILE *stream;

        if (entry->d_name[0] == '.')
            continue;
        snprintf(name_path, sizeof(name_path), "/sys/class/hwmon/%.100s/name",
                 entry->d_name);
        stream = fopen(name_path, "r");
        if (!stream)
            continue;
        name[0] = '\0';
        if (fscanf(stream, "%63s", name) != 1) {
            fclose(stream);
            continue;
        }
        fclose(stream);

        if (strcmp(name, "coretemp") == 0 || strcmp(name, "k10temp") == 0 ||
            strcmp(name, "zenpower") == 0 || strcmp(name, "cpu_thermal") == 0) {
            snprintf(path, length, "/sys/class/hwmon/%.100s/temp1_input",
                     entry->d_name);
            closedir(directory);
            return 0;
        }
    }
    closedir(directory);
    return -1;
}

static int find_thermal_temperature(char *path, size_t length) {
    DIR *directory = opendir("/sys/class/thermal");
    struct dirent *entry;

    if (!directory)
        return -1;

    while ((entry = readdir(directory)) != NULL) {
        char type_path[256], type[64];
        FILE *stream;

        if (strncmp(entry->d_name, "thermal_zone", 12) != 0)
            continue;
        snprintf(type_path, sizeof(type_path), "/sys/class/thermal/%.100s/type",
                 entry->d_name);
        stream = fopen(type_path, "r");
        if (!stream)
            continue;
        type[0] = '\0';
        if (fscanf(stream, "%63s", type) != 1) {
            fclose(stream);
            continue;
        }
        fclose(stream);

        if (strcmp(type, "x86_pkg_temp") == 0 || strcmp(type, "cpu-thermal") == 0) {
            snprintf(path, length, "/sys/class/thermal/%.100s/temp",
                     entry->d_name);
            closedir(directory);
            return 0;
        }
    }
    closedir(directory);
    return -1;
}

static int read_temperature(G15MetricsState *state) {
    struct timespec now;
    FILE *stream;
    char line[64], *end;
    long millidegrees;

    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return -1;

    if (state->temperature_available &&
        now.tv_sec - state->last_temperature_read.tv_sec < 1)
        return 0;

    if (!state->temperature_searched) {
        state->temperature_searched = 1;
        if (find_hwmon_temperature(state->temperature_path,
                                   sizeof(state->temperature_path)) != 0 &&
            find_thermal_temperature(state->temperature_path,
                                     sizeof(state->temperature_path)) != 0)
            return -1;
    }

    stream = fopen(state->temperature_path, "r");
    if (!stream) {
        state->temperature_available = 0;
        return -1;
    }
    if (!fgets(line, sizeof(line), stream)) {
        fclose(stream);
        state->temperature_available = 0;
        return -1;
    }
    fclose(stream);

    errno = 0;
    millidegrees = strtol(line, &end, 10);
    if (errno != 0 || end == line || millidegrees <= 0 ||
        millidegrees > INT_MAX) {
        state->temperature_available = 0;
        return -1;
    }

    state->cached_temperature = (int)(millidegrees / 1000);
    state->temperature_available = state->cached_temperature > 0;
    state->last_temperature_read = now;
    return state->temperature_available ? 0 : -1;
}

int g15_metrics_read(G15MetricsState *state, G15SystemMetrics *metrics) {
    FILE *stream;
    int result = 0;

    stream = fopen("/proc/stat", "r");
    if (!stream || g15_metrics_parse_cpu(stream, &state->cpu,
                                         &metrics->cpu_percent) != 0)
        result = -1;
    if (stream)
        fclose(stream);

    stream = fopen("/proc/meminfo", "r");
    if (!stream || g15_metrics_parse_memory(stream, &metrics->ram_percent,
                                            &metrics->swap_percent) != 0)
        result = -1;
    if (stream)
        fclose(stream);

    (void)read_temperature(state);
    metrics->cpu_temperature = state->cached_temperature;
    metrics->cpu_temperature_available = state->temperature_available;
    return result;
}
