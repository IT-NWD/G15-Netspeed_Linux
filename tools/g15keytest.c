/*
 * g15keytest - Kleines Debug-Tool zum Auslesen der G15-Tastencodes
 *
 * Verbindet sich zum g15daemon und gibt empfangene Key-States aus.
 * Beenden mit Ctrl+C.
 *
 * Kompilieren:
 *   gcc -o g15keytest g15keytest.c -lg15daemon_client
 */

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <libg15.h>
#include <g15daemon_client.h>

static volatile sig_atomic_t running = 1;

static void signal_handler(int sig) {
    (void)sig;
    running = 0;
}

static const char *key_name(unsigned int key_state) {
    if (key_state & G15_KEY_L1) return "DISPLAY_L1";
    if (key_state & G15_KEY_L2) return "DISPLAY_L2";
    if (key_state & G15_KEY_L3) return "DISPLAY_L3";
    if (key_state & G15_KEY_L4) return "DISPLAY_L4";
    if (key_state & G15_KEY_L5) return "DISPLAY_L5";
    if (key_state & G15_KEY_M1) return "M1";
    if (key_state & G15_KEY_M2) return "M2";
    if (key_state & G15_KEY_M3) return "M3";
    if (key_state & G15_KEY_MR) return "MR";
    return "UNBEKANNT/MEHRERE";
}

int main(int argc, char **argv) {
    int fd;
    unsigned int key_state;
    int request_foreground = argc == 2 && strcmp(argv[1], "--foreground") == 0;

    if (argc > 2 || (argc == 2 && !request_foreground)) {
        fprintf(stderr, "Verwendung: %s [--foreground]\n", argv[0]);
        return 2;
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    fd = new_g15_screen(G15_G15RBUF);
    if (fd < 0) {
        fprintf(stderr, "Fehler: Kann nicht zum g15daemon verbinden.\n");
        return 1;
    }

    printf("Verbunden mit g15daemon (fd=%d)\n", fd);
    if (request_foreground &&
        g15_send_cmd(fd, G15DAEMON_IS_FOREGROUND, 0) == 0) {
        if ((long)g15_send_cmd(fd, G15DAEMON_SWITCH_PRIORITIES, 0) < 0) {
            fprintf(stderr, "Fehler: Vordergrundmodus konnte nicht angefordert werden.\n");
            g15_close_screen(fd);
            return 1;
        }
    }
    printf("Vordergrund: %s\n",
           g15_send_cmd(fd, G15DAEMON_IS_FOREGROUND, 0) != 0 ? "ja" : "nein");
    printf("Druecke Tasten auf der G15... (Ctrl+C zum Beenden)\n\n");

    while (running) {
        fd_set readfds;
        struct timeval tv;

        FD_ZERO(&readfds);
        FD_SET(fd, &readfds);
        tv.tv_sec = 1;
        tv.tv_usec = 0;

        if (select(fd + 1, &readfds, NULL, NULL, &tv) > 0) {
            int ret = recv(fd, (char *)&key_state, sizeof(key_state), 0);
            if (ret == (int)sizeof(key_state)) {
                if (key_state)
                    printf("Key-State: 0x%08X  %-20s (dezimal: %u)\n",
                           key_state, key_name(key_state), key_state);
            }
        }
    }

    g15_close_screen(fd);
    printf("\nBeendet.\n");
    return 0;
}
