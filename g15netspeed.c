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
 *   ./g15netspeed [--interface INTERFACE] [--refresh MILLISEKUNDEN]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <getopt.h>
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
#define DEFAULT_UPDATE_MS 150
#define MIN_UPDATE_MS      50
#define MAX_UPDATE_MS      5000
#define IFACE_NAME_SIZE    128

static volatile sig_atomic_t running = 1;

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

static void signal_handler(int sig) {
    (void)sig;
    running = 0;
}

static void print_usage(FILE *stream, const char *program) {
    fprintf(stream,
            "Verwendung: %s [OPTIONEN] [INTERFACE]\n"
            "\n"
            "Zeigt Netzwerk-, CPU- und RAM-Daten auf dem Logitech-G15-LCD.\n"
            "Ohne Interface wird das Interface der IPv4-Standardroute verwendet.\n"
            "\n"
            "  -i, --interface NAME  Netzwerk-Interface oder 'auto' (Standard)\n"
            "  -r, --refresh MS      Aktualisierungsrate in Millisekunden (%d-%d)\n"
            "  -l, --list-interfaces Verfügbare Interfaces auflisten und beenden\n"
            "  -v, --verbose         Messwerte fortlaufend ausgeben\n"
            "  -h, --help            Diese Hilfe anzeigen\n",
            program, MIN_UPDATE_MS, MAX_UPDATE_MS);
}

static int list_interfaces(void) {
    FILE *fp;
    char line[512];

    fp = fopen("/proc/net/dev", "r");
    if (!fp) {
        perror("Fehler beim Auflisten der Netzwerk-Interfaces");
        return -1;
    }

    if (!fgets(line, sizeof(line), fp) || !fgets(line, sizeof(line), fp)) {
        fclose(fp);
        return -1;
    }

    while (fgets(line, sizeof(line), fp)) {
        char name[IFACE_NAME_SIZE];

        if (sscanf(line, " %127[^:]:", name) == 1)
            puts(name);
    }

    fclose(fp);
    return 0;
}

/* Bevorzugt das Interface der IPv4-Standardroute. */
static int detect_default_interface(char *iface, size_t len) {
    FILE *fp;
    char line[256];

    fp = fopen("/proc/net/route", "r");
    if (fp) {
        if (fgets(line, sizeof(line), fp)) {
            while (fgets(line, sizeof(line), fp)) {
                char name[IFACE_NAME_SIZE];
                unsigned long destination, gateway, flags;

                if (sscanf(line, "%127s %lx %lx %lx",
                           name, &destination, &gateway, &flags) == 4 &&
                    destination == 0 && (flags & 0x1UL) != 0 &&
                    strcmp(name, "lo") != 0) {
                    snprintf(iface, len, "%s", name);
                    fclose(fp);
                    return 0;
                }
            }
        }
        fclose(fp);
    }

    /* Fallback für Systeme ohne IPv4-Standardroute. */
    fp = fopen("/proc/net/dev", "r");
    if (fp) {
        if (!fgets(line, sizeof(line), fp) || !fgets(line, sizeof(line), fp)) {
            fclose(fp);
            return -1;
        }

        while (fgets(line, sizeof(line), fp)) {
            char name[IFACE_NAME_SIZE];
            char path[256], state[32];
            FILE *state_fp;

            if (sscanf(line, " %127[^:]:", name) != 1 || strcmp(name, "lo") == 0)
                continue;

            snprintf(path, sizeof(path), "/sys/class/net/%s/operstate", name);
            state_fp = fopen(path, "r");
            if (state_fp && fgets(state, sizeof(state), state_fp) &&
                (strncmp(state, "up", 2) == 0 || strncmp(state, "unknown", 7) == 0)) {
                snprintf(iface, len, "%s", name);
                fclose(state_fp);
                fclose(fp);
                return 0;
            }
            if (state_fp)
                fclose(state_fp);
        }
        fclose(fp);
    }

    return -1;
}

static int parse_refresh_ms(const char *value, int *refresh_ms) {
    char *end;
    long parsed;

    errno = 0;
    parsed = strtol(value, &end, 10);
    if (errno != 0 || *value == '\0' || *end != '\0' ||
        parsed < MIN_UPDATE_MS || parsed > MAX_UPDATE_MS)
        return -1;

    *refresh_ms = (int)parsed;
    return 0;
}

static void sleep_ms(int milliseconds) {
    struct timespec remaining;

    remaining.tv_sec = milliseconds / 1000;
    remaining.tv_nsec = (milliseconds % 1000) * 1000000L;
    while (running && nanosleep(&remaining, &remaining) != 0 && errno == EINTR)
        ;
}

static double elapsed_seconds(const struct timespec *before,
                              const struct timespec *after) {
    return (double)(after->tv_sec - before->tv_sec) +
           (double)(after->tv_nsec - before->tv_nsec) / 1000000000.0;
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
                   " %127[^:]: %llu %llu %llu %llu %llu %llu %llu %llu "
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
    static const struct option long_options[] = {
        {"interface", required_argument, NULL, 'i'},
        {"refresh", required_argument, NULL, 'r'},
        {"list-interfaces", no_argument, NULL, 'l'},
        {"verbose", no_argument, NULL, 'v'},
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0}
    };
    char iface[IFACE_NAME_SIZE] = "";
    int automatic_iface = 1;
    int interface_option_set = 0;
    int update_ms = DEFAULT_UPDATE_MS;
    int verbose = 0;
    int option;
    int g15_fd;
    g15canvas canvas;
    unsigned long long prev_rx = 0, prev_tx = 0;
    unsigned long long curr_rx, curr_tx;
    struct timespec previous_sample, current_sample;
    double dl_history[HISTORY_SIZE];
    double ul_history[HISTORY_SIZE];
    int dl_count = 0, ul_count = 0;
    char dl_str[32], ul_str[32];
    char title[128];

    while ((option = getopt_long(argc, argv, "i:r:lvh", long_options, NULL)) != -1) {
        switch (option) {
            case 'i':
                interface_option_set = 1;
                automatic_iface = strcmp(optarg, "auto") == 0;
                if (!automatic_iface) {
                    if (strlen(optarg) >= sizeof(iface)) {
                        fprintf(stderr, "Fehler: Interface-Name ist zu lang.\n");
                        return 2;
                    }
                    snprintf(iface, sizeof(iface), "%s", optarg);
                }
                break;
            case 'r':
                if (parse_refresh_ms(optarg, &update_ms) != 0) {
                    fprintf(stderr, "Fehler: --refresh erwartet einen Wert zwischen %d und %d ms.\n",
                            MIN_UPDATE_MS, MAX_UPDATE_MS);
                    return 2;
                }
                break;
            case 'l':
                return list_interfaces() == 0 ? 0 : 1;
            case 'v':
                verbose = 1;
                break;
            case 'h':
                print_usage(stdout, argv[0]);
                return 0;
            default:
                print_usage(stderr, argv[0]);
                return 2;
        }
    }

    if (optind < argc) {
        if (interface_option_set || optind + 1 != argc) {
            fprintf(stderr, "Fehler: Bitte genau ein Interface mit -i oder als Positionsargument angeben.\n");
            print_usage(stderr, argv[0]);
            return 2;
        }
        if (strlen(argv[optind]) >= sizeof(iface)) {
            fprintf(stderr, "Fehler: Interface-Name ist zu lang.\n");
            return 2;
        }
        snprintf(iface, sizeof(iface), "%s", argv[optind]);
        automatic_iface = 0;
    }

    if (automatic_iface && detect_default_interface(iface, sizeof(iface)) != 0) {
        fprintf(stderr, "Fehler: Kein geeignetes Netzwerk-Interface gefunden.\n");
        fprintf(stderr, "Verfügbare Interfaces zeigt '%s --list-interfaces'.\n", argv[0]);
        return 1;
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    memset(dl_history, 0, sizeof(dl_history));
    memset(ul_history, 0, sizeof(ul_history));

    /* Initiales Lesen der Bytes */
    if (read_net_bytes(iface, &prev_rx, &prev_tx) != 0) {
        fprintf(stderr, "Fehler: Interface '%s' nicht gefunden in /proc/net/dev\n", iface);
        return 1;
    }

    if (clock_gettime(CLOCK_MONOTONIC, &previous_sample) != 0) {
        perror("Fehler beim Lesen der Systemzeit");
        return 1;
    }

    /* Zum g15daemon verbinden */
    g15_fd = new_g15_screen(G15_G15RBUF);
    if (g15_fd < 0) {
        fprintf(stderr, "Fehler: Kann nicht zum g15daemon verbinden.\n");
        fprintf(stderr, "Ist g15daemon gestartet?\n");
        return 1;
    }

    g15r_initCanvas(&canvas);

    printf("g15netspeed gestartet für Interface '%s' (%d ms%s).\n",
           iface, update_ms, automatic_iface ? ", automatisch" : "");
    printf("Drücke Ctrl+C zum Beenden.\n");

    while (running) {
        double sample_seconds;

        sleep_ms(update_ms);
        if (!running)
            break;

        if (read_net_bytes(iface, &curr_rx, &curr_tx) != 0)
            continue;

        if (clock_gettime(CLOCK_MONOTONIC, &current_sample) != 0) {
            perror("Fehler beim Lesen der Systemzeit");
            break;
        }

        sample_seconds = elapsed_seconds(&previous_sample, &current_sample);
        if (sample_seconds <= 0.0) {
            previous_sample = current_sample;
            continue;
        }

        /* Ein Reset des Interfaces darf keinen künstlichen Traffic-Peak erzeugen. */
        if (curr_rx < prev_rx || curr_tx < prev_tx) {
            fprintf(stderr, "Hinweis: Netzwerkzähler von '%s' wurden zurückgesetzt.\n", iface);
            prev_rx = curr_rx;
            prev_tx = curr_tx;
            previous_sample = current_sample;
            continue;
        }

        /* KB/s anhand der tatsächlich vergangenen Zeit berechnen. */
        double dl_kbps = (double)(curr_rx - prev_rx) / 1024.0 / sample_seconds;
        double ul_kbps = (double)(curr_tx - prev_tx) / 1024.0 / sample_seconds;

        if (verbose) {
            printf("DL: %.2f KB/s  UL: %.2f KB/s  (%.3f s, rx=%llu tx=%llu)\n",
                   dl_kbps, ul_kbps, sample_seconds, curr_rx, curr_tx);
        }

        prev_rx = curr_rx;
        prev_tx = curr_tx;
        previous_sample = current_sample;

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
        format_speed(ul_kbps, ul_str, sizeof(ul_str));

        /* System-Auslastung lesen */
        int cpu_pct = read_cpu_usage();
        int ram_pct = read_ram_usage();

        /* Canvas leeren und zeichnen */
        g15r_clearScreen(&canvas, G15_COLOR_WHITE);

        /* Zeile 0: Interface links, CPU/RAM rechts. Die GPU-Abfrage ist bewusst deaktiviert. */
        {
            char iface_short[8];
            snprintf(iface_short, sizeof(iface_short), "%s", iface);
            g15r_renderString(&canvas, (unsigned char *)iface_short, 0, G15_TEXT_MED, 0, 0);
        }
        snprintf(title, sizeof(title), "CPU:%d%% RAM:%d%%", cpu_pct, ram_pct);
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
        if (g15_send(g15_fd, (char *)canvas.buffer, G15_BUFFER_LEN) < 0) {
            fprintf(stderr, "Fehler: Verbindung zum g15daemon verloren.\n");
            break;
        }
    }

    /* Aufräumen: Display leeren */
    g15r_clearScreen(&canvas, G15_COLOR_WHITE);
    g15_send(g15_fd, (char *)canvas.buffer, G15_BUFFER_LEN);
    g15_close_screen(g15_fd);

    printf("\ng15netspeed beendet.\n");
    return 0;
}
