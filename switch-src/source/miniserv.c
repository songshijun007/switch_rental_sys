#include "miniserv.h"
#include "sha256.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>

static void url_decode(char *dst, const char *src, size_t dst_sz) {
    size_t di = 0;
    while (*src && di + 1 < dst_sz) {
        if (*src == '%' && src[1] && src[2]) {
            char hex[3] = { src[1], src[2], 0 };
            dst[di++] = (char)strtol(hex, NULL, 16);
            src += 3;
        } else if (*src == '+') {
            dst[di++] = ' ';
            src++;
        } else {
            dst[di++] = *src++;
        }
    }
    dst[di] = 0;
}

static void send_all(int fd, const char *data, size_t len) {
    while (len > 0) {
        ssize_t n = send(fd, data, len, 0);
        if (n <= 0) return;
        data += n;
        len -= (size_t)n;
    }
}

static void send_response(int fd, const char *status, const char *ctype,
                          const char *body, size_t body_len) {
    char hdr[512];
    int hn = snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 %s\r\n"
        "Content-Type: %s; charset=utf-8\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "Cache-Control: no-store\r\n"
        "\r\n", status, ctype, body_len);
    send_all(fd, hdr, (size_t)hn);
    if (body && body_len) send_all(fd, body, body_len);
}

static const char *html_escape_buf(char *buf, size_t sz, const char *s) {
    size_t o = 0;
    while (*s && o + 8 < sz) {
        if (*s == '<') { memcpy(buf + o, "&lt;", 4); o += 4; }
        else if (*s == '>') { memcpy(buf + o, "&gt;", 4); o += 4; }
        else if (*s == '&') { memcpy(buf + o, "&amp;", 5); o += 5; }
        else if (*s == '"') { memcpy(buf + o, "&quot;", 6); o += 6; }
        else buf[o++] = *s;
        s++;
    }
    buf[o] = 0;
    return buf;
}

// Cleaner renderer that writes directly.
static void build_page(miniserv_t *srv, char *out, size_t out_sz,
                       const char *flash_msg, int flash_ok) {
    uint64_t now = (uint64_t)time(NULL);
    int cur, paid;
    rental_status_t st = rental_check(srv->cfg, srv->state, now, &cur, &paid);
    const char *mc = rental_machine_code(srv->state);
    uint64_t period_len = (uint64_t)srv->cfg->period_days * 86400ULL;

    char paid_str[48];
    if (paid >= 0) {
        uint64_t paid_end = RENTAL_EPOCH + (uint64_t)(paid + 1) * period_len - 1ULL;
        time_t t = (time_t)paid_end;
        struct tm *tm = gmtime(&t);
        if (tm) {
            strftime(paid_str, sizeof(paid_str), "#%d · 截止 %Y-%m-%d", tm);
            // Prefix with the period number.
            size_t plen = strlen(paid_str);
            snprintf(paid_str + 8, sizeof(paid_str) - 8, "· 截止 %Y-%m-%d UTC", tm);
            snprintf(paid_str, sizeof(paid_str), "#%d ", paid);
            char date[32];
            strftime(date, sizeof(date), "%Y-%m-%d", tm);
            snprintf(paid_str, sizeof(paid_str), "#%d · 截止 %s", paid, date);
            (void)plen;
        } else {
            snprintf(paid_str, sizeof(paid_str), "#%d", paid);
        }
    } else {
        snprintf(paid_str, sizeof(paid_str), "尚未解锁");
    }

    const char *status_text, *status_color;
    switch (st) {
        case RENTAL_OK:       status_text = "已解锁"; status_color = "#10b981"; break;
        case RENTAL_IN_GRACE: status_text = "宽限期"; status_color = "#f59e0b"; break;
        default:              status_text = "已锁定"; status_color = "#ef4444"; break;
    }

    char mc_safe[80] = {0}, flash_safe[512] = "";
    html_escape_buf(mc_safe, sizeof(mc_safe), mc);
    if (flash_msg) html_escape_buf(flash_safe, sizeof(flash_safe), flash_msg);

    const char *flash_html = "";
    char flash_block[768];
    if (flash_msg) {
        snprintf(flash_block, sizeof(flash_block),
            "<div class='flash %s'>%s</div>",
            flash_ok ? "ok" : "err", flash_safe);
        flash_html = flash_block;
    }

    snprintf(out, out_sz,
"<!doctype html><html><head><meta charset='utf-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>Switch 出租锁</title>"
"<style>"
"body{font-family:-apple-system,Segoe UI,Roboto,sans-serif;background:#0f172a;color:#e2e8f0;margin:0;padding:24px;}"
".card{max-width:560px;margin:0 auto;background:#1e293b;border-radius:16px;padding:28px;box-shadow:0 20px 40px rgba(0,0,0,.4);}"
"h1{margin-top:0;font-size:22px}"
".mc{font-family:ui-monospace,monospace;background:#0f172a;padding:10px 14px;border-radius:8px;letter-spacing:.15em;color:#34d399;font-size:20px;text-align:center;margin:10px 0;}"
"input[type=text]{width:100%%;padding:14px;font-size:24px;letter-spacing:.25em;text-align:center;font-family:monospace;border-radius:8px;border:1px solid #475569;background:#0f172a;color:#f1f5f9;box-sizing:border-box}"
"button{margin-top:14px;width:100%%;padding:14px;border:0;border-radius:8px;background:#10b981;color:#0f172a;font-weight:700;font-size:16px;cursor:pointer}"
"button:hover{background:#34d399}"
".row{display:flex;justify-content:space-between;padding:8px 0;border-bottom:1px solid #334155}"
".row span:last-child{color:#cbd5e1;font-family:ui-monospace,monospace;font-size:14px}"
".status{display:inline-block;padding:4px 10px;border-radius:6px;font-weight:700;color:#0f172a;margin-left:8px}"
".flash{margin-top:14px;padding:12px 14px;border-radius:8px}"
".flash.ok{background:#064e3b;color:#6ee7b7}"
".flash.err{background:#7f1d1d;color:#fecaca}"
"footer{margin-top:20px;text-align:center;color:#64748b;font-size:12px}"
"label{display:block;margin-top:18px;font-weight:600}"
"</style></head><body>"
"<div class='card'>"
"<h1>Switch 出租锁<span class='status' style='background:%s'>%s</span></h1>"
"<p style='color:#94a3b8;margin-top:0'>请把下方机器码发送给机主，获得 8 位数字秘钥后输入解锁。</p>"
"<div class='mc'>%s</div>"
"<div class='row'><span>出租周期</span><span>%d 天</span></div>"
"<div class='row'><span>当前周期</span><span>#%d</span></div>"
"<div class='row'><span>已解锁至</span><span>%s</span></div>"
"<form method='POST' action='/unlock'>"
"<label for='key'>输入 8 位数字秘钥</label>"
"<input id='key' name='key' type='text' maxlength='8' inputmode='numeric' pattern='[0-9]{8}' required autofocus>"
"<button type='submit'>解锁</button>"
"</form>"
"%s"
"<footer>PCTL All-in-One Rental · 完全离线校验</footer>"
"</div></body></html>",
        status_color, status_text,
        mc_safe,
        srv->cfg->period_days,
        cur,
        paid_str,
        flash_html);
}

static void handle_unlock(miniserv_t *srv, int fd, const char *body) {
    char key[32] = {0};
    // Parse a very small form. Body looks like "key=12345678".
    const char *p = strstr(body, "key=");
    if (p) {
        p += 4;
        size_t i = 0;
        while (*p && *p != '&' && i < sizeof(key) - 1) {
            key[i++] = *p++;
        }
        key[i] = 0;
        url_decode(key, key, sizeof(key));
    }

    char page[8192];
    rental_status_t rc;
    const char *msg;
    int ok;
    if (!key[0]) {
        rc = RENTAL_WRONG_KEY;
        msg = "未检测到秘钥，请输入 8 位数字。";
        ok = 0;
    } else {
        rc = rental_submit_key((rental_config_t*)srv->cfg, srv->state, key);
        if (rc == RENTAL_OK) {
            msg = "解锁成功！正在解除家长控制限制…";
            ok = 1;
            rental_enforce_unlock();
        } else if (rc == RENTAL_COOLDOWN) {
            uint64_t now = (uint64_t)time(NULL);
            int left = (int)(srv->state->cooldown_until - now);
            char b[128];
            snprintf(b, sizeof(b), "错误次数过多，请 %d 秒后再试。", left < 0 ? 0 : left);
            msg = b;
            ok = 0;
        } else {
            msg = "秘钥错误，请重新确认机器码和秘钥。";
            ok = 0;
        }
    }

    build_page(srv, page, sizeof(page), msg, ok);
    send_response(fd, (rc == RENTAL_OK) ? "200 OK" : "200 OK",
                  "text/html; charset=utf-8", page, strlen(page));

    if (rc == RENTAL_OK && srv->on_state_changed) {
        uint64_t now = (uint64_t)time(NULL);
        int cur;
        rental_status_t ns = rental_check(srv->cfg, srv->state, now, &cur, NULL);
        srv->on_state_changed(ns);
    }
}

static void handle_root(miniserv_t *srv, int fd) {
    char page[8192];
    build_page(srv, page, sizeof(page), NULL, 0);
    send_response(fd, "200 OK", "text/html; charset=utf-8", page, strlen(page));
}

static void handle_client(miniserv_t *srv, int fd) {
    char req[2048];
    memset(req, 0, sizeof(req));
    ssize_t total = 0;
    // Peek only enough to read the request line + any tiny body.
    // We set a short recv timeout so slow clients don't hang the UI.
    struct timeval tv = {2, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    while (total < (ssize_t)sizeof(req) - 1) {
        ssize_t n = recv(fd, req + total, sizeof(req) - 1 - (size_t)total, 0);
        if (n <= 0) break;
        total += n;
        if (strstr(req, "\r\n\r\n")) break;
    }
    req[total] = 0;

    const char *body = strstr(req, "\r\n\r\n");
    if (body) body += 4; else body = "";

    if (strncmp(req, "GET ", 4) == 0 || strncmp(req, "GET / ", 6) == 0) {
        handle_root(srv, fd);
    } else if (strncmp(req, "POST /unlock", 12) == 0) {
        handle_unlock(srv, fd, body);
    } else {
        const char *msg = "Not Found";
        send_response(fd, "404 Not Found", "text/plain", msg, strlen(msg));
    }
}

int miniserv_start(miniserv_t *srv, const rental_config_t *cfg,
                   rental_state_t *state,
                   void (*on_state_changed)(rental_status_t)) {
    srv->cfg = cfg;
    srv->state = state;
    srv->on_state_changed = on_state_changed;
    srv->running = 1;

    srv->sock = socket(AF_INET, SOCK_STREAM, 0);
    if (srv->sock < 0) return -1;
    int one = 1;
    setsockopt(srv->sock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(HTTP_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(srv->sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(srv->sock);
        srv->sock = -1;
        return -1;
    }
    if (listen(srv->sock, 4) < 0) {
        close(srv->sock);
        srv->sock = -1;
        return -1;
    }
    // Make listening socket non-blocking so that console input polling works.
    int flags = fcntl(srv->sock, F_GETFL, 0);
    fcntl(srv->sock, F_SETFL, flags | O_NONBLOCK);
    return 0;
}

void miniserv_stop(miniserv_t *srv) {
    srv->running = 0;
    if (srv->sock >= 0) {
        close(srv->sock);
        srv->sock = -1;
    }
}

void miniserv_pump(miniserv_t *srv) {
    if (srv->sock < 0) return;
    struct sockaddr_in ca;
    socklen_t cl = sizeof(ca);
    int fd = accept(srv->sock, (struct sockaddr*)&ca, &cl);
    if (fd < 0) return;
    handle_client(srv, fd);
    close(fd);
}
