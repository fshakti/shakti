#include "govee.h"
#include "json_parse.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define GOVEE_SCAN_PORT 4001
#define GOVEE_LISTEN_PORT 4002
#define GOVEE_CMD_PORT 4003
#define GOVEE_MULTICAST "239.255.255.250"
#define GOVEE_SCAN_JSON "{\"msg\":{\"cmd\":\"scan\",\"data\":{\"account_topic\":\"reserve\"}}}"
#define GOVEE_STATUS_JSON "{\"msg\":{\"cmd\":\"devStatus\",\"data\":{}}}"

#ifndef SHAKTI_GOVEE_MODEL_DEFAULT
#define SHAKTI_GOVEE_MODEL_DEFAULT "H70B1"
#endif

static char g_govee_ip[64];
static char g_govee_model[32];
static int g_govee_inited;

static void govee_init(void) {
    if (g_govee_inited) return;
    g_govee_inited = 1;
    g_govee_ip[0] = 0;
    strncpy(g_govee_model, SHAKTI_GOVEE_MODEL_DEFAULT, sizeof g_govee_model - 1);
    const char *env = getenv("SHAKTI_GOVEE_IP");
    if (env && env[0]) strncpy(g_govee_ip, env, sizeof g_govee_ip - 1);
    env = getenv("SHAKTI_GOVEE_SKU");
    if (env && env[0]) strncpy(g_govee_model, env, sizeof g_govee_model - 1);
}

static int govee_skip_lan(void) {
    const char *env = getenv("SHAKTI_GOVEE_SKIP_LAN");
    return env && env[0] == '1';
}

static int set_nonblock(int fd, int on) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    if (on) flags |= O_NONBLOCK;
    else flags &= ~O_NONBLOCK;
    return fcntl(fd, F_SETFL, flags);
}

static int govee_bind_listen(int *out_fd) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return -1;
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(GOVEE_LISTEN_PORT);
    if (bind(fd, (struct sockaddr *)&addr, sizeof addr) != 0) {
        close(fd);
        return -1;
    }
    set_nonblock(fd, 1);
    *out_fd = fd;
    return 0;
}

static int govee_send_scan_to(const char *ip, int broadcast) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return -1;
    if (broadcast) {
        int one = 1;
        setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &one, sizeof one);
    } else {
        int ttl = 1;
        setsockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof ttl);
    }
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(GOVEE_SCAN_PORT);
    if (inet_pton(AF_INET, ip, &addr.sin_addr) != 1) {
        close(fd);
        return -1;
    }
    ssize_t n = sendto(fd, GOVEE_SCAN_JSON, strlen(GOVEE_SCAN_JSON), 0,
                       (struct sockaddr *)&addr, sizeof addr);
    close(fd);
    return n < 0 ? -1 : 0;
}

static void govee_send_scan_all(void) {
    govee_send_scan_to(GOVEE_MULTICAST, 0);
    govee_send_scan_to("255.255.255.255", 1);
    struct ifaddrs *ifa = NULL;
    if (getifaddrs(&ifa) == 0) {
        for (struct ifaddrs *p = ifa; p; p = p->ifa_next) {
            if (!p->ifa_addr || p->ifa_addr->sa_family != AF_INET) continue;
            if (p->ifa_flags & IFF_LOOPBACK) continue;
            if (!(p->ifa_flags & IFF_UP)) continue;
            struct sockaddr_in *sa = (struct sockaddr_in *)p->ifa_addr;
            struct sockaddr_in *ba = (struct sockaddr_in *)p->ifa_broadaddr;
            if (ba && ba->sin_addr.s_addr)
                govee_send_scan_to(inet_ntoa(ba->sin_addr), 1);
            else {
                char ipbuf[64];
                inet_ntop(AF_INET, &sa->sin_addr, ipbuf, sizeof ipbuf);
                unsigned a, b, c, d;
                if (sscanf(ipbuf, "%u.%u.%u.%u", &a, &b, &c, &d) == 4) {
                    char bcast[64];
                    snprintf(bcast, sizeof bcast, "%u.%u.%u.255", a, b, c);
                    govee_send_scan_to(bcast, 1);
                }
            }
        }
        freeifaddrs(ifa);
    }
}

static int govee_send_scan(int listen_fd) {
    (void)listen_fd;
    govee_send_scan_all();
    return 0;
}

static int govee_send_to_ip(const char *ip, const char *json) {
    if (!ip || !ip[0] || !json) return -1;
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(GOVEE_CMD_PORT);
    if (inet_pton(AF_INET, ip, &addr.sin_addr) != 1) {
        close(fd);
        return -1;
    }
    ssize_t n = sendto(fd, json, strlen(json), 0, (struct sockaddr *)&addr, sizeof addr);
    close(fd);
    return n < 0 ? -1 : 0;
}

static V *govee_parse_scan_device(const char *buf, struct sockaddr_in *from) {
    V *parsed = shakti_json_parse(buf, NULL);
    if (!parsed || parsed->t == T_ERR) {
        if (parsed) v_free(parsed);
        return NULL;
    }
    V *msg = v_dict_get(parsed, "msg");
    if (!msg || msg->t != T_DICT) {
        v_free(parsed);
        return NULL;
    }
    V *cmd = v_dict_get(msg, "cmd");
    if (!cmd || cmd->t != T_STR || strcmp(cmd->s, "scan") != 0) {
        v_free(parsed);
        return NULL;
    }
    V *data = v_dict_get(msg, "data");
    if (!data || data->t != T_DICT) {
        v_free(parsed);
        return NULL;
    }
    V *keys = v_list(0);
    V *vals = v_list(0);
    for (int64_t i = 0; i < data->n; i++) {
        V *k = data->keys->L[i];
        if (k && k->t == T_STR)
            v_list_append(keys, v_str(k->s));
        else
            v_list_append(keys, v_nil());
        v_list_append(vals, v_ref(data->vals->L[i]));
    }
    char ipbuf[64];
    if (from) {
        inet_ntop(AF_INET, &from->sin_addr, ipbuf, sizeof ipbuf);
        v_list_append(keys, v_str("ip"));
        v_list_append(vals, v_str(ipbuf));
    }
    V *out = v_dict(keys, vals);
    v_free(keys);
    v_free(vals);
    v_free(parsed);
    return out;
}

static int govee_device_seen(V *list, const char *device_id) {
    if (!list || list->t != T_LIST || !device_id) return 0;
    for (int64_t i = 0; i < list->n; i++) {
        V *d = list->L[i];
        if (!d || d->t != T_DICT) continue;
        V *dev = v_dict_get(d, "device");
        if (dev && dev->t == T_STR && !strcmp(dev->s, device_id)) return 1;
    }
    return 0;
}

static V *govee_parse_status(const char *buf) {
    V *parsed = shakti_json_parse(buf, NULL);
    if (!parsed || parsed->t == T_ERR) return parsed ? parsed : v_err("govee: invalid JSON");
    V *msg = v_dict_get(parsed, "msg");
    if (!msg || msg->t != T_DICT) {
        v_free(parsed);
        return v_err("govee: missing msg");
    }
    V *cmd = v_dict_get(msg, "cmd");
    if (!cmd || cmd->t != T_STR || strcmp(cmd->s, "devStatus") != 0) {
        v_free(parsed);
        return v_err("govee: unexpected response cmd");
    }
    V *data = v_dict_get(msg, "data");
    if (!data || data->t != T_DICT) {
        v_free(parsed);
        return v_err("govee: missing status data");
    }
    V *keys = v_list(0);
    V *vals = v_list(0);
    for (int64_t i = 0; i < data->n; i++) {
        V *k = data->keys->L[i];
        if (k && k->t == T_STR)
            v_list_append(keys, v_str(k->s));
        else
            v_list_append(keys, v_nil());
        v_list_append(vals, v_ref(data->vals->L[i]));
    }
    V *out = v_dict(keys, vals);
    v_free(keys);
    v_free(vals);
    v_free(parsed);
    return out;
}

V *bi_govee_lan_probe(V **a, int n) {
    P(n < 1 || a[0]->t != T_STR, v_err("govee_lan_probe(ip)"))
    if (govee_skip_lan()) return v_list(0);

    int listen_fd = -1;
    if (govee_bind_listen(&listen_fd) != 0)
        return v_err("govee: cannot bind UDP port 4002");

    govee_send_scan_to(a[0]->s, 0);

    V *devices = v_list(0);
    char buf[4096];
    for (int attempt = 0; attempt < 15; attempt++) {
        struct pollfd pfd = {listen_fd, POLLIN, 0};
        if (poll(&pfd, 1, 200) <= 0) continue;
        struct sockaddr_in from;
        socklen_t fromlen = sizeof from;
        ssize_t nr = recvfrom(listen_fd, buf, sizeof buf - 1, 0,
                              (struct sockaddr *)&from, &fromlen);
        if (nr <= 0) continue;
        buf[nr] = 0;
        V *dev = govee_parse_scan_device(buf, &from);
        if (!dev) continue;
        v_list_append(devices, dev);
        v_free(dev);
        break;
    }
    close(listen_fd);
    return devices;
}

V *bi_govee_lan_scan(V **a, int n) {
    (void)a;
    int timeout_ms = 3000;
    if (n > 0 && a[0]->t == T_INT) timeout_ms = (int)a[0]->j;
    if (timeout_ms < 100) timeout_ms = 100;
    if (govee_skip_lan()) return v_list(0);

    int listen_fd = -1;
    if (govee_bind_listen(&listen_fd) != 0)
        return v_err("govee: cannot bind UDP port 4002 (another Govee integration may be running)");

    V *devices = v_list(0);
    govee_send_scan(listen_fd);

    char buf[4096];
    int elapsed = 0;
    while (elapsed < timeout_ms) {
        struct pollfd pfd = {listen_fd, POLLIN, 0};
        int pr = poll(&pfd, 1, 100);
        if (pr < 0) break;
        elapsed += 100;
        if (pr == 0) {
            if (elapsed == 100 || elapsed == 1100 || elapsed == 2100)
                govee_send_scan(listen_fd);
            continue;
        }
        struct sockaddr_in from;
        socklen_t fromlen = sizeof from;
        ssize_t nr = recvfrom(listen_fd, buf, sizeof buf - 1, 0,
                              (struct sockaddr *)&from, &fromlen);
        if (nr <= 0) continue;
        buf[nr] = 0;
        V *dev = govee_parse_scan_device(buf, &from);
        if (!dev) continue;
        V *dev_id = v_dict_get(dev, "device");
        if (dev_id && dev_id->t == T_STR && govee_device_seen(devices, dev_id->s)) {
            v_free(dev);
            continue;
        }
        v_list_append(devices, dev);
        v_free(dev);
    }
    close(listen_fd);
    return devices;
}

V *bi_govee_lan_send(V **a, int n) {
    P(n < 2 || a[0]->t != T_STR || a[1]->t != T_STR, v_err("govee_lan_send(ip, json)"))
    if (govee_skip_lan()) return v_bool(1);
    if (govee_send_to_ip(a[0]->s, a[1]->s) != 0)
        return v_err("govee: send failed");
    return v_bool(1);
}

V *bi_govee_lan_status(V **a, int n) {
    P(n < 1 || a[0]->t != T_STR, v_err("govee_lan_status(ip)"))
    if (govee_skip_lan()) return v_err("govee: LAN skipped");

    int listen_fd = -1;
    if (govee_bind_listen(&listen_fd) != 0)
        return v_err("govee: cannot bind UDP port 4002");

    if (govee_send_to_ip(a[0]->s, GOVEE_STATUS_JSON) != 0) {
        close(listen_fd);
        return v_err("govee: status send failed");
    }

    char buf[4096];
    V *out = NULL;
    for (int attempt = 0; attempt < 30; attempt++) {
        struct pollfd pfd = {listen_fd, POLLIN, 0};
        if (poll(&pfd, 1, 100) <= 0) continue;
        struct sockaddr_in from;
        socklen_t fromlen = sizeof from;
        ssize_t nr = recvfrom(listen_fd, buf, sizeof buf - 1, 0,
                              (struct sockaddr *)&from, &fromlen);
        if (nr <= 0) continue;
        buf[nr] = 0;
        out = govee_parse_status(buf);
        if (out && out->t != T_ERR) break;
        if (out) { v_free(out); out = NULL; }
    }
    close(listen_fd);
    if (!out) return v_err("govee: status timeout");
    return out;
}

V *bi_govee_cmd_turn(V **a, int n) {
    P(n < 1, v_err("govee_cmd_turn(on)"))
    int on = 0;
    if (a[0]->t == T_BOOL) on = a[0]->b;
    else if (a[0]->t == T_INT) on = a[0]->j != 0;
    else return v_err("govee_cmd_turn(on)");
    char buf[128];
    snprintf(buf, sizeof buf, "{\"msg\":{\"cmd\":\"turn\",\"data\":{\"value\":%d}}}", on ? 1 : 0);
    return v_str(buf);
}

V *bi_govee_cmd_brightness(V **a, int n) {
    P(n < 1 || a[0]->t != T_INT, v_err("govee_cmd_brightness(0..100)"))
    int v = (int)a[0]->j;
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    char buf[128];
    snprintf(buf, sizeof buf, "{\"msg\":{\"cmd\":\"brightness\",\"data\":{\"value\":%d}}}", v);
    return v_str(buf);
}

V *bi_govee_cmd_color(V **a, int n) {
    P(n < 3 || a[0]->t != T_INT || a[1]->t != T_INT || a[2]->t != T_INT,
      v_err("govee_cmd_color(r, g, b[, kelvin])"))
    int r = (int)a[0]->j, g = (int)a[1]->j, b = (int)a[2]->j;
    int kelvin = 0;
    if (n > 3 && a[3]->t == T_INT) kelvin = (int)a[3]->j;
    if (r < 0) r = 0; if (r > 255) r = 255;
    if (g < 0) g = 0; if (g > 255) g = 255;
    if (b < 0) b = 0; if (b > 255) b = 255;
    if (kelvin < 0) kelvin = 0;
    char buf[256];
    snprintf(buf, sizeof buf,
             "{\"msg\":{\"cmd\":\"colorwc\",\"data\":{\"color\":{\"r\":%d,\"g\":%d,\"b\":%d},\"colorTemInKelvin\":%d}}}",
             r, g, b, kelvin);
    return v_str(buf);
}

V *bi_govee_get_model(V **a, int n) {
    (void)a; (void)n;
    govee_init();
    return v_str(g_govee_model);
}

V *bi_govee_get_ip(V **a, int n) {
    (void)a; (void)n;
    govee_init();
    return g_govee_ip[0] ? v_str(g_govee_ip) : v_nil();
}

V *bi_govee_set_ip(V **a, int n) {
    govee_init();
    P(n < 1 || a[0]->t != T_STR, v_err("govee_set_ip(ip)"))
    strncpy(g_govee_ip, a[0]->s, sizeof g_govee_ip - 1);
    g_govee_ip[sizeof g_govee_ip - 1] = 0;
    return v_nil();
}
