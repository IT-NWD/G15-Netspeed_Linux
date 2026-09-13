#ifndef G15NETSPEED_DISPLAY_H
#define G15NETSPEED_DISPLAY_H

#include <libg15.h>
#include <libg15render.h>

#include "g15netspeed/history.h"
#include "g15netspeed/metrics.h"
#include "g15netspeed/network.h"

typedef enum {
    G15_PAGE_NETWORK = 0,
    G15_PAGE_CPU,
    G15_PAGE_MEMORY,
    G15_PAGE_COUNT
} G15Page;

typedef struct {
    G15History download;
    G15History upload;
    G15History cpu;
    G15History temperature;
    G15History ram;
    G15History swap;
} G15DisplayHistory;

typedef struct {
    const char *interface;
    G15NetworkCounters counters;
    double download_kbps;
    double upload_kbps;
    G15SystemMetrics metrics;
    int adaptive_scale;
} G15DisplayData;

void g15_display_history_init(G15DisplayHistory *history);
void g15_display_history_clear_network(G15DisplayHistory *history);
void g15_display_history_push(G15DisplayHistory *history,
                              const G15DisplayData *data);
void g15_display_render(g15canvas *canvas, G15Page page,
                        const G15DisplayHistory *history,
                        const G15DisplayData *data);
const char *g15_display_page_name(G15Page page);

#endif
