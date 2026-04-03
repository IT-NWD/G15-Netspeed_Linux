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
#include <unistd.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <g15daemon_client.h>

static volatile int running = 1;

static void signal_handler(int sig) {
    (void)sig;
    running = 0;
}

int main(void) {
    int fd;
    unsigned int key_state;

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    fd = new_g15_screen(G15_G15RBUF);
    if (fd < 0) {
        fprintf(stderr, "Fehler: Kann nicht zum g15daemon verbinden.\n");
        return 1;
    }

    printf("Verbunden mit g15daemon (fd=%d)\n", fd);
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
                    printf("Key-State: 0x%08X  (dezimal: %u)\n", key_state, key_state);
            }
        }
    }

    g15_close_screen(fd);
    printf("\nBeendet.\n");
    return 0;
}
