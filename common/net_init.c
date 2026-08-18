#include "net_init.h"

#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int read_ipv4(const char *ifname, char *out, size_t out_len) {
    char cmd[128];
    snprintf(cmd, sizeof(cmd),
             "ip -4 -o addr show dev %s 2>/dev/null | awk '{print $4}' | cut -d/ -f1",
             ifname);
    FILE *fp = popen(cmd, "r");
    if (!fp) {
        return -1;
    }
    if (!fgets(out, (int)out_len, fp)) {
        pclose(fp);
        return -1;
    }
    pclose(fp);
    size_t n = strlen(out);
    if (n > 0 && out[n - 1] == '\n') {
        out[n - 1] = '\0';
    }
    return out[0] ? 0 : -1;
}

static int iface_carrier_up(const char *ifname) {
    char path[128];
    snprintf(path, sizeof(path), "/sys/class/net/%s/carrier", ifname);
    FILE *fp = fopen(path, "r");
    if (!fp) {
        return 0;
    }
    int v = 0;
    if (fscanf(fp, "%d", &v) != 1) {
        v = 0;
    }
    fclose(fp);
    return v == 1;
}

static const char *pick_iface(void) {
    if (access("/sys/class/net/eth0", F_OK) == 0) {
        return "eth0";
    }
    if (access("/sys/class/net/end0", F_OK) == 0) {
        return "end0";
    }
    return NULL;
}

int net_init_from_ini(const IniConfig *cfg) {
    const char *want_ip = ini_get(cfg, "eth0StaticIp", "");
    const char *want_mask = ini_get(cfg, "eth0Netmask", "255.255.255.0");
    int simulate = ini_get_int(cfg, "simulateNetwork", 1);
    const char *iface = pick_iface();

    if (!iface) {
        LOGW("net_init: no eth0/end0, simulate mode (resume: 静态 IP 待接网线后生效)");
        return 0;
    }

    if (!want_ip[0]) {
        LOGI("net_init: %s no static IP configured", iface);
        return 0;
    }

    char cur[64] = {0};
    int have_ip = (read_ipv4(iface, cur, sizeof(cur)) == 0 && strcmp(cur, want_ip) == 0);
    if (have_ip) {
        LOGI("net_init: %s already %s", iface, cur);
    } else {
        char cmd[256];
        snprintf(cmd, sizeof(cmd),
                 "ip addr flush dev %s 2>/dev/null; ip addr add %s/%s dev %s; ip link set %s up",
                 iface, want_ip, want_mask, iface, iface);
        if (system(cmd) != 0) {
            LOGE("net_init: set %s %s failed", iface, want_ip);
            return -1;
        }
    }

    if (simulate || !iface_carrier_up(iface)) {
        LOGW("net_init: %s configured %s/%s without carrier (软件模拟静态 IP，供本地 RTSP)",
             iface, want_ip, want_mask);
        char lo_cmd[128];
        snprintf(lo_cmd, sizeof(lo_cmd), "ip addr add %s/32 dev lo 2>/dev/null || true",
                 want_ip);
        (void)system(lo_cmd);
    } else if (!have_ip) {
        LOGI("net_init: configured %s static %s/%s", iface, want_ip, want_mask);
    }
    sleep(1);
    return 0;
}
