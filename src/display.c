#include "g15netspeed/display.h"

#include <stdio.h>
#include <string.h>

#define LCD_WIDTH 160
#define GRAPH_X 35
#define GRAPH_WIDTH G15_HISTORY_CAPACITY
#define GRAPH_TOP_Y 10
#define GRAPH_TOP_HEIGHT 16
#define GRAPH_BOTTOM_Y 28
#define GRAPH_BOTTOM_HEIGHT 14

static void format_speed(double kbps, char *value, size_t value_length,
                         const char **unit) {
    if (kbps >= 1048576.0) {
        snprintf(value, value_length, "%.1f", kbps / 1048576.0);
        *unit = "GB/s";
    } else if (kbps >= 1024.0) {
        snprintf(value, value_length, "%.1f", kbps / 1024.0);
        *unit = "MB/s";
    } else {
        snprintf(value, value_length, kbps >= 10.0 ? "%.0f" : "%.1f", kbps);
        *unit = "KB/s";
    }
}

static void format_bytes(unsigned long long bytes, char *buffer, size_t length) {
    const double value = (double)bytes;

    if (value >= 1099511627776.0)
        snprintf(buffer, length, "%.1fT", value / 1099511627776.0);
    else if (value >= 1073741824.0)
        snprintf(buffer, length, "%.1fG", value / 1073741824.0);
    else if (value >= 1048576.0)
        snprintf(buffer, length, "%.1fM", value / 1048576.0);
    else
        snprintf(buffer, length, "%.0fK", value / 1024.0);
}

static void draw_graph(g15canvas *canvas, const G15History *history,
                       int y, int height, double maximum, int inverted) {
    size_t i;
    int offset = GRAPH_WIDTH - (int)history->count;
    int previous_height = -1;

    g15r_drawLine(canvas, GRAPH_X - 1, y - 1,
                  GRAPH_X + GRAPH_WIDTH, y - 1, G15_COLOR_BLACK);
    g15r_drawLine(canvas, GRAPH_X - 1, y + height,
                  GRAPH_X + GRAPH_WIDTH, y + height, G15_COLOR_BLACK);
    g15r_drawLine(canvas, GRAPH_X - 1, y - 1,
                  GRAPH_X - 1, y + height, G15_COLOR_BLACK);
    g15r_drawLine(canvas, GRAPH_X + GRAPH_WIDTH, y - 1,
                  GRAPH_X + GRAPH_WIDTH, y + height, G15_COLOR_BLACK);

    if (maximum <= 0.0)
        maximum = 1.0;
    if (offset < 0)
        offset = 0;

    for (i = 0; i < history->count && i < GRAPH_WIDTH; i++) {
        int bar_height = (int)(g15_history_get(history, i) / maximum *
                               (double)(height - 1));
        const int x = GRAPH_X + offset + (int)i;

        if (bar_height > height - 1)
            bar_height = height - 1;
        if (bar_height < 0)
            bar_height = 0;

        if (inverted) {
            if (bar_height > 0)
                g15r_drawLine(canvas, x, y, x, y + bar_height, G15_COLOR_BLACK);
            if (previous_height >= 0 && (bar_height > 0 || previous_height > 0))
                g15r_drawLine(canvas, x - 1, y + previous_height,
                              x, y + bar_height, G15_COLOR_BLACK);
        } else {
            if (bar_height > 0)
                g15r_drawLine(canvas, x, y + height - 1 - bar_height,
                              x, y + height - 1, G15_COLOR_BLACK);
            if (previous_height >= 0 && (bar_height > 0 || previous_height > 0))
                g15r_drawLine(canvas, x - 1, y + height - 1 - previous_height,
                              x, y + height - 1 - bar_height, G15_COLOR_BLACK);
        }
        previous_height = bar_height;
    }
}

static void render_label(g15canvas *canvas, const char *name,
                         const char *value, const char *unit,
                         int name_y, int value_y) {
    char label[16];

    if (unit)
        snprintf(label, sizeof(label), "%s %s", name, unit);
    else
        snprintf(label, sizeof(label), "%s", name);
    g15r_renderString(canvas, (unsigned char *)label, 0,
                      G15_TEXT_SMALL, 1, name_y);
    g15r_renderString(canvas, (unsigned char *)value, 0,
                      G15_TEXT_SMALL, 1, value_y);
}

void g15_display_history_init(G15DisplayHistory *history) {
    g15_history_init(&history->download);
    g15_history_init(&history->upload);
    g15_history_init(&history->cpu);
    g15_history_init(&history->temperature);
    g15_history_init(&history->ram);
    g15_history_init(&history->swap);
}

void g15_display_history_clear_network(G15DisplayHistory *history) {
    g15_history_clear(&history->download);
    g15_history_clear(&history->upload);
}

void g15_display_history_push(G15DisplayHistory *history,
                              const G15DisplayData *data) {
    g15_history_push(&history->download, data->download_kbps);
    g15_history_push(&history->upload, data->upload_kbps);
    g15_history_push(&history->cpu, data->metrics.cpu_percent);
    if (data->metrics.cpu_temperature_available)
        g15_history_push(&history->temperature, data->metrics.cpu_temperature);
    g15_history_push(&history->ram, data->metrics.ram_percent);
    g15_history_push(&history->swap, data->metrics.swap_percent);
}

static void render_network(g15canvas *canvas, const G15DisplayHistory *history,
                           const G15DisplayData *data) {
    char header[128], rx_total[16], tx_total[16];
    char download[16], upload[16];
    const char *download_unit, *upload_unit;
    double download_max, upload_max;

    format_bytes(data->counters.rx_bytes, rx_total, sizeof(rx_total));
    format_bytes(data->counters.tx_bytes, tx_total, sizeof(tx_total));
    snprintf(header, sizeof(header), "%.7s D:%s U:%s",
             data->interface, rx_total, tx_total);
    g15r_renderString(canvas, (unsigned char *)header, 0,
                      G15_TEXT_MED, 0, 0);

    format_speed(data->download_kbps, download, sizeof(download), &download_unit);
    format_speed(data->upload_kbps, upload, sizeof(upload), &upload_unit);
    download_max = g15_history_scale(&history->download, 10.0, data->adaptive_scale);
    upload_max = g15_history_scale(&history->upload, 10.0, data->adaptive_scale);

    render_label(canvas, "DL", download, download_unit, 10, 17);
    draw_graph(canvas, &history->download, GRAPH_TOP_Y, GRAPH_TOP_HEIGHT,
               download_max, 0);
    g15r_drawLine(canvas, 0, 27, LCD_WIDTH - 1, 27, G15_COLOR_BLACK);
    render_label(canvas, "UL", upload, upload_unit, 29, 35);
    draw_graph(canvas, &history->upload, GRAPH_BOTTOM_Y, GRAPH_BOTTOM_HEIGHT,
               upload_max, 1);
}

static void render_cpu(g15canvas *canvas, const G15DisplayHistory *history,
                       const G15DisplayData *data) {
    char header[64], cpu[16], temperature[16];
    double temperature_max;

    if (data->metrics.cpu_temperature_available)
        snprintf(header, sizeof(header), "CPU %d%%  Temperatur %dC",
                 data->metrics.cpu_percent, data->metrics.cpu_temperature);
    else
        snprintf(header, sizeof(header), "CPU %d%%  Temperatur N/A",
                 data->metrics.cpu_percent);
    g15r_renderString(canvas, (unsigned char *)header, 0,
                      G15_TEXT_MED, 0, 0);

    snprintf(cpu, sizeof(cpu), "%d%%", data->metrics.cpu_percent);
    render_label(canvas, "CPU", cpu, NULL, 10, 18);
    draw_graph(canvas, &history->cpu, GRAPH_TOP_Y, GRAPH_TOP_HEIGHT, 100.0, 0);
    g15r_drawLine(canvas, 0, 27, LCD_WIDTH - 1, 27, G15_COLOR_BLACK);

    if (data->metrics.cpu_temperature_available)
        snprintf(temperature, sizeof(temperature), "%dC",
                 data->metrics.cpu_temperature);
    else
        snprintf(temperature, sizeof(temperature), "N/A");
    temperature_max = g15_history_scale(&history->temperature, 80.0,
                                        data->adaptive_scale);
    render_label(canvas, "TMP", temperature, NULL, 29, 36);
    draw_graph(canvas, &history->temperature, GRAPH_BOTTOM_Y,
               GRAPH_BOTTOM_HEIGHT, temperature_max, 1);
}

static void render_memory(g15canvas *canvas, const G15DisplayHistory *history,
                          const G15DisplayData *data) {
    char header[64], ram[16], swap[16];

    snprintf(header, sizeof(header), "RAM %d%%  Swap %d%%",
             data->metrics.ram_percent, data->metrics.swap_percent);
    g15r_renderString(canvas, (unsigned char *)header, 0,
                      G15_TEXT_MED, 0, 0);
    snprintf(ram, sizeof(ram), "%d%%", data->metrics.ram_percent);
    snprintf(swap, sizeof(swap), "%d%%", data->metrics.swap_percent);
    render_label(canvas, "RAM", ram, NULL, 10, 18);
    draw_graph(canvas, &history->ram, GRAPH_TOP_Y, GRAPH_TOP_HEIGHT, 100.0, 0);
    g15r_drawLine(canvas, 0, 27, LCD_WIDTH - 1, 27, G15_COLOR_BLACK);
    render_label(canvas, "SWP", swap, NULL, 29, 36);
    draw_graph(canvas, &history->swap, GRAPH_BOTTOM_Y, GRAPH_BOTTOM_HEIGHT,
               100.0, 1);
}

void g15_display_render(g15canvas *canvas, G15Page page,
                        const G15DisplayHistory *history,
                        const G15DisplayData *data) {
    g15r_clearScreen(canvas, G15_COLOR_WHITE);
    if (page == G15_PAGE_CPU)
        render_cpu(canvas, history, data);
    else if (page == G15_PAGE_MEMORY)
        render_memory(canvas, history, data);
    else
        render_network(canvas, history, data);
}

const char *g15_display_page_name(G15Page page) {
    if (page == G15_PAGE_CPU)
        return "CPU";
    if (page == G15_PAGE_MEMORY)
        return "RAM/Swap";
    return "Netzwerk";
}
