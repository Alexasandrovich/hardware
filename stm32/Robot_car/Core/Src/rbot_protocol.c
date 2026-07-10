#include "rbot_protocol.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

uint8_t rbot_xor_checksum(const char *body)
{
    uint8_t cs = 0;
    while (body && *body) {
        cs ^= (uint8_t)*body++;
    }
    return cs;
}

static void upper_cmd(char *dst, const char *src, int n)
{
    int i;
    for (i = 0; i < n - 1 && src[i]; i++) {
        dst[i] = (char)toupper((unsigned char)src[i]);
    }
    dst[i] = '\0';
}

bool rbot_parse_line(const char *line, rbot_frame_t *out)
{
    memset(out, 0, sizeof(*out));
    out->seq = -1;

    if (!line || !*line || line[0] != '@') {
        return false;
    }

    const char *star = strrchr(line, '*');
    if (!star || star <= line + 1) {
        return false;
    }

    char body[RBOT_RX_MAX];
    int body_len = (int)(star - (line + 1));
    if (body_len >= RBOT_RX_MAX) {
        return false;
    }
    memcpy(body, line + 1, (size_t)body_len);
    body[body_len] = '\0';

    unsigned int cs_given = 0;
    if (sscanf(star + 1, "%2x", &cs_given) != 1) {
        out->bad_cs = true;
        strcpy(out->cmd, "INVALID");
        return true;
    }
    if ((uint8_t)cs_given != rbot_xor_checksum(body)) {
        out->bad_cs = true;
        strcpy(out->cmd, "INVALID");
        return true;
    }

    char *parts[16];
    int count = 0;
    char tmp[RBOT_RX_MAX];
    strncpy(tmp, body, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    char *tok = strtok(tmp, ":");
    while (tok && count < 16) {
        parts[count++] = tok;
        tok = strtok(NULL, ":");
    }
    if (count < 2) {
        return false;
    }

    out->seq = atoi(parts[0]);
    upper_cmd(out->cmd, parts[1], (int)sizeof(out->cmd));
    out->arg_count = count - 2;
    for (int i = 0; i < out->arg_count && i < 4; i++) {
        strncpy(out->args[i], parts[i + 2], sizeof(out->args[i]) - 1);
    }
    return true;
}

int rbot_build_frame(char *buf, int buf_len, int seq, const char *cmd, ...)
{
    char body[RBOT_RX_MAX];
    int n = snprintf(body, sizeof(body), "%04d:%s", seq % 10000, cmd);
    if (n < 0 || n >= (int)sizeof(body)) {
        return -1;
    }

    va_list ap;
    va_start(ap, cmd);
    const char *arg;
    while ((arg = va_arg(ap, const char *)) != NULL) {
        int m = snprintf(body + n, sizeof(body) - (size_t)n, ":%s", arg);
        if (m < 0 || n + m >= (int)sizeof(body)) {
            va_end(ap);
            return -1;
        }
        n += m;
    }
    va_end(ap);

    uint8_t cs = rbot_xor_checksum(body);
    return snprintf(buf, (size_t)buf_len, "@%s*%02X\r\n", body, cs);
}
