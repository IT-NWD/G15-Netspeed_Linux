#ifndef G15NETSPEED_HISTORY_H
#define G15NETSPEED_HISTORY_H

#include <stddef.h>

#define G15_HISTORY_CAPACITY 124

typedef struct {
    double values[G15_HISTORY_CAPACITY];
    size_t start;
    size_t count;
} G15History;

void g15_history_init(G15History *history);
void g15_history_clear(G15History *history);
void g15_history_push(G15History *history, double value);
double g15_history_get(const G15History *history, size_t index);
double g15_history_scale(const G15History *history, double minimum,
                         int use_percentile);

#endif
