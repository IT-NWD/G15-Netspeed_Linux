/*
 * g15netspeed - System-Monitor für Logitech G15
 *
 * 4 Seiten (umschaltbar mit G1-Taste):
 *   Seite 0: Netzwerk (DL/UL Graphen + Gesamtdatenmenge)
 *   Seite 1: CPU (Auslastung + Temperatur)
 *   Seite 2: GPU (Auslastung + Temperatur)
 *   Seite 3: RAM (Auslastung + Swap)
 *
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
#include <fcntl.h>
#include <errno.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <libg15.h>
#include <libg15render.h>
#include <g15daemon_client.h>

#define LCD_WIDTH      160
#define LCD_HEIGHT     43
#define GRAPH_WIDTH    126
#define GRAPH_HEIGHT   16
#define GRAPH_X        33
#define HISTORY_SIZE   GRAPH_WIDTH
#define UPDATE_MS      150
#define DEFAULT_IFACE  "enp7s0"
#define NUM_PAGES      4
#define L1_KEY         0x00800000

static volatile int running = 1;
static int current_page = 0;

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

/* CPU-Temperatur aus /sys lesen (Rückgabe: Grad Celsius, 0 bei Fehler) */
static int read_cpu_temp(void) {
    static int cached_temp = 0;
    static struct timespec last_read = {0, 0};
    struct timespec now;
    FILE *fp;
    int temp;

    clock_gettime(CLOCK_MONOTONIC, &now);
    if (cached_temp > 0 && (now.tv_sec - last_read.tv_sec) < 1)
        return cached_temp;

    fp = fopen("/sys/class/thermal/thermal_zone0/temp", "r");
    if (!fp)
        fp = fopen("/sys/class/hwmon/hwmon0/temp1_input", "r");
    if (fp) {
        if (fscanf(fp, "%d", &temp) == 1)
            cached_temp = temp / 1000;
        fclose(fp);
    }

    last_read = now;
    return cached_temp;
}

/* GPU-Temperatur über nvidia-smi lesen (gecacht, max. 1x pro Sekunde) */
static int read_gpu_temp(void) {
    static int cached_temp = 0;
    static struct timespec last_read = {0, 0};
    struct timespec now;
    FILE *fp;
    int temp;

    clock_gettime(CLOCK_MONOTONIC, &now);
    if (cached_temp > 0 && (now.tv_sec - last_read.tv_sec) < 1)
        return cached_temp;

    fp = popen("nvidia-smi --query-gpu=temperature.gpu --format=csv,noheader,nounits 2>/dev/null", "r");
    if (fp) {
        if (fscanf(fp, "%d", &temp) == 1)
            cached_temp = temp;
        pclose(fp);
    }

    last_read = now;
    return cached_temp;
}

/* Swap-Auslastung aus /proc/meminfo lesen (Rückgabe: 0-100%) */
static int read_swap_usage(void) {
    FILE *fp;
    char line[256];
    unsigned long long swap_total = 0, swap_free = 0;
    int found = 0;

    fp = fopen("/proc/meminfo", "r");
    if (!fp) return 0;

    while (fgets(line, sizeof(line), fp) && found < 2) {
        if (sscanf(line, "SwapTotal: %llu kB", &swap_total) == 1) found++;
        if (sscanf(line, "SwapFree: %llu kB", &swap_free) == 1) found++;
    }
    fclose(fp);

    if (swap_total > 0)
        return (int)(100.0 * (double)(swap_total - swap_free) / (double)swap_total + 0.5);
    return 0;
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

/* Einheit für Geschwindigkeit bestimmen */
static const char *speed_unit(double kbps) {
    if (kbps >= 1048576.0)
        return "GB/s";
    else if (kbps >= 1024.0)
        return "MB/s";
    else
        return "KB/s";
}

/* Zahlenwert der Geschwindigkeit (in passender Einheit, 1 Nachkommastelle) */
static void format_speed_value(double kbps, char *buf, size_t len) {
    if (kbps >= 1048576.0)
        snprintf(buf, len, "%.1f", kbps / 1048576.0);
    else if (kbps >= 1024.0)
        snprintf(buf, len, "%.1f", kbps / 1024.0);
    else
        snprintf(buf, len, "%.1f", kbps);
}

/* Formatierte Datenmenge als String (KB, MB, GB, TB) – immer 2 Nachkommastellen */
static void format_bytes(unsigned long long bytes, char *buf, size_t len) {
    double val = (double)bytes;
    if (val >= 1099511627776.0)
        snprintf(buf, len, "%.2fTB", val / 1099511627776.0);
    else if (val >= 1073741824.0)
        snprintf(buf, len, "%.2fGB", val / 1073741824.0);
    else if (val >= 1048576.0)
        snprintf(buf, len, "%.2fMB", val / 1048576.0);
    else
        snprintf(buf, len, "%.2fKB", val / 1024.0);
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
    double dl_history[HISTORY_SIZE], ul_history[HISTORY_SIZE];
    int dl_count = 0, ul_count = 0;
    double cpu_history[HISTORY_SIZE], cpu_temp_history[HISTORY_SIZE];
    int cpu_count = 0, cpu_temp_count = 0;
    double gpu_history[HISTORY_SIZE], gpu_temp_history[HISTORY_SIZE];
    int gpu_count = 0, gpu_temp_count = 0;
    double ram_history[HISTORY_SIZE], swap_history[HISTORY_SIZE];
    int ram_count = 0, swap_count = 0;
    int first_read = 1;
    char dl_str[32], ul_str[32];
    char dl_total_str[32], ul_total_str[32];
    char title[128];
    unsigned int key_state = 0;
    unsigned int prev_key_state = 0;

    if (argc > 1)
        iface = argv[1];

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    memset(dl_history, 0, sizeof(dl_history));
    memset(ul_history, 0, sizeof(ul_history));
    memset(cpu_history, 0, sizeof(cpu_history));
    memset(cpu_temp_history, 0, sizeof(cpu_temp_history));
    memset(gpu_history, 0, sizeof(gpu_history));
    memset(gpu_temp_history, 0, sizeof(gpu_temp_history));
    memset(ram_history, 0, sizeof(ram_history));
    memset(swap_history, 0, sizeof(swap_history));

    /* Zum g15daemon verbinden */
    g15_fd = new_g15_screen(G15_G15RBUF);
    if (g15_fd < 0) {
        fprintf(stderr, "Fehler: Kann nicht zum g15daemon verbinden.\n");
        fprintf(stderr, "Ist g15daemon gestartet?\n");
        return 1;
    }

    g15r_initCanvas(&canvas);


    /* Initiales Lesen der Bytes */
    fprintf(stderr, "Verbinde mit g15daemon... OK (fd=%d)\n", g15_fd);

    if (read_net_bytes(iface, &prev_rx, &prev_tx) != 0) {
        fprintf(stderr, "Fehler: Interface '%s' nicht gefunden in /proc/net/dev\n", iface);
        g15_close_screen(g15_fd);
        return 1;
    }

    fprintf(stderr, "g15netspeed gestartet für Interface: '%s'\n", iface);
    fprintf(stderr, "L1-Taste (1. LCD-Taste): Seiten umschalten (Netz/CPU/GPU/RAM)\n");
    fprintf(stderr, "Drücke Ctrl+C zum Beenden.\n");

    while (running) {
        struct timespec ts;
        ts.tv_sec = UPDATE_MS / 1000;
        ts.tv_nsec = (UPDATE_MS % 1000) * 1000000L;
        nanosleep(&ts, NULL);

        /* Tastenerkennung (nicht-blockierend via select) */
        {
            fd_set readfds;
            struct timeval tv = {0, 0};  /* sofort zurückkehren */
            FD_ZERO(&readfds);
            FD_SET(g15_fd, &readfds);
            if (select(g15_fd + 1, &readfds, NULL, NULL, &tv) > 0) {
                int ret = recv(g15_fd, (char *)&key_state, sizeof(key_state), 0);
                if (ret == sizeof(key_state)) {
                    if ((key_state & L1_KEY) && !(prev_key_state & L1_KEY)) {
                        current_page = (current_page + 1) % NUM_PAGES;
                        fprintf(stderr, "Seite gewechselt: %d\n", current_page);
                    }
                    prev_key_state = key_state;
                }
            }
        }

        /* === Netzwerk-Daten immer sammeln === */
        if (read_net_bytes(iface, &curr_rx, &curr_tx) != 0)
            continue;

        if (first_read) {
            prev_rx = curr_rx;
            prev_tx = curr_tx;
            first_read = 0;
            continue;
        }

        double dl_kbps = (double)(curr_rx - prev_rx) / 1024.0 * (1000.0 / UPDATE_MS);
        double ul_kbps = (double)(curr_tx - prev_tx) / 1024.0 * (1000.0 / UPDATE_MS);
        prev_rx = curr_rx;
        prev_tx = curr_tx;

        push_history(dl_history, &dl_count, HISTORY_SIZE, dl_kbps);
        push_history(ul_history, &ul_count, HISTORY_SIZE, ul_kbps);

        /* === System-Daten immer sammeln === */
        int cpu_pct = read_cpu_usage();
        int cpu_temp = read_cpu_temp();
        int ram_pct = read_ram_usage();
        int gpu_pct = read_gpu_usage();
        int gpu_temp = read_gpu_temp();
        int swap_pct = read_swap_usage();

        push_history(cpu_history, &cpu_count, HISTORY_SIZE, (double)cpu_pct);
        push_history(cpu_temp_history, &cpu_temp_count, HISTORY_SIZE, (double)cpu_temp);
        push_history(gpu_history, &gpu_count, HISTORY_SIZE, (double)(gpu_pct >= 0 ? gpu_pct : 0));
        push_history(gpu_temp_history, &gpu_temp_count, HISTORY_SIZE, (double)gpu_temp);
        push_history(ram_history, &ram_count, HISTORY_SIZE, (double)ram_pct);
        push_history(swap_history, &swap_count, HISTORY_SIZE, (double)swap_pct);

        /* === Canvas leeren === */
        g15r_clearScreen(&canvas, G15_COLOR_WHITE);

        /* === Aktive Seite zeichnen === */
        if (current_page == 0) {
            /* --- Seite 0: Netzwerk --- */
            double dl_max = find_max(dl_history, dl_count);
            double ul_max = find_max(ul_history, ul_count);
            if (dl_max < 10.0) dl_max = 10.0;
            if (ul_max < 10.0) ul_max = 10.0;

            format_speed_value(dl_kbps, dl_str, sizeof(dl_str));
            format_speed_value(ul_kbps, ul_str, sizeof(ul_str));
            format_bytes(curr_rx, dl_total_str, sizeof(dl_total_str));
            format_bytes(curr_tx, ul_total_str, sizeof(ul_total_str));

            /* Kopfzeile: Interface + Gesamtdatenmenge */
            {
                char iface_short[8];
                snprintf(iface_short, sizeof(iface_short), "%s", iface);
                g15r_renderString(&canvas, (unsigned char *)iface_short, 0, G15_TEXT_MED, 0, 0);
            }
            snprintf(title, sizeof(title), "D:%s U:%s", dl_total_str, ul_total_str);
            g15r_renderString(&canvas, (unsigned char *)title, 0, G15_TEXT_MED,
                              LCD_WIDTH - (int)strlen(title) * 5, 0);

            /* Download-Graph */
            {
                char dl_label[16];
                snprintf(dl_label, sizeof(dl_label), "DL(%s)", speed_unit(dl_kbps));
                g15r_renderString(&canvas, (unsigned char *)dl_label, 0, G15_TEXT_SMALL, 1, 10);
            }
            g15r_renderString(&canvas, (unsigned char *)dl_str, 0, G15_TEXT_SMALL, 1, 18);
            draw_graph(&canvas, dl_history, dl_count,
                       GRAPH_X, 10, GRAPH_WIDTH, GRAPH_HEIGHT, dl_max, 0);

            /* Trennlinie */
            g15r_drawLine(&canvas, 0, 27, LCD_WIDTH - 1, 27, G15_COLOR_BLACK);

            /* Upload-Graph (invertiert) */
            {
                char ul_label[16];
                snprintf(ul_label, sizeof(ul_label), "UL(%s)", speed_unit(ul_kbps));
                g15r_renderString(&canvas, (unsigned char *)ul_label, 0, G15_TEXT_SMALL, 1, 29);
            }
            g15r_renderString(&canvas, (unsigned char *)ul_str, 0, G15_TEXT_SMALL, 1, 36);
            draw_graph(&canvas, ul_history, ul_count,
                       GRAPH_X, 28, GRAPH_WIDTH, 14, ul_max, 1);

        } else if (current_page == 1) {
            /* --- Seite 1: CPU --- */
            double cpu_max = 100.0;
            double temp_max = find_max(cpu_temp_history, cpu_temp_count);
            if (temp_max < 50.0) temp_max = 100.0;
            else temp_max = temp_max * 1.2;

            /* Kopfzeile */
            snprintf(title, sizeof(title), "CPU: %d%%  Temp: %dC", cpu_pct, cpu_temp);
            g15r_renderString(&canvas, (unsigned char *)title, 0, G15_TEXT_MED, 0, 0);

            /* CPU-Auslastung Graph */
            {
                char pct_str[16];
                snprintf(pct_str, sizeof(pct_str), "%d%%", cpu_pct);
                g15r_renderString(&canvas, (unsigned char *)"CPU", 0, G15_TEXT_SMALL, 1, 10);
                g15r_renderString(&canvas, (unsigned char *)pct_str, 0, G15_TEXT_SMALL, 1, 18);
            }
            draw_graph(&canvas, cpu_history, cpu_count,
                       GRAPH_X, 10, GRAPH_WIDTH, GRAPH_HEIGHT, cpu_max, 0);

            /* Trennlinie */
            g15r_drawLine(&canvas, 0, 27, LCD_WIDTH - 1, 27, G15_COLOR_BLACK);

            /* CPU-Temperatur Graph (invertiert) */
            {
                char temp_str[16];
                snprintf(temp_str, sizeof(temp_str), "%dC", cpu_temp);
                g15r_renderString(&canvas, (unsigned char *)"TMP", 0, G15_TEXT_SMALL, 1, 29);
                g15r_renderString(&canvas, (unsigned char *)temp_str, 0, G15_TEXT_SMALL, 1, 36);
            }
            draw_graph(&canvas, cpu_temp_history, cpu_temp_count,
                       GRAPH_X, 28, GRAPH_WIDTH, 14, temp_max, 1);

        } else if (current_page == 2) {
            /* --- Seite 2: GPU --- */
            double gpu_max = 100.0;
            double gtemp_max = find_max(gpu_temp_history, gpu_temp_count);
            if (gtemp_max < 50.0) gtemp_max = 100.0;
            else gtemp_max = gtemp_max * 1.2;

            /* Kopfzeile */
            if (gpu_pct >= 0)
                snprintf(title, sizeof(title), "GPU: %d%%  Temp: %dC", gpu_pct, gpu_temp);
            else
                snprintf(title, sizeof(title), "GPU: N/A  Temp: %dC", gpu_temp);
            g15r_renderString(&canvas, (unsigned char *)title, 0, G15_TEXT_MED, 0, 0);

            /* GPU-Auslastung Graph */
            {
                char pct_str[16];
                if (gpu_pct >= 0)
                    snprintf(pct_str, sizeof(pct_str), "%d%%", gpu_pct);
                else
                    snprintf(pct_str, sizeof(pct_str), "N/A");
                g15r_renderString(&canvas, (unsigned char *)"GPU", 0, G15_TEXT_SMALL, 1, 10);
                g15r_renderString(&canvas, (unsigned char *)pct_str, 0, G15_TEXT_SMALL, 1, 18);
            }
            draw_graph(&canvas, gpu_history, gpu_count,
                       GRAPH_X, 10, GRAPH_WIDTH, GRAPH_HEIGHT, gpu_max, 0);

            /* Trennlinie */
            g15r_drawLine(&canvas, 0, 27, LCD_WIDTH - 1, 27, G15_COLOR_BLACK);

            /* GPU-Temperatur Graph (invertiert) */
            {
                char temp_str[16];
                snprintf(temp_str, sizeof(temp_str), "%dC", gpu_temp);
                g15r_renderString(&canvas, (unsigned char *)"TMP", 0, G15_TEXT_SMALL, 1, 29);
                g15r_renderString(&canvas, (unsigned char *)temp_str, 0, G15_TEXT_SMALL, 1, 36);
            }
            draw_graph(&canvas, gpu_temp_history, gpu_temp_count,
                       GRAPH_X, 28, GRAPH_WIDTH, 14, gtemp_max, 1);

        } else if (current_page == 3) {
            /* --- Seite 3: RAM --- */
            double ram_max = 100.0;
            double swap_max = 100.0;

            /* Kopfzeile */
            snprintf(title, sizeof(title), "RAM: %d%%  Swap: %d%%", ram_pct, swap_pct);
            g15r_renderString(&canvas, (unsigned char *)title, 0, G15_TEXT_MED, 0, 0);

            /* RAM-Auslastung Graph */
            {
                char pct_str[16];
                snprintf(pct_str, sizeof(pct_str), "%d%%", ram_pct);
                g15r_renderString(&canvas, (unsigned char *)"RAM", 0, G15_TEXT_SMALL, 1, 10);
                g15r_renderString(&canvas, (unsigned char *)pct_str, 0, G15_TEXT_SMALL, 1, 18);
            }
            draw_graph(&canvas, ram_history, ram_count,
                       GRAPH_X, 10, GRAPH_WIDTH, GRAPH_HEIGHT, ram_max, 0);

            /* Trennlinie */
            g15r_drawLine(&canvas, 0, 27, LCD_WIDTH - 1, 27, G15_COLOR_BLACK);

            /* Swap-Auslastung Graph (invertiert) */
            {
                char pct_str[16];
                snprintf(pct_str, sizeof(pct_str), "%d%%", swap_pct);
                g15r_renderString(&canvas, (unsigned char *)"SWP", 0, G15_TEXT_SMALL, 1, 29);
                g15r_renderString(&canvas, (unsigned char *)pct_str, 0, G15_TEXT_SMALL, 1, 36);
            }
            draw_graph(&canvas, swap_history, swap_count,
                       GRAPH_X, 28, GRAPH_WIDTH, 14, swap_max, 1);
        }

        /* An g15daemon senden */
        g15_send(g15_fd, (char *)canvas.buffer, G15_BUFFER_LEN);
    }

    /* Aufräumen: Display leeren */
    g15r_clearScreen(&canvas, G15_COLOR_WHITE);
    g15_send(g15_fd, (char *)canvas.buffer, G15_BUFFER_LEN);
    g15_close_screen(g15_fd);

    fprintf(stderr, "\ng15netspeed beendet. (Letzte Seite: %d)\n", current_page);
    return 0;
}
