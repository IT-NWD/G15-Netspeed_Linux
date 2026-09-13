#include "g15netspeed/config.h"

#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <stdlib.h>
#include <string.h>

static char *trim(char *text) {
    char *end;

    while (isspace((unsigned char)*text))
        text++;
    if (*text == '\0')
        return text;
    end = text + strlen(text) - 1;
    while (end > text && isspace((unsigned char)*end))
        *end-- = '\0';
    return text;
}

static int parse_integer(const char *text, int minimum, int maximum, int *value) {
    char *end;
    long parsed;

    errno = 0;
    parsed = strtol(text, &end, 10);
    if (errno != 0 || *text == '\0' || *end != '\0' ||
        parsed < minimum || parsed > maximum)
        return -1;
    *value = (int)parsed;
    return 0;
}

static int parse_boolean(const char *text, int *value) {
    if (strcmp(text, "true") == 0 || strcmp(text, "yes") == 0 ||
        strcmp(text, "1") == 0) {
        *value = 1;
        return 0;
    }
    if (strcmp(text, "false") == 0 || strcmp(text, "no") == 0 ||
        strcmp(text, "0") == 0) {
        *value = 0;
        return 0;
    }
    return -1;
}

static int set_interface(G15Config *config, const char *value) {
    if (strcmp(value, "auto") == 0) {
        config->automatic_interface = 1;
        config->interface[0] = '\0';
        return 0;
    }
    if (*value == '\0' || strlen(value) >= sizeof(config->interface))
        return -1;
    config->automatic_interface = 0;
    snprintf(config->interface, sizeof(config->interface), "%s", value);
    return 0;
}

static int set_scale(G15Config *config, const char *value) {
    if (strcmp(value, "adaptive") == 0) {
        config->adaptive_scale = 1;
        return 0;
    }
    if (strcmp(value, "maximum") == 0) {
        config->adaptive_scale = 0;
        return 0;
    }
    return -1;
}

void g15_config_defaults(G15Config *config) {
    memset(config, 0, sizeof(*config));
    config->automatic_interface = 1;
    config->refresh_ms = G15_DEFAULT_REFRESH_MS;
    config->reconnect_ms = G15_DEFAULT_RECONNECT_MS;
    config->adaptive_scale = 1;
}

int g15_config_load_file(const char *path, G15Config *config,
                         char *error, size_t error_length) {
    FILE *stream = fopen(path, "r");
    char line[512];
    unsigned int line_number = 0;

    if (!stream) {
        snprintf(error, error_length, "Konfiguration '%s' kann nicht gelesen werden: %s",
                 path, strerror(errno));
        return -1;
    }

    while (fgets(line, sizeof(line), stream)) {
        char *key, *value, *separator;
        int valid = 0;

        line_number++;
        key = trim(line);
        if (*key == '\0' || *key == '#')
            continue;
        separator = strchr(key, '=');
        if (!separator) {
            snprintf(error, error_length, "%s:%u: '=' fehlt", path, line_number);
            fclose(stream);
            return -1;
        }
        *separator = '\0';
        value = trim(separator + 1);
        key = trim(key);

        if (strcmp(key, "interface") == 0)
            valid = set_interface(config, value) == 0;
        else if (strcmp(key, "refresh_ms") == 0)
            valid = parse_integer(value, G15_MIN_REFRESH_MS,
                                  G15_MAX_REFRESH_MS, &config->refresh_ms) == 0;
        else if (strcmp(key, "reconnect_ms") == 0)
            valid = parse_integer(value, 1000, 60000, &config->reconnect_ms) == 0;
        else if (strcmp(key, "verbose") == 0)
            valid = parse_boolean(value, &config->verbose) == 0;
        else if (strcmp(key, "scale") == 0)
            valid = set_scale(config, value) == 0;
        else {
            snprintf(error, error_length, "%s:%u: unbekannte Option '%s'",
                     path, line_number, key);
            fclose(stream);
            return -1;
        }

        if (!valid) {
            snprintf(error, error_length, "%s:%u: ungültiger Wert für '%s'",
                     path, line_number, key);
            fclose(stream);
            return -1;
        }
    }

    if (ferror(stream)) {
        snprintf(error, error_length, "Fehler beim Lesen von '%s'", path);
        fclose(stream);
        return -1;
    }
    fclose(stream);
    return 0;
}

static int default_config_path(char *path, size_t length) {
    const char *xdg = getenv("XDG_CONFIG_HOME");
    const char *user_home = getenv("HOME");

    if (xdg && *xdg) {
        return snprintf(path, length, "%s/g15netspeed/config", xdg) < (int)length
                   ? 0 : -1;
    }
    if (user_home && *user_home) {
        return snprintf(path, length, "%s/.config/g15netspeed/config", user_home) <
                       (int)length ? 0 : -1;
    }
    return -1;
}

static int discover_config(int argc, char **argv, char *path, size_t length,
                           int *enabled, int *explicit_path) {
    int i;

    *enabled = 1;
    *explicit_path = 0;
    path[0] = '\0';
    if (default_config_path(path, length) != 0)
        path[0] = '\0';

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--no-config") == 0 || strcmp(argv[i], "-n") == 0) {
            *enabled = 0;
        } else if ((strcmp(argv[i], "--config") == 0 || strcmp(argv[i], "-c") == 0) &&
                   i + 1 < argc) {
            if (snprintf(path, length, "%s", argv[++i]) >= (int)length)
                return -1;
            *explicit_path = 1;
            *enabled = 1;
        } else if (strncmp(argv[i], "--config=", 9) == 0) {
            if (snprintf(path, length, "%s", argv[i] + 9) >= (int)length)
                return -1;
            *explicit_path = 1;
            *enabled = 1;
        } else if (strncmp(argv[i], "-c", 2) == 0 && argv[i][2] != '\0') {
            if (snprintf(path, length, "%s", argv[i] + 2) >= (int)length)
                return -1;
            *explicit_path = 1;
            *enabled = 1;
        }
    }
    return 0;
}

int g15_config_prepare(int argc, char **argv, G15Config *config,
                       G15Action *action, char *error, size_t error_length) {
    static const struct option options[] = {
        {"interface", required_argument, NULL, 'i'},
        {"refresh", required_argument, NULL, 'r'},
        {"reconnect", required_argument, NULL, 'R'},
        {"scale", required_argument, NULL, 's'},
        {"config", required_argument, NULL, 'c'},
        {"no-config", no_argument, NULL, 'n'},
        {"list-interfaces", no_argument, NULL, 'l'},
        {"verbose", no_argument, NULL, 'v'},
        {"version", no_argument, NULL, 'V'},
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0}
    };
    char config_path[G15_PATH_SIZE];
    int config_enabled, explicit_path;
    int option, interface_option = 0;

    g15_config_defaults(config);
    *action = G15_ACTION_RUN;
    if (discover_config(argc, argv, config_path, sizeof(config_path),
                        &config_enabled, &explicit_path) != 0) {
        snprintf(error, error_length, "Pfad der Konfigurationsdatei ist zu lang");
        return -1;
    }
    if (config_enabled && config_path[0] != '\0') {
        FILE *probe = fopen(config_path, "r");
        if (probe) {
            fclose(probe);
            if (g15_config_load_file(config_path, config, error, error_length) != 0)
                return -1;
        } else if (explicit_path || errno != ENOENT) {
            snprintf(error, error_length, "Konfiguration '%s' kann nicht gelesen werden: %s",
                     config_path, strerror(errno));
            return -1;
        }
    }

    optind = 1;
    opterr = 0;
    while ((option = getopt_long(argc, argv, "i:r:R:s:c:nlvVh", options, NULL)) != -1) {
        switch (option) {
            case 'i':
                interface_option = 1;
                if (set_interface(config, optarg) != 0)
                    goto invalid_interface;
                break;
            case 'r':
                if (parse_integer(optarg, G15_MIN_REFRESH_MS, G15_MAX_REFRESH_MS,
                                  &config->refresh_ms) != 0) {
                    snprintf(error, error_length,
                             "--refresh erwartet %d bis %d ms",
                             G15_MIN_REFRESH_MS, G15_MAX_REFRESH_MS);
                    return -1;
                }
                break;
            case 'R':
                if (parse_integer(optarg, 1000, 60000, &config->reconnect_ms) != 0) {
                    snprintf(error, error_length,
                             "--reconnect erwartet 1000 bis 60000 ms");
                    return -1;
                }
                break;
            case 's':
                if (set_scale(config, optarg) != 0) {
                    snprintf(error, error_length,
                             "--scale erwartet 'adaptive' oder 'maximum'");
                    return -1;
                }
                break;
            case 'c':
            case 'n':
                break;
            case 'l':
                *action = G15_ACTION_LIST_INTERFACES;
                break;
            case 'v':
                config->verbose = 1;
                break;
            case 'V':
                *action = G15_ACTION_VERSION;
                break;
            case 'h':
                *action = G15_ACTION_HELP;
                break;
            default:
                snprintf(error, error_length, "Unbekannte oder unvollständige Option");
                return -1;
        }
    }

    if (optind < argc) {
        if (interface_option || optind + 1 != argc) {
            snprintf(error, error_length,
                     "Genau ein Interface mit -i oder als Positionsargument angeben");
            return -1;
        }
        if (set_interface(config, argv[optind]) != 0)
            goto invalid_interface;
    }
    return 0;

invalid_interface:
    snprintf(error, error_length, "Ungültiger oder zu langer Interface-Name");
    return -1;
}

void g15_config_print_usage(FILE *stream, const char *program) {
    fprintf(stream,
            "Verwendung: %s [OPTIONEN] [INTERFACE]\n\n"
            "  -i, --interface NAME   Interface oder 'auto' (Standard)\n"
            "  -r, --refresh MS       Aktualisierung (%d-%d ms)\n"
            "  -R, --reconnect MS     Wiederverbindung (1000-60000 ms)\n"
            "  -s, --scale MODUS      'adaptive' oder 'maximum'\n"
            "  -c, --config DATEI     Alternative Konfiguration laden\n"
            "  -n, --no-config        Konfigurationsdatei ignorieren\n"
            "  -l, --list-interfaces  Interfaces auflisten\n"
            "  -v, --verbose          Messwerte ausgeben\n"
            "  -V, --version          Version anzeigen\n"
            "  -h, --help             Hilfe anzeigen\n",
            program, G15_MIN_REFRESH_MS, G15_MAX_REFRESH_MS);
}
