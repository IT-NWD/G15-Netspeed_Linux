/*
 * g15netspeed - Netzwerk-Geschwindigkeitsanzeige für Logitech G15
 *
 * Zeigt Upload/Download als scrollende Graphen auf dem G15-LCD an.
 * Benötigt: g15daemon, libg15, libg15render
 *
 * Kompilieren:
 *   gcc -o g15netspeed g15netspeed.c -lg15daemon_client -lg15render -lm
 *
 * Starten:
 *   ./g15netspeed [interface]
 *   z.B.: ./g15netspeed enp5s0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <math.h>
#include <time.h>
#include <libg15.h>
#include <libg15render.h>
#include <g15daemon_client.h>

#define LCD_WIDTH      160
#define LCD_HEIGHT     43
#define GRAPH_WIDTH    139
#define GRAPH_HEIGHT   16
#define GRAPH_DL_Y     2
#define GRAPH_UL_Y     24
#define GRAPH_X        20
#define HISTORY_SIZE   GRAPH_WIDTH
#define UPDATE_MS      150
#define DEFAULT_IFACE  "enp7s0"

static volatile int running = 1;

/* CPU-Auslastung: vorherige Werte für Differenzberechnung */
static unsigned long long prev_cpu_total = 0, prev_cpu_idle = 0;

/* CPU-Auslastung aus /proc/stat lesen (Rückgabe: 0-100%) */
static int read_cpu_usage(void) {
    FILE *fp;
    char line[256];
    unsigned long long user, nice, system, idle, iowait, irq, softirq, steal;
    unsigned long long total, idle_total, diff_total, diff_idle;
    int usage = 0;

    fp = fopen("/proc/stat", "r");
    if (!fp) return 0;

    if (fgets(line, sizeof(line), fp)) {
        if (sscanf(line, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
                   &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal) == 8) {
            idle_total = idle + iowait;
            total = user + nice + system + idle + iowait + irq + softirq + steal;

            diff_total = total - prev_cpu_total;
            diff_idle = idle_total - prev_cpu_idle;

            if (diff_total > 0)
                usage = (int)(100.0 * (double)(diff_total - diff_idle) / (double)diff_total + 0.5);

            prev_cpu_total = total;
            prev_cpu_idle = idle_total;
        }
    }
    fclose(fp);
    return usage;
}

/* RAM-Auslastung aus /proc/meminfo lesen (Rückgabe: 0-100%) */
static int read_ram_usage(void) {
    FILE *fp;
    char line[256];
    unsigned long long mem_total = 0, mem_available = 0;
    int found = 0;

    fp = fopen("/proc/meminfo", "r");
    if (!fp) return 0;

    while (fgets(line, sizeof(line), fp) && found < 2) {
        if (sscanf(line, "MemTotal: %llu kB", &mem_total) == 1) found++;
        if (sscanf(line, "MemAvailable: %llu kB", &mem_available) == 1) found++;
    }
    fclose(fp);

    if (mem_total > 0)
        return (int)(100.0 * (double)(mem_total - mem_available) / (double)mem_total + 0.5);
    return 0;
}

/* GPU-Auslastung über nvidia-smi lesen (gecacht, max. 1x pro Sekunde) */
static int read_gpu_usage(void) {
    static int cached_usage = -1;
    static struct timespec last_read = {0, 0};
    struct timespec now;
    FILE *fp;
    int usage;

    clock_gettime(CLOCK_MONOTONIC, &now);

    /* Nur alle 1 Sekunde neu abfragen */
    if (cached_usage >= 0 &&
        (now.tv_sec - last_read.tv_sec) < 1) {
        return cached_usage;
    }

    fp = popen("nvidia-smi --query-gpu=utilization.gpu --format=csv,noheader,nounits 2>/dev/null", "r");
    if (fp) {
        if (fscanf(fp, "%d", &usage) == 1)
            cached_usage = usage;
        pclose(fp);
    }

    last_read = now;
    return cached_usage;
}

static void signal_handler(int sig) {
    (void)sig;
    running = 0;
}

/* Netzwerk-Bytes aus /proc/net/dev lesen */
static int read_net_bytes(const char *iface, unsigned long long *rx, unsigned long long *tx) {
    FILE *fp;
    char line[512];
    char name[128];
    int found = 0;

    fp = fopen("/proc/net/dev", "r");
    if (!fp)
        return -1;

    /* Erste zwei Zeilen sind Header */
    if (!fgets(line, sizeof(line), fp)) { fclose(fp); return -1; }
    if (!fgets(line, sizeof(line), fp)) { fclose(fp); return -1; }

    while (fgets(line, sizeof(line), fp)) {
        unsigned long long r_bytes, t_bytes;
        unsigned long long d[14];
        /* Format: "  iface: rx_bytes rx_packets ... tx_bytes tx_packets ..." */
        if (sscanf(line,
                   " %[^:]: %llu %llu %llu %llu %llu %llu %llu %llu "
                   "%llu %llu %llu %llu %llu %llu %llu %llu",
                   name,
                   &r_bytes, &d[0], &d[1], &d[2], &d[3], &d[4], &d[5], &d[6],
                   &t_bytes, &d[7], &d[8], &d[9], &d[10], &d[11], &d[12], &d[13]) == 17) {
            if (strcmp(name, iface) == 0) {
                *rx = r_bytes;
                *tx = t_bytes;
                found = 1;
                break;
            }
        }
    }

    fclose(fp);
    return found ? 0 : -1;
}

/* Formatierte Geschwindigkeit als String */
static void format_speed(double kbps, char *buf, size_t len) {
    if (kbps >= 1024.0)
        snprintf(buf, len, "%.1fM", kbps / 1024.0);
    else if (kbps >= 10.0)
        snprintf(buf, len, "%.0fK", kbps);
    else
        snprintf(buf, len, "%.1fK", kbps);
}

/* Graph zeichnen (rechtsbündig, gefüllte Fläche, nach oben) */
static void draw_graph(g15canvas *canvas, double *history, int count,
                       int x, int y, int w, int h, double max_val, int inverted) {
    int i, bar_h, prev_bar_h;
    int offset;

    /* Rahmen mit einzelnen Linien */
    g15r_drawLine(canvas, x - 1, y - 1, x + w, y - 1, G15_COLOR_BLACK);
    g15r_drawLine(canvas, x - 1, y + h, x + w, y + h, G15_COLOR_BLACK);
    g15r_drawLine(canvas, x - 1, y - 1, x - 1, y + h, G15_COLOR_BLACK);
    g15r_drawLine(canvas, x + w, y - 1, x + w, y + h, G15_COLOR_BLACK);

    if (max_val <= 0.0)
        max_val = 1.0;

    /* Rechtsbündig: Offset berechnen */
    offset = w - count;
    if (offset < 0) offset = 0;

    prev_bar_h = -1;
    for (i = 0; i < count && i < w; i++) {
        bar_h = (int)((history[i] / max_val) * (double)(h - 1));
        if (bar_h > (h - 1)) bar_h = h - 1;
        if (bar_h < 0) bar_h = 0;

        if (inverted) {
            /* Invertiert: von oben nach unten wachsend */
            if (bar_h > 0) {
                g15r_drawLine(canvas, x + offset + i, y,
                              x + offset + i, y + bar_h, G15_COLOR_BLACK);
            }
            if (prev_bar_h >= 0 && (bar_h > 0 || prev_bar_h > 0)) {
                g15r_drawLine(canvas, x + offset + i - 1, y + prev_bar_h,
                              x + offset + i, y + bar_h, G15_COLOR_BLACK);
            }
        } else {
            /* Normal: von unten nach oben wachsend */
            if (bar_h > 0) {
                g15r_drawLine(canvas, x + offset + i, y + h - 1 - bar_h,
                              x + offset + i, y + h - 1, G15_COLOR_BLACK);
            }
            if (prev_bar_h >= 0 && (bar_h > 0 || prev_bar_h > 0)) {
                g15r_drawLine(canvas, x + offset + i - 1, y + h - 1 - prev_bar_h,
                              x + offset + i, y + h - 1 - bar_h, G15_COLOR_BLACK);
            }
        }

        prev_bar_h = bar_h;
    }
}

/* Maximalen Wert im History-Array finden */
static double find_max(double *history, int count) {
    double max = 0.0;
    int i;
    for (i = 0; i < count; i++) {
        if (history[i] > max)
            max = history[i];
    }
    return max;
}

/* History-Array nach links schieben und neuen Wert anhängen */
static void push_history(double *history, int *count, int max_size, double value) {
    if (*count >= max_size) {
        memmove(history, history + 1, (max_size - 1) * sizeof(double));
        history[max_size - 1] = value;
    } else {
        history[*count] = value;
        (*count)++;
    }
}

int main(int argc, char *argv[]) {
    const char *iface = DEFAULT_IFACE;
    int g15_fd;
    g15canvas canvas;
    unsigned long long prev_rx = 0, prev_tx = 0;
    unsigned long long curr_rx, curr_tx;
    double dl_history[HISTORY_SIZE];
    double ul_history[HISTORY_SIZE];
    int dl_count = 0, ul_count = 0;
    int first_read = 1;
    char dl_str[32], ul_str[32], max_dl_str[32], max_ul_str[32];
    char title[128];

    if (argc > 1)
        iface = argv[1];

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    memset(dl_history, 0, sizeof(dl_history));
    memset(ul_history, 0, sizeof(ul_history));

    /* Zum g15daemon verbinden */
    g15_fd = new_g15_screen(G15_G15RBUF);
    if (g15_fd < 0) {
        fprintf(stderr, "Fehler: Kann nicht zum g15daemon verbinden.\n");
        fprintf(stderr, "Ist g15daemon gestartet?\n");
        return 1;
    }

    g15r_initCanvas(&canvas);

    /* Initiales Lesen der Bytes */
    if (read_net_bytes(iface, &prev_rx, &prev_tx) != 0) {
        fprintf(stderr, "Fehler: Interface '%s' nicht gefunden in /proc/net/dev\n", iface);
        g15_close_screen(g15_fd);
        return 1;
    }

    printf("g15netspeed gestartet für Interface: '%s'\n", iface);
    printf("Drücke Ctrl+C zum Beenden.\n");

    while (running) {
        struct timespec ts;
        ts.tv_sec = UPDATE_MS / 1000;
        ts.tv_nsec = (UPDATE_MS % 1000) * 1000000L;
        nanosleep(&ts, NULL);

        if (read_net_bytes(iface, &curr_rx, &curr_tx) != 0)
            continue;

        if (first_read) {
            prev_rx = curr_rx;
            prev_tx = curr_tx;
            first_read = 0;
            continue;
        }

        /* KB/s berechnen (Skalierung auf 1 Sekunde) */
        double dl_kbps = (double)(curr_rx - prev_rx) / 1024.0 * (1000.0 / UPDATE_MS);
        double ul_kbps = (double)(curr_tx - prev_tx) / 1024.0 * (1000.0 / UPDATE_MS);

        printf("DL: %.2f KB/s  UL: %.2f KB/s  (rx=%llu tx=%llu)\n",
               dl_kbps, ul_kbps, curr_rx, curr_tx);

        prev_rx = curr_rx;
        prev_tx = curr_tx;

        /* History aktualisieren */
        push_history(dl_history, &dl_count, HISTORY_SIZE, dl_kbps);
        push_history(ul_history, &ul_count, HISTORY_SIZE, ul_kbps);

        /* Maximalwerte für Skalierung */
        double dl_max = find_max(dl_history, dl_count);
        double ul_max = find_max(ul_history, ul_count);

        /* Mindest-Skalierung: 10 KB/s */
        if (dl_max < 10.0) dl_max = 10.0;
        if (ul_max < 10.0) ul_max = 10.0;

        /* Strings formatieren */
        format_speed(dl_kbps, dl_str, sizeof(dl_str));
        format_speed(dl_max, max_dl_str, sizeof(max_dl_str));
        format_speed(ul_kbps, ul_str, sizeof(ul_str));
        format_speed(ul_max, max_ul_str, sizeof(max_ul_str));

        /* System-Auslastung lesen */
        int cpu_pct = read_cpu_usage();
        int ram_pct = read_ram_usage();
        int gpu_pct = read_gpu_usage();

        /* Canvas leeren und zeichnen */
        g15r_clearScreen(&canvas, G15_COLOR_WHITE);

        /* Zeile 0: Interface links, CPU/GPU/RAM rechts */
        {
            char iface_short[8];
            snprintf(iface_short, sizeof(iface_short), "%s", iface);
            g15r_renderString(&canvas, (unsigned char *)iface_short, 0, G15_TEXT_MED, 0, 0);
        }
        if (gpu_pct >= 0)
            snprintf(title, sizeof(title), "CPU:%d%% GPU:%d%% RAM:%d%%", cpu_pct, gpu_pct, ram_pct);
        else
            snprintf(title, sizeof(title), "CPU:%d%% GPU:N/A RAM:%d%%", cpu_pct, ram_pct);
        g15r_renderString(&canvas, (unsigned char *)title, 0, G15_TEXT_MED,
                          LCD_WIDTH - (int)strlen(title) * 5, 0);

        /* Download-Label und Graph: y=10 bis y=25 */
        g15r_renderString(&canvas, (unsigned char *)"DL", 0, G15_TEXT_SMALL, 1, 10);
        g15r_renderString(&canvas, (unsigned char *)dl_str, 0, G15_TEXT_SMALL, 1, 18);
        draw_graph(&canvas, dl_history, dl_count,
                   GRAPH_X, 10, GRAPH_WIDTH, GRAPH_HEIGHT, dl_max, 0);

        /* Trennlinie */
        g15r_drawLine(&canvas, 0, 27, LCD_WIDTH - 1, 27, G15_COLOR_BLACK);

        /* Upload-Label und Graph: y=28 bis y=41 (invertiert, wächst nach unten) */
        g15r_renderString(&canvas, (unsigned char *)"UL", 0, G15_TEXT_SMALL, 1, 29);
        g15r_renderString(&canvas, (unsigned char *)ul_str, 0, G15_TEXT_SMALL, 1, 36);
        draw_graph(&canvas, ul_history, ul_count,
                   GRAPH_X, 28, GRAPH_WIDTH, 14, ul_max, 1);

        /* An g15daemon senden */
        g15_send(g15_fd, (char *)canvas.buffer, G15_BUFFER_LEN);
    }

    /* Aufräumen: Display leeren */
    g15r_clearScreen(&canvas, G15_COLOR_WHITE);
    g15_send(g15_fd, (char *)canvas.buffer, G15_BUFFER_LEN);
    g15_close_screen(g15_fd);

    printf("\ng15netspeed beendet.\n");
    return 0;
}
