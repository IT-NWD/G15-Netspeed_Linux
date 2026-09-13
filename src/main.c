#include "g15netspeed/config.h"
#include "g15netspeed/display.h"
#include "g15netspeed/metrics.h"
#include "g15netspeed/network.h"
#include "g15netspeed/version.h"

#include <errno.h>
#include <libg15.h>
#include <g15daemon_client.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t running = 1;

static void handle_signal(int signal_number) {
    (void)signal_number;
    running = 0;
}

static int install_signal_handlers(void) {
    struct sigaction action;

    memset(&action, 0, sizeof(action));
    action.sa_handler = handle_signal;
    sigemptyset(&action.sa_mask);
    return sigaction(SIGINT, &action, NULL) == 0 &&
           sigaction(SIGTERM, &action, NULL) == 0 ? 0 : -1;
}

static void sleep_milliseconds(int milliseconds) {
    struct timespec remaining;

    remaining.tv_sec = milliseconds / 1000;
    remaining.tv_nsec = (milliseconds % 1000) * 1000000L;
    while (running && nanosleep(&remaining, &remaining) != 0 && errno == EINTR)
        ;
}

static double seconds_between(const struct timespec *before,
                              const struct timespec *after) {
    return (double)(after->tv_sec - before->tv_sec) +
           (double)(after->tv_nsec - before->tv_nsec) / 1000000000.0;
}

static long long monotonic_milliseconds(void) {
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return 0;
    return (long long)now.tv_sec * 1000LL + now.tv_nsec / 1000000LL;
}

static int connect_daemon(int verbose) {
    const int descriptor = new_g15_screen(G15_G15RBUF);

    if (descriptor >= 0) {
        if (g15_send_cmd(descriptor, G15DAEMON_IS_FOREGROUND, 0) == 0)
            (void)g15_send_cmd(descriptor, G15DAEMON_SWITCH_PRIORITIES, 0);
        fprintf(stderr, "Mit g15daemon verbunden.\n");
    } else if (verbose) {
        fprintf(stderr, "g15daemon nicht erreichbar; neuer Versuch folgt.\n");
    }
    return descriptor;
}

static int poll_keys(int descriptor, unsigned int *key_state) {
    fd_set descriptors;
    struct timeval timeout = {0, 0};
    int ready;

    FD_ZERO(&descriptors);
    FD_SET(descriptor, &descriptors);
    ready = select(descriptor + 1, &descriptors, NULL, NULL, &timeout);
    if (ready < 0)
        return errno == EINTR ? 0 : -1;
    if (ready == 0)
        return 0;
    return recv(descriptor, key_state, sizeof(*key_state), MSG_DONTWAIT) ==
           (ssize_t)sizeof(*key_state) ? 1 : -1;
}

static int choose_relative_interface(char *interface, size_t length,
                                     G15NetworkCounters *baseline,
                                     int direction) {
    char interfaces[G15_MAX_INTERFACES][G15_IFACE_NAME_SIZE];
    const size_t count = g15_network_scan(interfaces, G15_MAX_INTERFACES, 0);
    size_t current = count;
    size_t step;

    if (count < 2)
        return -1;
    for (step = 0; step < count; step++) {
        if (strcmp(interfaces[step], interface) == 0) {
            current = step;
            break;
        }
    }

    for (step = 1; step <= count; step++) {
        size_t candidate;

        if (current == count)
            candidate = direction < 0 ? count - step : step - 1;
        else if (direction < 0)
            candidate = (current + count - (step % count)) % count;
        else
            candidate = (current + step) % count;

        if (g15_network_read(interfaces[candidate], baseline) == 0) {
            snprintf(interface, length, "%s", interfaces[candidate]);
            return 0;
        }
    }
    return -1;
}

static int recover_automatic_interface(char *interface, size_t length,
                                       G15NetworkCounters *baseline) {
    char detected[G15_IFACE_NAME_SIZE];

    if (g15_network_detect_default(detected, sizeof(detected)) != 0 ||
        g15_network_read(detected, baseline) != 0)
        return -1;
    if (strcmp(interface, detected) != 0)
        fprintf(stderr, "Automatisch auf Interface '%s' gewechselt.\n", detected);
    snprintf(interface, length, "%s", detected);
    return 0;
}

int main(int argc, char **argv) {
    G15Config config;
    G15Action action;
    char error[512];
    char interface[G15_IFACE_NAME_SIZE];
    G15NetworkCounters previous_counters, current_counters;
    G15MetricsState metrics_state;
    G15SystemMetrics metrics = {0};
    G15DisplayHistory histories;
    G15DisplayData display_data;
    G15Page page = G15_PAGE_NETWORK;
    g15canvas canvas;
    struct timespec previous_sample, current_sample;
    unsigned int previous_keys = 0;
    int descriptor = -1;
    long long next_reconnect = 0;
    int network_warning_shown = 0;

    if (g15_config_prepare(argc, argv, &config, &action,
                           error, sizeof(error)) != 0) {
        fprintf(stderr, "Fehler: %s\n", error);
        g15_config_print_usage(stderr, argv[0]);
        return 2;
    }
    if (action == G15_ACTION_HELP) {
        g15_config_print_usage(stdout, argv[0]);
        return 0;
    }
    if (action == G15_ACTION_VERSION) {
        printf("g15netspeed %s\n", G15NETSPEED_VERSION);
        return 0;
    }
    if (action == G15_ACTION_LIST_INTERFACES)
        return g15_network_print_interfaces(stdout) == 0 ? 0 : 1;

    if (config.automatic_interface) {
        if (g15_network_detect_default(interface, sizeof(interface)) != 0) {
            fprintf(stderr, "Fehler: Kein aktives Netzwerk-Interface gefunden.\n");
            return 1;
        }
    } else {
        snprintf(interface, sizeof(interface), "%s", config.interface);
    }

    if (g15_network_read(interface, &previous_counters) != 0) {
        fprintf(stderr, "Fehler: Interface '%s' ist nicht verfügbar.\n", interface);
        return 1;
    }
    if (clock_gettime(CLOCK_MONOTONIC, &previous_sample) != 0 ||
        install_signal_handlers() != 0) {
        perror("Initialisierung fehlgeschlagen");
        return 1;
    }

    g15_metrics_state_init(&metrics_state);
    (void)g15_metrics_read(&metrics_state, &metrics);
    g15_display_history_init(&histories);
    g15r_initCanvas(&canvas);

    fprintf(stderr, "g15netspeed: Interface '%s', Intervall %d ms, Skalierung %s.\n",
            interface, config.refresh_ms,
            config.adaptive_scale ? "adaptiv" : "Maximum");
    fprintf(stderr, "L2/L3: Seite zurück/weiter, L4/L5: Interface zurück/weiter.\n");

    while (running) {
        double elapsed, download_kbps, upload_kbps;
        const long long now_ms = monotonic_milliseconds();

        if (descriptor < 0 && now_ms >= next_reconnect) {
            descriptor = connect_daemon(config.verbose);
            if (descriptor < 0)
                next_reconnect = now_ms + config.reconnect_ms;
            previous_keys = 0;
        }

        sleep_milliseconds(config.refresh_ms);
        if (!running)
            break;

        if (g15_network_read(interface, &current_counters) != 0) {
            if (config.automatic_interface &&
                recover_automatic_interface(interface, sizeof(interface),
                                            &previous_counters) == 0) {
                g15_display_history_clear_network(&histories);
                (void)clock_gettime(CLOCK_MONOTONIC, &previous_sample);
                network_warning_shown = 0;
            } else if (!network_warning_shown) {
                fprintf(stderr, "Warnung: Interface '%s' ist nicht verfügbar.\n", interface);
                network_warning_shown = 1;
            }
            continue;
        }
        network_warning_shown = 0;

        if (clock_gettime(CLOCK_MONOTONIC, &current_sample) != 0)
            continue;
        elapsed = seconds_between(&previous_sample, &current_sample);
        if (elapsed <= 0.0)
            continue;

        if (current_counters.rx_bytes < previous_counters.rx_bytes ||
            current_counters.tx_bytes < previous_counters.tx_bytes) {
            fprintf(stderr, "Netzwerkzähler von '%s' wurden zurückgesetzt.\n", interface);
            previous_counters = current_counters;
            previous_sample = current_sample;
            g15_display_history_clear_network(&histories);
            continue;
        }

        download_kbps = (double)(current_counters.rx_bytes - previous_counters.rx_bytes) /
                        1024.0 / elapsed;
        upload_kbps = (double)(current_counters.tx_bytes - previous_counters.tx_bytes) /
                      1024.0 / elapsed;
        previous_counters = current_counters;
        previous_sample = current_sample;
        (void)g15_metrics_read(&metrics_state, &metrics);

        display_data.interface = interface;
        display_data.counters = current_counters;
        display_data.download_kbps = download_kbps;
        display_data.upload_kbps = upload_kbps;
        display_data.metrics = metrics;
        display_data.adaptive_scale = config.adaptive_scale;
        g15_display_history_push(&histories, &display_data);

        if (config.verbose) {
            if (metrics.cpu_temperature_available) {
                fprintf(stderr,
                        "%s: DL %.2f KB/s, UL %.2f KB/s, CPU %d%%/%dC, RAM %d%%, Swap %d%%\n",
                        interface, download_kbps, upload_kbps, metrics.cpu_percent,
                        metrics.cpu_temperature, metrics.ram_percent,
                        metrics.swap_percent);
            } else {
                fprintf(stderr,
                        "%s: DL %.2f KB/s, UL %.2f KB/s, CPU %d%%/N/A, RAM %d%%, Swap %d%%\n",
                        interface, download_kbps, upload_kbps, metrics.cpu_percent,
                        metrics.ram_percent, metrics.swap_percent);
            }
        }

        if (descriptor >= 0) {
            unsigned int keys = 0;
            const int key_result = poll_keys(descriptor, &keys);
            int interface_changed = 0;

            if (key_result < 0) {
                g15_close_screen(descriptor);
                descriptor = -1;
                next_reconnect = monotonic_milliseconds() + config.reconnect_ms;
                fprintf(stderr, "Verbindung zu g15daemon verloren.\n");
                continue;
            }
            if (key_result > 0) {
                if ((keys & G15_KEY_L2) && !(previous_keys & G15_KEY_L2)) {
                    page = (G15Page)((page + G15_PAGE_COUNT - 1) % G15_PAGE_COUNT);
                    fprintf(stderr, "Seite: %s.\n", g15_display_page_name(page));
                } else if ((keys & G15_KEY_L3) && !(previous_keys & G15_KEY_L3)) {
                    page = (G15Page)((page + 1) % G15_PAGE_COUNT);
                    fprintf(stderr, "Seite: %s.\n", g15_display_page_name(page));
                } else if (((keys & G15_KEY_L4) && !(previous_keys & G15_KEY_L4)) ||
                           ((keys & G15_KEY_L5) && !(previous_keys & G15_KEY_L5))) {
                    const int direction = (keys & G15_KEY_L4) ? -1 : 1;

                    if (choose_relative_interface(interface, sizeof(interface),
                                                  &previous_counters,
                                                  direction) == 0) {
                        config.automatic_interface = 0;
                        previous_sample = current_sample;
                        g15_display_history_clear_network(&histories);
                        fprintf(stderr, "Interface: %s.\n", interface);
                        interface_changed = 1;
                    }
                }
                previous_keys = keys;
                if (interface_changed)
                    continue;
            }

            g15_display_render(&canvas, page, &histories, &display_data);
            if (g15_send(descriptor, (char *)canvas.buffer, G15_BUFFER_LEN) < 0) {
                g15_close_screen(descriptor);
                descriptor = -1;
                next_reconnect = monotonic_milliseconds() + config.reconnect_ms;
                fprintf(stderr, "Senden an g15daemon fehlgeschlagen.\n");
            }
        }
    }

    if (descriptor >= 0) {
        g15r_clearScreen(&canvas, G15_COLOR_WHITE);
        (void)g15_send(descriptor, (char *)canvas.buffer, G15_BUFFER_LEN);
        g15_close_screen(descriptor);
    }
    fprintf(stderr, "g15netspeed beendet.\n");
    return 0;
}
