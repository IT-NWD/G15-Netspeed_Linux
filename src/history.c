#include "g15netspeed/history.h"

#include <stdlib.h>
#include <string.h>

static int compare_double(const void *left, const void *right) {
    const double a = *(const double *)left;
    const double b = *(const double *)right;
    return (a > b) - (a < b);
}

void g15_history_init(G15History *history) {
    memset(history, 0, sizeof(*history));
}

void g15_history_clear(G15History *history) {
    g15_history_init(history);
}

void g15_history_push(G15History *history, double value) {
    size_t position;

    if (history->count < G15_HISTORY_CAPACITY) {
        position = (history->start + history->count) % G15_HISTORY_CAPACITY;
        history->count++;
    } else {
        position = history->start;
        history->start = (history->start + 1) % G15_HISTORY_CAPACITY;
    }
    history->values[position] = value;
}

double g15_history_get(const G15History *history, size_t index) {
    if (index >= history->count)
        return 0.0;
    return history->values[(history->start + index) % G15_HISTORY_CAPACITY];
}

double g15_history_scale(const G15History *history, double minimum,
                         int use_percentile) {
    double samples[G15_HISTORY_CAPACITY];
    double scale = minimum;
    size_t i;

    if (history->count == 0)
        return minimum;

    for (i = 0; i < history->count; i++)
        samples[i] = g15_history_get(history, i);

    if (use_percentile && history->count >= 20) {
        size_t percentile_index;
        qsort(samples, history->count, sizeof(samples[0]), compare_double);
        percentile_index = (history->count * 95 + 99) / 100;
        if (percentile_index > 0)
            percentile_index--;
        scale = samples[percentile_index] * 1.15;
    } else {
        for (i = 0; i < history->count; i++) {
            if (samples[i] > scale)
                scale = samples[i];
        }
        if (scale > minimum)
            scale *= 1.05;
    }

    return scale < minimum ? minimum : scale;
}
