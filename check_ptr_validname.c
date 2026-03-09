#include <arpa/inet.h>
#include <arpa/nameser.h>
#include <ctype.h>
#include <netdb.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define STATE_OK 0
#define STATE_WARNING 1
#define STATE_CRITICAL 2
#define STATE_UNKNOWN 3

#define DEFAULT_TIMEOUT_MS 2000
#define DNS_PORT 53
#define DNS_PACKET_MAX 512

struct options {
    const char *ip;
    const char *resolver;
    int timeout_ms;
    bool warn_partial;
    bool perfdata;
    bool json;
    bool idn_check;
};

struct check_result {
    int state;
    const char *state_text;
    const char *message;
    char detail[256];
    char hostname[NI_MAXHOST];
    bool has_hostname;
    bool ptr_ok;
    bool hostname_ok;
    bool idn_ok;
    bool forward_ok;
    bool forward_match;
    bool partial;
    double ptr_ms;
    double forward_ms;
    double total_ms;
    char resolver_used[INET6_ADDRSTRLEN + 16];
};

static void print_usage(const char *prog) {
    printf("Usage: %s -i <ip-address> [options]\n", prog);
    printf("Options:\n");
    printf("  -i, --ip <ip>              IPv4 oder IPv6 Adresse (Pflicht)\n");
    printf("  -r, --resolver <ip>        Expliziter DNS-Resolver (IPv4/IPv6)\n");
    printf("  -t, --timeout-ms <ms>      DNS-Timeout in Millisekunden (Default: %d)\n", DEFAULT_TIMEOUT_MS);
    printf("      --warn-partial         WARNING statt CRITICAL bei Teilkonsistenz\n");
    printf("      --perfdata             Performance-Daten im Monitoring-Output\n");
    printf("      --json                 JSON-Ausgabe\n");
    printf("      --idn-check            Erweiterte IDN/Punycode-Pruefung\n");
    printf("  -h, --help                 Hilfe anzeigen\n");
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

static bool is_valid_hostname_strict(const char *name) {
    size_t len = strlen(name);
    if (len == 0 || len > 253) {
        return false;
    }

    if (name[len - 1] == '.') {
        len--;
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

static bool is_valid_hostname_relaxed(const char *name) {
    size_t len = strlen(name);
    if (len == 0 || len > 253) {
        return false;
    }

    if (name[len - 1] == '.') {
        len--;
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
            label_len = 0;
            continue;
        }

        if (!(isalnum(c) || c == '-' || c == '_')) {
            return false;
        }
        label_len++;
    }

    return label_len > 0 && label_len <= 63;
}

static double elapsed_ms(const struct timespec *start, const struct timespec *end) {
    double sec = (double)(end->tv_sec - start->tv_sec) * 1000.0;
    double nsec = (double)(end->tv_nsec - start->tv_nsec) / 1000000.0;
    return sec + nsec;
}

static bool parse_positive_int(const char *s, int *out) {
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (*s == '\0' || end == NULL || *end != '\0' || v <= 0 || v > 600000) {
        return false;
    }
    *out = (int)v;
    return true;
}

static bool extract_system_resolver(char *buf, size_t buf_len) {
    FILE *fp = fopen("/etc/resolv.conf", "r");
    if (fp == NULL) {
        return false;
    }

    char line[512];
    while (fgets(line, sizeof(line), fp) != NULL) {
        char *p = line;
        while (isspace((unsigned char)*p)) {
            p++;
        }
        if (*p == '#' || *p == ';' || *p == '\0') {
            continue;
        }

        if (strncmp(p, "nameserver", 10) == 0 && isspace((unsigned char)p[10])) {
            p += 10;
            while (isspace((unsigned char)*p)) {
                p++;
            }

            size_t n = strcspn(p, " \t\r\n#;");
            if (n == 0 || n >= buf_len) {
                fclose(fp);
                return false;
            }
            memcpy(buf, p, n);
            buf[n] = '\0';
            fclose(fp);
            return true;
        }
    }

    fclose(fp);
    return false;
}

static bool parse_resolver_address(const char *resolver, struct sockaddr_storage *ss, socklen_t *ss_len, char *pretty, size_t pretty_len) {
    struct sockaddr_in sa4;
    memset(&sa4, 0, sizeof(sa4));
    sa4.sin_family = AF_INET;
    sa4.sin_port = htons(DNS_PORT);

    if (inet_pton(AF_INET, resolver, &sa4.sin_addr) == 1) {
        memcpy(ss, &sa4, sizeof(sa4));
        *ss_len = sizeof(sa4);
        snprintf(pretty, pretty_len, "%s:%d", resolver, DNS_PORT);
        return true;
    }

    struct sockaddr_in6 sa6;
    memset(&sa6, 0, sizeof(sa6));
    sa6.sin6_family = AF_INET6;
    sa6.sin6_port = htons(DNS_PORT);

    if (inet_pton(AF_INET6, resolver, &sa6.sin6_addr) == 1) {
        memcpy(ss, &sa6, sizeof(sa6));
        *ss_len = sizeof(sa6);
        snprintf(pretty, pretty_len, "[%s]:%d", resolver, DNS_PORT);
        return true;
    }

    return false;
}

static uint16_t read_u16(const unsigned char *buf, size_t off) {
    return (uint16_t)(((uint16_t)buf[off] << 8) | (uint16_t)buf[off + 1]);
}

static uint32_t read_u32(const unsigned char *buf, size_t off) {
    return ((uint32_t)buf[off] << 24) | ((uint32_t)buf[off + 1] << 16) | ((uint32_t)buf[off + 2] << 8) | (uint32_t)buf[off + 3];
}

static bool encode_dns_name(const char *name, unsigned char *out, size_t out_len, size_t *written) {
    size_t pos = 0;
    const char *cur = name;

    while (*cur != '\0') {
        const char *dot = strchr(cur, '.');
        size_t label_len = (dot != NULL) ? (size_t)(dot - cur) : strlen(cur);

        if (label_len > 63 || pos + 1 + label_len >= out_len) {
            return false;
        }

        out[pos++] = (unsigned char)label_len;
        if (label_len > 0) {
            memcpy(&out[pos], cur, label_len);
            pos += label_len;
        }

        if (dot == NULL) {
            break;
        }

        cur = dot + 1;
        if (*cur == '\0') {
            break;
        }
    }

    if (pos + 1 > out_len) {
        return false;
    }
    out[pos++] = 0;
    *written = pos;
    return true;
}

static bool decode_dns_name(const unsigned char *packet, size_t packet_len, size_t start, char *out, size_t out_len, size_t *next_offset) {
    size_t pos = start;
    size_t out_pos = 0;
    size_t jumps = 0;
    bool jumped = false;
    size_t jump_return = 0;

    while (1) {
        if (pos >= packet_len) {
            return false;
        }

        unsigned char len = packet[pos];
        if (len == 0) {
            pos++;
            break;
        }

        if ((len & 0xC0) == 0xC0) {
            if (pos + 1 >= packet_len) {
                return false;
            }
            uint16_t ptr = (uint16_t)(((len & 0x3F) << 8) | packet[pos + 1]);
            if (ptr >= packet_len || jumps++ > 20) {
                return false;
            }
            if (!jumped) {
                jump_return = pos + 2;
                jumped = true;
            }
            pos = ptr;
            continue;
        }

        if ((len & 0xC0) != 0) {
            return false;
        }

        pos++;
        if (pos + len > packet_len) {
            return false;
        }

        if (out_pos != 0) {
            if (out_pos + 1 >= out_len) {
                return false;
            }
            out[out_pos++] = '.';
        }

        if (out_pos + len >= out_len) {
            return false;
        }

        memcpy(&out[out_pos], &packet[pos], len);
        out_pos += len;
        pos += len;
    }

    if (out_pos >= out_len) {
        return false;
    }
    out[out_pos] = '\0';

    if (next_offset != NULL) {
        *next_offset = jumped ? jump_return : pos;
    }

    return true;
}

static bool build_dns_query(const char *qname, uint16_t qtype, unsigned char *query, size_t query_len, size_t *out_len, uint16_t *out_id) {
    if (query_len < 12) {
        return false;
    }

    uint16_t id = (uint16_t)(((unsigned int)getpid() & 0xFFFFu) ^ (uint16_t)time(NULL));
    memset(query, 0, query_len);

    query[0] = (unsigned char)(id >> 8);
    query[1] = (unsigned char)(id & 0xFF);
    query[2] = 0x01;
    query[3] = 0x00;
    query[4] = 0x00;
    query[5] = 0x01;

    size_t name_len = 0;
    if (!encode_dns_name(qname, &query[12], query_len - 12, &name_len)) {
        return false;
    }

    size_t pos = 12 + name_len;
    if (pos + 4 > query_len) {
        return false;
    }

    query[pos++] = (unsigned char)(qtype >> 8);
    query[pos++] = (unsigned char)(qtype & 0xFF);
    query[pos++] = 0x00;
    query[pos++] = 0x01;

    *out_len = pos;
    *out_id = id;
    return true;
}

static bool send_dns_query(const struct sockaddr_storage *resolver_ss, socklen_t resolver_len,
                           const unsigned char *query, size_t query_len,
                           unsigned char *response, size_t response_len, size_t *response_size,
                           int timeout_ms, char *err, size_t err_len) {
    int fd = socket(resolver_ss->ss_family, SOCK_DGRAM, 0);
    if (fd < 0) {
        snprintf(err, err_len, "socket() fehlgeschlagen");
        return false;
    }

    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) != 0) {
        close(fd);
        snprintf(err, err_len, "setsockopt(SO_RCVTIMEO) fehlgeschlagen");
        return false;
    }

    ssize_t sent = sendto(fd, query, query_len, 0, (const struct sockaddr *)resolver_ss, resolver_len);
    if (sent < 0 || (size_t)sent != query_len) {
        close(fd);
        snprintf(err, err_len, "sendto() fehlgeschlagen");
        return false;
    }

    ssize_t n = recvfrom(fd, response, response_len, 0, NULL, NULL);
    if (n < 0) {
        close(fd);
        snprintf(err, err_len, "recvfrom() Timeout oder Fehler");
        return false;
    }

    close(fd);
    *response_size = (size_t)n;
    return true;
}

static bool ipv4_to_ptr_name(const struct sockaddr_storage *ss, char *out, size_t out_len) {
    const struct sockaddr_in *sa4 = (const struct sockaddr_in *)ss;
    unsigned char b[4];
    memcpy(b, &sa4->sin_addr, 4);
    int n = snprintf(out, out_len, "%u.%u.%u.%u.in-addr.arpa", b[3], b[2], b[1], b[0]);
    return n > 0 && (size_t)n < out_len;
}

static bool ipv6_to_ptr_name(const struct sockaddr_storage *ss, char *out, size_t out_len) {
    const struct sockaddr_in6 *sa6 = (const struct sockaddr_in6 *)ss;
    const unsigned char *bytes = (const unsigned char *)&sa6->sin6_addr;
    static const char hex[] = "0123456789abcdef";
    size_t pos = 0;

    for (int i = 15; i >= 0; i--) {
        unsigned char byte = bytes[i];
        unsigned char lo = (unsigned char)(byte & 0x0F);
        unsigned char hi = (unsigned char)((byte >> 4) & 0x0F);

        if (pos + 4 >= out_len) {
            return false;
        }
        out[pos++] = hex[lo];
        out[pos++] = '.';
        out[pos++] = hex[hi];
        out[pos++] = '.';
    }

    const char *suffix = "ip6.arpa";
    size_t suffix_len = strlen(suffix);
    if (pos + suffix_len + 1 > out_len) {
        return false;
    }
    memcpy(&out[pos], suffix, suffix_len + 1);
    return true;
}

static bool extract_first_ptr_record(const unsigned char *response, size_t response_len, uint16_t expected_id,
                                     char *hostname, size_t hostname_len, char *err, size_t err_len) {
    if (response_len < 12) {
        snprintf(err, err_len, "DNS-Antwort zu kurz");
        return false;
    }

    uint16_t id = read_u16(response, 0);
    if (id != expected_id) {
        snprintf(err, err_len, "DNS-Transaktions-ID passt nicht");
        return false;
    }

    uint16_t flags = read_u16(response, 2);
    uint16_t qdcount = read_u16(response, 4);
    uint16_t ancount = read_u16(response, 6);
    uint16_t rcode = flags & 0x000F;

    if (rcode != 0) {
        snprintf(err, err_len, "DNS-Fehlercode: %u", rcode);
        return false;
    }

    size_t off = 12;
    for (uint16_t i = 0; i < qdcount; i++) {
        if (!decode_dns_name(response, response_len, off, hostname, hostname_len, &off)) {
            snprintf(err, err_len, "DNS-Question ungueltig");
            return false;
        }
        if (off + 4 > response_len) {
            snprintf(err, err_len, "DNS-Question abgeschnitten");
            return false;
        }
        off += 4;
    }

    for (uint16_t i = 0; i < ancount; i++) {
        if (!decode_dns_name(response, response_len, off, hostname, hostname_len, &off)) {
            snprintf(err, err_len, "DNS-Answer Name ungueltig");
            return false;
        }

        if (off + 10 > response_len) {
            snprintf(err, err_len, "DNS-Answer Header abgeschnitten");
            return false;
        }

        uint16_t type = read_u16(response, off);
        uint16_t class = read_u16(response, off + 2);
        (void)read_u32(response, off + 4);
        uint16_t rdlen = read_u16(response, off + 8);
        off += 10;

        if (off + rdlen > response_len) {
            snprintf(err, err_len, "DNS-Answer RDATA abgeschnitten");
            return false;
        }

        if (class == ns_c_in && type == ns_t_ptr) {
            size_t rstart = off;
            if (!decode_dns_name(response, response_len, rstart, hostname, hostname_len, NULL)) {
                snprintf(err, err_len, "PTR-RDATA ungueltig");
                return false;
            }
            return true;
        }

        off += rdlen;
    }

    snprintf(err, err_len, "Kein PTR-Record in DNS-Antwort");
    return false;
}

static bool query_ptr_record(const struct sockaddr_storage *target_ss, int family,
                             const struct sockaddr_storage *resolver_ss, socklen_t resolver_len,
                             int timeout_ms, char *hostname, size_t hostname_len,
                             char *err, size_t err_len) {
    char ptr_name[NI_MAXHOST];
    memset(ptr_name, 0, sizeof(ptr_name));

    bool ptr_ok = false;
    if (family == AF_INET) {
        ptr_ok = ipv4_to_ptr_name(target_ss, ptr_name, sizeof(ptr_name));
    } else if (family == AF_INET6) {
        ptr_ok = ipv6_to_ptr_name(target_ss, ptr_name, sizeof(ptr_name));
    }

    if (!ptr_ok) {
        snprintf(err, err_len, "PTR-Zielname konnte nicht erstellt werden");
        return false;
    }

    unsigned char query[DNS_PACKET_MAX];
    size_t query_len = 0;
    uint16_t query_id = 0;
    if (!build_dns_query(ptr_name, ns_t_ptr, query, sizeof(query), &query_len, &query_id)) {
        snprintf(err, err_len, "DNS-Query-Aufbau fehlgeschlagen");
        return false;
    }

    unsigned char response[DNS_PACKET_MAX];
    size_t response_len = 0;
    if (!send_dns_query(resolver_ss, resolver_len, query, query_len, response, sizeof(response), &response_len,
                        timeout_ms, err, err_len)) {
        return false;
    }

    return extract_first_ptr_record(response, response_len, query_id, hostname, hostname_len, err, err_len);
}

static bool query_forward_match(const char *hostname, int family, const struct sockaddr_storage *target_ss,
                                const struct sockaddr_storage *resolver_ss, socklen_t resolver_len,
                                int timeout_ms, bool *matched,
                                char *err, size_t err_len) {
    *matched = false;

    uint16_t qtype = (family == AF_INET6) ? ns_t_aaaa : ns_t_a;

    unsigned char query[DNS_PACKET_MAX];
    size_t query_len = 0;
    uint16_t query_id = 0;
    if (!build_dns_query(hostname, qtype, query, sizeof(query), &query_len, &query_id)) {
        snprintf(err, err_len, "DNS-Forward-Query-Aufbau fehlgeschlagen");
        return false;
    }

    unsigned char response[DNS_PACKET_MAX];
    size_t response_len = 0;
    if (!send_dns_query(resolver_ss, resolver_len, query, query_len, response, sizeof(response), &response_len,
                        timeout_ms, err, err_len)) {
        return false;
    }

    if (response_len < 12) {
        snprintf(err, err_len, "DNS-Antwort zu kurz");
        return false;
    }

    uint16_t id = read_u16(response, 0);
    if (id != query_id) {
        snprintf(err, err_len, "DNS-Transaktions-ID passt nicht");
        return false;
    }

    uint16_t flags = read_u16(response, 2);
    uint16_t qdcount = read_u16(response, 4);
    uint16_t ancount = read_u16(response, 6);
    uint16_t rcode = flags & 0x000F;

    if (rcode != 0) {
        snprintf(err, err_len, "DNS-Fehlercode: %u", rcode);
        return false;
    }

    char tmp_name[NI_MAXHOST];
    size_t off = 12;

    for (uint16_t i = 0; i < qdcount; i++) {
        if (!decode_dns_name(response, response_len, off, tmp_name, sizeof(tmp_name), &off)) {
            snprintf(err, err_len, "DNS-Question ungueltig");
            return false;
        }
        if (off + 4 > response_len) {
            snprintf(err, err_len, "DNS-Question abgeschnitten");
            return false;
        }
        off += 4;
    }

    bool any_address = false;
    for (uint16_t i = 0; i < ancount; i++) {
        if (!decode_dns_name(response, response_len, off, tmp_name, sizeof(tmp_name), &off)) {
            snprintf(err, err_len, "DNS-Answer Name ungueltig");
            return false;
        }

        if (off + 10 > response_len) {
            snprintf(err, err_len, "DNS-Answer Header abgeschnitten");
            return false;
        }

        uint16_t type = read_u16(response, off);
        uint16_t class = read_u16(response, off + 2);
        uint16_t rdlen = read_u16(response, off + 8);
        off += 10;

        if (off + rdlen > response_len) {
            snprintf(err, err_len, "DNS-Answer RDATA abgeschnitten");
            return false;
        }

        if (class == ns_c_in) {
            if (family == AF_INET && type == ns_t_a && rdlen == 4) {
                any_address = true;
                struct sockaddr_in candidate;
                memset(&candidate, 0, sizeof(candidate));
                candidate.sin_family = AF_INET;
                memcpy(&candidate.sin_addr, &response[off], 4);
                if (sockaddr_equals_ip((const struct sockaddr *)&candidate, target_ss, family)) {
                    *matched = true;
                }
            } else if (family == AF_INET6 && type == ns_t_aaaa && rdlen == 16) {
                any_address = true;
                struct sockaddr_in6 candidate6;
                memset(&candidate6, 0, sizeof(candidate6));
                candidate6.sin6_family = AF_INET6;
                memcpy(&candidate6.sin6_addr, &response[off], 16);
                if (sockaddr_equals_ip((const struct sockaddr *)&candidate6, target_ss, family)) {
                    *matched = true;
                }
            }
        }

        off += rdlen;
    }

    if (!any_address) {
        snprintf(err, err_len, "Keine passenden A/AAAA Records gefunden");
        return false;
    }

    return true;
}

static int punycode_digit_value(int c) {
    if (c >= 'a' && c <= 'z') {
        return c - 'a';
    }
    if (c >= 'A' && c <= 'Z') {
        return c - 'A';
    }
    if (c >= '0' && c <= '9') {
        return c - '0' + 26;
    }
    return -1;
}

static unsigned int punycode_adapt(unsigned int delta, unsigned int numpoints, bool first_time) {
    const unsigned int base = 36;
    const unsigned int tmin = 1;
    const unsigned int tmax = 26;
    const unsigned int skew = 38;
    const unsigned int damp = 700;

    delta = first_time ? delta / damp : delta / 2;
    delta += delta / numpoints;

    unsigned int k = 0;
    while (delta > ((base - tmin) * tmax) / 2) {
        delta /= (base - tmin);
        k += base;
    }

    return k + (((base - tmin + 1) * delta) / (delta + skew));
}

static bool punycode_decode_label(const char *input) {
    const unsigned int base = 36;
    const unsigned int tmin = 1;
    const unsigned int tmax = 26;
    const unsigned int initial_n = 128;
    const unsigned int initial_bias = 72;

    size_t in_len = strlen(input);
    if (in_len == 0) {
        return false;
    }

    unsigned int output[64];
    size_t out_len = 0;

    const char *last_dash = strrchr(input, '-');
    size_t b = (last_dash == NULL) ? 0 : (size_t)(last_dash - input);

    for (size_t j = 0; j < b; j++) {
        unsigned char c = (unsigned char)input[j];
        if (c >= 0x80) {
            return false;
        }
        if (out_len >= 64) {
            return false;
        }
        output[out_len++] = c;
    }

    size_t in_pos = (last_dash == NULL) ? 0 : b + 1;
    unsigned int n = initial_n;
    unsigned int i = 0;
    unsigned int bias = initial_bias;

    while (in_pos < in_len) {
        unsigned int oldi = i;
        unsigned int w = 1;

        for (unsigned int k = base;; k += base) {
            if (in_pos >= in_len) {
                return false;
            }

            int digit = punycode_digit_value((unsigned char)input[in_pos++]);
            if (digit < 0) {
                return false;
            }

            if ((unsigned int)digit > (UINT32_MAX - i) / w) {
                return false;
            }
            i += (unsigned int)digit * w;

            unsigned int t;
            if (k <= bias + tmin) {
                t = tmin;
            } else if (k >= bias + tmax) {
                t = tmax;
            } else {
                t = k - bias;
            }

            if ((unsigned int)digit < t) {
                break;
            }

            if (w > UINT32_MAX / (base - t)) {
                return false;
            }
            w *= (base - t);
        }

        bias = punycode_adapt(i - oldi, (unsigned int)(out_len + 1), oldi == 0);

        if (i / (out_len + 1) > UINT32_MAX - n) {
            return false;
        }
        n += i / (out_len + 1);
        i %= (out_len + 1);

        if (out_len >= 64) {
            return false;
        }

        memmove(&output[i + 1], &output[i], (out_len - i) * sizeof(output[0]));
        output[i] = n;
        out_len++;
        i++;
    }

    return out_len > 0;
}

static bool validate_idn_punycode(const char *hostname, char *err, size_t err_len) {
    char buf[NI_MAXHOST];
    size_t len = strlen(hostname);
    if (len >= sizeof(buf)) {
        snprintf(err, err_len, "Hostname zu lang");
        return false;
    }

    memcpy(buf, hostname, len + 1);
    if (len > 0 && buf[len - 1] == '.') {
        buf[len - 1] = '\0';
    }

    char *save = NULL;
    char *label = strtok_r(buf, ".", &save);
    while (label != NULL) {
        size_t l = strlen(label);
        if (l >= 4 && strncasecmp(label, "xn--", 4) == 0) {
            if (!punycode_decode_label(label + 4)) {
                snprintf(err, err_len, "Ungueltiges Punycode-Label: %s", label);
                return false;
            }
        }
        label = strtok_r(NULL, ".", &save);
    }

    return true;
}

static void json_escape_print(const char *s) {
    for (const unsigned char *p = (const unsigned char *)s; *p != '\0'; p++) {
        switch (*p) {
            case '"':
                printf("\\\"");
                break;
            case '\\':
                printf("\\\\");
                break;
            case '\b':
                printf("\\b");
                break;
            case '\f':
                printf("\\f");
                break;
            case '\n':
                printf("\\n");
                break;
            case '\r':
                printf("\\r");
                break;
            case '\t':
                printf("\\t");
                break;
            default:
                if (*p < 0x20) {
                    printf("\\u%04x", (unsigned int)*p);
                } else {
                    putchar(*p);
                }
                break;
        }
    }
}

static const char *state_to_text(int state) {
    switch (state) {
        case STATE_OK:
            return "OK";
        case STATE_WARNING:
            return "WARNING";
        case STATE_CRITICAL:
            return "CRITICAL";
        default:
            return "UNKNOWN";
    }
}

static void print_result_text(const struct options *opt, const struct check_result *res) {
    printf("%s - %s", res->state_text, res->message);
    if (res->has_hostname) {
        printf(" (%s -> %s)", opt->ip, res->hostname);
    }
    if (res->detail[0] != '\0') {
        printf(" [%s]", res->detail);
    }

    if (opt->perfdata) {
        printf(" | ptr_lookup_ms=%.3fms;;;; forward_lookup_ms=%.3fms;;;; total_ms=%.3fms;;;;", res->ptr_ms, res->forward_ms, res->total_ms);
    }
    printf("\n");
}

static void print_result_json(const struct options *opt, const struct check_result *res) {
    printf("{\"state\":\"");
    json_escape_print(res->state_text);
    printf("\",\"code\":%d,\"message\":\"", res->state);
    json_escape_print(res->message);
    printf("\",\"detail\":\"");
    json_escape_print(res->detail);
    printf("\",\"ip\":\"");
    json_escape_print(opt->ip != NULL ? opt->ip : "");
    printf("\",\"hostname\":\"");
    json_escape_print(res->has_hostname ? res->hostname : "");
    printf("\",\"resolver\":\"");
    json_escape_print(res->resolver_used);
    printf("\",\"timeout_ms\":%d", opt->timeout_ms);

    printf(",\"checks\":{\"ptr\":%s,\"hostname\":%s,\"idn\":%s,\"forward\":%s,\"forward_match\":%s,\"partial\":%s}",
           res->ptr_ok ? "true" : "false",
           res->hostname_ok ? "true" : "false",
           res->idn_ok ? "true" : "false",
           res->forward_ok ? "true" : "false",
           res->forward_match ? "true" : "false",
           res->partial ? "true" : "false");

    printf(",\"latency_ms\":{\"ptr_lookup\":%.3f,\"forward_lookup\":%.3f,\"total\":%.3f}}\n",
           res->ptr_ms, res->forward_ms, res->total_ms);
}

static void init_result(struct check_result *res) {
    memset(res, 0, sizeof(*res));
    res->state = STATE_UNKNOWN;
    res->state_text = state_to_text(STATE_UNKNOWN);
    res->message = "Unbekannter Zustand";
    res->detail[0] = '\0';
    res->resolver_used[0] = '\0';
    res->idn_ok = true;
}

int main(int argc, char **argv) {
    struct options opt;
    memset(&opt, 0, sizeof(opt));
    opt.timeout_ms = DEFAULT_TIMEOUT_MS;

    for (int i = 1; i < argc; i++) {
        if ((strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--ip") == 0) && i + 1 < argc) {
            opt.ip = argv[++i];
        } else if ((strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "--resolver") == 0) && i + 1 < argc) {
            opt.resolver = argv[++i];
        } else if ((strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--timeout-ms") == 0) && i + 1 < argc) {
            if (!parse_positive_int(argv[++i], &opt.timeout_ms)) {
                printf("UNKNOWN - Ungueltiger Timeout-Wert: %s\n", argv[i]);
                return STATE_UNKNOWN;
            }
        } else if (strcmp(argv[i], "--warn-partial") == 0) {
            opt.warn_partial = true;
        } else if (strcmp(argv[i], "--perfdata") == 0) {
            opt.perfdata = true;
        } else if (strcmp(argv[i], "--json") == 0) {
            opt.json = true;
        } else if (strcmp(argv[i], "--idn-check") == 0) {
            opt.idn_check = true;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return STATE_UNKNOWN;
        } else {
            printf("UNKNOWN - Ungueltige Aufrufparameter: %s\n", argv[i]);
            print_usage(argv[0]);
            return STATE_UNKNOWN;
        }
    }

    if (opt.ip == NULL) {
        printf("UNKNOWN - Keine IP angegeben\n");
        print_usage(argv[0]);
        return STATE_UNKNOWN;
    }

    struct check_result res;
    init_result(&res);

    struct timespec start_ts;
    clock_gettime(CLOCK_MONOTONIC, &start_ts);

    struct sockaddr_storage target_ss;
    memset(&target_ss, 0, sizeof(target_ss));
    socklen_t target_len = 0;
    int family = AF_UNSPEC;

    if (!parse_ip(opt.ip, &target_ss, &target_len, &family)) {
        res.state = STATE_UNKNOWN;
        res.state_text = state_to_text(res.state);
        res.message = "Ungueltige IP-Adresse";
        snprintf(res.detail, sizeof(res.detail), "%s", opt.ip);
        if (opt.json) {
            print_result_json(&opt, &res);
        } else {
            print_result_text(&opt, &res);
        }
        return res.state;
    }

    char resolver_ip[INET6_ADDRSTRLEN + 1];
    memset(resolver_ip, 0, sizeof(resolver_ip));

    if (opt.resolver != NULL) {
        snprintf(resolver_ip, sizeof(resolver_ip), "%s", opt.resolver);
    } else if (!extract_system_resolver(resolver_ip, sizeof(resolver_ip))) {
        res.state = STATE_UNKNOWN;
        res.state_text = state_to_text(res.state);
        res.message = "Kein DNS-Resolver gefunden";
        snprintf(res.detail, sizeof(res.detail), "Bitte --resolver setzen oder /etc/resolv.conf pruefen");
        if (opt.json) {
            print_result_json(&opt, &res);
        } else {
            print_result_text(&opt, &res);
        }
        return res.state;
    }

    struct sockaddr_storage resolver_ss;
    memset(&resolver_ss, 0, sizeof(resolver_ss));
    socklen_t resolver_len = 0;

    if (!parse_resolver_address(resolver_ip, &resolver_ss, &resolver_len, res.resolver_used, sizeof(res.resolver_used))) {
        res.state = STATE_UNKNOWN;
        res.state_text = state_to_text(res.state);
        res.message = "Ungueltiger Resolver";
        snprintf(res.detail, sizeof(res.detail), "%s", resolver_ip);
        if (opt.json) {
            print_result_json(&opt, &res);
        } else {
            print_result_text(&opt, &res);
        }
        return res.state;
    }

    char dns_err[256];
    memset(dns_err, 0, sizeof(dns_err));

    struct timespec ptr_start;
    struct timespec ptr_end;
    clock_gettime(CLOCK_MONOTONIC, &ptr_start);
    bool ptr_query_ok = query_ptr_record(&target_ss, family, &resolver_ss, resolver_len, opt.timeout_ms,
                                         res.hostname, sizeof(res.hostname), dns_err, sizeof(dns_err));
    clock_gettime(CLOCK_MONOTONIC, &ptr_end);
    res.ptr_ms = elapsed_ms(&ptr_start, &ptr_end);

    if (!ptr_query_ok) {
        res.state = STATE_CRITICAL;
        res.state_text = state_to_text(res.state);
        res.message = "Kein PTR-Eintrag oder PTR-Lookup fehlgeschlagen";
        snprintf(res.detail, sizeof(res.detail), "%s", dns_err);
        struct timespec end_ts;
        clock_gettime(CLOCK_MONOTONIC, &end_ts);
        res.total_ms = elapsed_ms(&start_ts, &end_ts);
        if (opt.json) {
            print_result_json(&opt, &res);
        } else {
            print_result_text(&opt, &res);
        }
        return res.state;
    }

    res.ptr_ok = true;
    res.has_hostname = true;

    bool strict_hostname_ok = is_valid_hostname_strict(res.hostname);
    bool relaxed_hostname_ok = is_valid_hostname_relaxed(res.hostname);

    if (!strict_hostname_ok) {
        if (opt.warn_partial && relaxed_hostname_ok) {
            res.partial = true;
            res.hostname_ok = false;
            res.state = STATE_WARNING;
            res.state_text = state_to_text(res.state);
            res.message = "PTR vorhanden, Hostname nur grenzwertig";
            snprintf(res.detail, sizeof(res.detail), "Hostname=%s", res.hostname);
        } else {
            res.state = STATE_CRITICAL;
            res.state_text = state_to_text(res.state);
            res.message = "PTR-Hostname ungueltig";
            snprintf(res.detail, sizeof(res.detail), "Hostname=%s", res.hostname);
            struct timespec end_ts;
            clock_gettime(CLOCK_MONOTONIC, &end_ts);
            res.total_ms = elapsed_ms(&start_ts, &end_ts);
            if (opt.json) {
                print_result_json(&opt, &res);
            } else {
                print_result_text(&opt, &res);
            }
            return res.state;
        }
    } else {
        res.hostname_ok = true;
    }

    if (opt.idn_check) {
        char idn_err[256];
        memset(idn_err, 0, sizeof(idn_err));
        if (!validate_idn_punycode(res.hostname, idn_err, sizeof(idn_err))) {
            res.idn_ok = false;
            if (opt.warn_partial) {
                res.partial = true;
                res.state = STATE_WARNING;
                res.state_text = state_to_text(res.state);
                res.message = "IDN/Punycode-Pruefung grenzwertig";
                snprintf(res.detail, sizeof(res.detail), "%s", idn_err);
            } else {
                res.state = STATE_CRITICAL;
                res.state_text = state_to_text(res.state);
                res.message = "IDN/Punycode-Pruefung fehlgeschlagen";
                snprintf(res.detail, sizeof(res.detail), "%s", idn_err);
                struct timespec end_ts;
                clock_gettime(CLOCK_MONOTONIC, &end_ts);
                res.total_ms = elapsed_ms(&start_ts, &end_ts);
                if (opt.json) {
                    print_result_json(&opt, &res);
                } else {
                    print_result_text(&opt, &res);
                }
                return res.state;
            }
        }
    }

    bool matched = false;
    clock_gettime(CLOCK_MONOTONIC, &ptr_start);
    bool forward_ok = query_forward_match(res.hostname, family, &target_ss, &resolver_ss, resolver_len,
                                          opt.timeout_ms, &matched, dns_err, sizeof(dns_err));
    clock_gettime(CLOCK_MONOTONIC, &ptr_end);
    res.forward_ms = elapsed_ms(&ptr_start, &ptr_end);

    if (!forward_ok) {
        res.state = STATE_CRITICAL;
        res.state_text = state_to_text(res.state);
        res.message = "Forward-Lookup fehlgeschlagen";
        snprintf(res.detail, sizeof(res.detail), "%s", dns_err);
        struct timespec end_ts;
        clock_gettime(CLOCK_MONOTONIC, &end_ts);
        res.total_ms = elapsed_ms(&start_ts, &end_ts);
        if (opt.json) {
            print_result_json(&opt, &res);
        } else {
            print_result_text(&opt, &res);
        }
        return res.state;
    }

    res.forward_ok = true;
    res.forward_match = matched;

    if (!matched) {
        res.state = STATE_CRITICAL;
        res.state_text = state_to_text(res.state);
        res.message = "Forward-Confirm-Match fehlgeschlagen";
        snprintf(res.detail, sizeof(res.detail), "Hostname %s loest nicht auf %s", res.hostname, opt.ip);
    } else if (res.partial) {
        if (res.message == NULL || strcmp(res.message, "Unbekannter Zustand") == 0) {
            res.message = "Teilkonsistenz festgestellt";
        }
        res.state = STATE_WARNING;
        res.state_text = state_to_text(res.state);
    } else {
        res.state = STATE_OK;
        res.state_text = state_to_text(res.state);
        res.message = "PTR vorhanden und konsistent";
        snprintf(res.detail, sizeof(res.detail), "Forward-Confirm-Match erfolgreich");
    }

    struct timespec end_ts;
    clock_gettime(CLOCK_MONOTONIC, &end_ts);
    res.total_ms = elapsed_ms(&start_ts, &end_ts);

    if (opt.json) {
        print_result_json(&opt, &res);
    } else {
        print_result_text(&opt, &res);
    }

    return res.state;
}
