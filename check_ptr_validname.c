#include <arpa/inet.h>
#include <ctype.h>
#include <netdb.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#define STATE_OK 0
#define STATE_WARNING 1
#define STATE_CRITICAL 2
#define STATE_UNKNOWN 3

static void print_usage(const char *prog) {
    printf("Usage: %s -i <ip-address>\n", prog);
}

static bool parse_ip(const char *ip, struct sockaddr_storage *ss, socklen_t *ss_len, int *family) {
    struct sockaddr_in sa4;
    memset(&sa4, 0, sizeof(sa4));
    sa4.sin_family = AF_INET;

    if (inet_pton(AF_INET, ip, &sa4.sin_addr) == 1) {
        memcpy(ss, &sa4, sizeof(sa4));
        *ss_len = sizeof(sa4);
        *family = AF_INET;
        return true;
    }

    struct sockaddr_in6 sa6;
    memset(&sa6, 0, sizeof(sa6));
    sa6.sin6_family = AF_INET6;

    if (inet_pton(AF_INET6, ip, &sa6.sin6_addr) == 1) {
        memcpy(ss, &sa6, sizeof(sa6));
        *ss_len = sizeof(sa6);
        *family = AF_INET6;
        return true;
    }

    return false;
}

static bool is_valid_hostname(const char *name) {
    size_t len = strlen(name);
    if (len == 0 || len > 253) {
        return false;
    }

    if (name[len - 1] == '.') {
        len--; /* Allow trailing dot by ignoring it for validation */
    }

    if (len == 0) {
        return false;
    }

    size_t label_len = 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)name[i];

        if (c == '.') {
            if (label_len == 0 || label_len > 63) {
                return false;
            }
            if (name[i - 1] == '-') {
                return false;
            }
            label_len = 0;
            continue;
        }

        if (!(isalnum(c) || c == '-')) {
            return false;
        }

        if (label_len == 0 && c == '-') {
            return false;
        }

        label_len++;
    }

    if (label_len == 0 || label_len > 63) {
        return false;
    }
    if (name[len - 1] == '-') {
        return false;
    }

    return true;
}

static bool sockaddr_equals_ip(const struct sockaddr *a, const struct sockaddr_storage *b, int family) {
    if (family == AF_INET && a->sa_family == AF_INET) {
        const struct sockaddr_in *a4 = (const struct sockaddr_in *)a;
        const struct sockaddr_in *b4 = (const struct sockaddr_in *)b;
        return memcmp(&a4->sin_addr, &b4->sin_addr, sizeof(struct in_addr)) == 0;
    }

    if (family == AF_INET6 && a->sa_family == AF_INET6) {
        const struct sockaddr_in6 *a6 = (const struct sockaddr_in6 *)a;
        const struct sockaddr_in6 *b6 = (const struct sockaddr_in6 *)b;
        return memcmp(&a6->sin6_addr, &b6->sin6_addr, sizeof(struct in6_addr)) == 0;
    }

    return false;
}

int main(int argc, char **argv) {
    const char *ip = NULL;

    for (int i = 1; i < argc; i++) {
        if ((strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--ip") == 0) && i + 1 < argc) {
            ip = argv[++i];
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return STATE_UNKNOWN;
        } else {
            printf("UNKNOWN - Ungueltige Aufrufparameter\n");
            printf("Argumentfehler: %s\n", argv[i]);
            printf("Erwartet: -i <ip-address>\n");
            print_usage(argv[0]);
            return STATE_UNKNOWN;
        }
    }

    if (ip == NULL) {
        printf("UNKNOWN - Keine IP angegeben\n");
        printf("Parameter fehlt: -i <ip-address>\n");
        print_usage(argv[0]);
        return STATE_UNKNOWN;
    }

    struct sockaddr_storage ss;
    memset(&ss, 0, sizeof(ss));
    socklen_t ss_len = 0;
    int family = AF_UNSPEC;

    if (!parse_ip(ip, &ss, &ss_len, &family)) {
        printf("UNKNOWN - Ungueltige IP-Adresse: %s\n", ip);
        printf("Die Eingabe konnte weder als IPv4 noch als IPv6 geparst werden.\n");
        return STATE_UNKNOWN;
    }

    char hostname[NI_MAXHOST];
    memset(hostname, 0, sizeof(hostname));

    int rc = getnameinfo((struct sockaddr *)&ss, ss_len, hostname, sizeof(hostname), NULL, 0, NI_NAMEREQD);
    if (rc != 0) {
        printf("CRITICAL - Kein PTR-Eintrag fuer %s\n", ip);
        printf("Reverse-Lookup fehlgeschlagen: %s\n", gai_strerror(rc));
        return STATE_CRITICAL;
    }

    if (!is_valid_hostname(hostname)) {
        printf("CRITICAL - PTR fuer %s liefert ungueltigen Hostnamen\n", ip);
        printf("PTR-Wert: %s\n", hostname);
        printf("Hostname-Pruefung: RFC-konformer FQDN (Label 1-63, Gesamtlaenge <=253, nur [A-Za-z0-9-]).\n");
        return STATE_CRITICAL;
    }

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = family;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *res = NULL;
    rc = getaddrinfo(hostname, NULL, &hints, &res);
    if (rc != 0) {
        printf("CRITICAL - PTR-Hostname %s loest nicht auf\n", hostname);
        printf("Forward-Lookup fehlgeschlagen: %s\n", gai_strerror(rc));
        printf("Gepruefte IP: %s\n", ip);
        return STATE_CRITICAL;
    }

    bool matched = false;
    for (struct addrinfo *p = res; p != NULL; p = p->ai_next) {
        if (sockaddr_equals_ip(p->ai_addr, &ss, family)) {
            matched = true;
            break;
        }
    }
    freeaddrinfo(res);

    if (!matched) {
        printf("CRITICAL - PTR-Hostname %s loest nicht zurueck auf %s\n", hostname, ip);
        printf("Forward-Lookup erfolgreich, aber ohne exakten Treffer auf die gepruefte IP.\n");
        return STATE_CRITICAL;
    }

    printf("OK - PTR vorhanden und valide: %s -> %s\n", ip, hostname);
    printf("Check erfolgreich: Reverse-Lookup, Hostname-Validierung und Forward-Confirm-Match sind konsistent.\n");
    return STATE_OK;
}
