#include "rbot_app.h"
#include "rbot_protocol.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static rbot_hal_t g_hal;
static rbot_state_t g_state = RBOT_DISARMED;
static int g_left = 0;
static int g_right = 0;
static int g_err = 0;
static uint32_t g_last_cmd_ms = 0;
static char g_rx[RBOT_RX_MAX];
static char g_line[RBOT_RX_MAX];
static int g_rx_len = 0;
static volatile bool g_line_ready = false;
static bool g_wd_fired = false;

static int clamp_speed(int v)
{
    int lim = g_hal.pwm_max;
    if (lim > 100) lim = 100;
    if (lim < 0) lim = 0;
    if (v > lim) return lim;
    if (v < -lim) return -lim;
    return v;
}

static void stop_motors(void)
{
    g_left = 0;
    g_right = 0;
    if (g_hal.motor_set) {
        g_hal.motor_set(0, 0);
    }
}

static void touch_wd(void)
{
    if (g_hal.millis) {
        g_last_cmd_ms = g_hal.millis();
    }
}

static void tx_frame(int seq, const char *cmd, ...)
{
    char buf[RBOT_RX_MAX];
    char body[RBOT_RX_MAX];
    int n = snprintf(body, sizeof(body), "%04d:%s", seq % 10000, cmd);

    va_list ap;
    va_start(ap, cmd);
    const char *arg = va_arg(ap, const char *);
    while (arg) {
        int m = snprintf(body + n, sizeof(body) - (size_t)n, ":%s", arg);
        if (m > 0) n += m;
        arg = va_arg(ap, const char *);
    }
    va_end(ap);

    uint8_t cs = rbot_xor_checksum(body);
    snprintf(buf, sizeof(buf), "@%s*%02X\r\n", body, cs);
    if (g_hal.uart_write) {
        g_hal.uart_write(buf);
    }
}

static void tx_evt(const char *code, const char *detail)
{
    char buf[RBOT_RX_MAX];
    char body[RBOT_RX_MAX];
    snprintf(body, sizeof(body), "0000:EVT:%s:%s", code, detail);
    uint8_t cs = rbot_xor_checksum(body);
    snprintf(buf, sizeof(buf), "@%s*%02X\r\n", body, cs);
    if (g_hal.uart_write) {
        g_hal.uart_write(buf);
    }
}

static void reply_ack(int seq, const char *cmd)
{
    if (seq >= 0) {
        tx_frame(seq, "ACK", cmd, NULL);
    }
}

static void reply_nak(int seq, const char *cmd, const char *reason)
{
    if (seq >= 0) {
        tx_frame(seq, "NAK", cmd, reason, NULL);
    }
}

static void apply_drive(int left, int right)
{
    g_left = clamp_speed(left);
    g_right = clamp_speed(right);
    if (g_hal.motor_set) {
        g_hal.motor_set(g_left, g_right);
    }
}

static void handle_frame(const rbot_frame_t *f)
{
    int seq = f->seq;

    if (f->bad_cs || strcmp(f->cmd, "INVALID") == 0) {
        reply_nak(seq, "FRAME", "BAD_CS");
        return;
    }

    if (strcmp(f->cmd, "HELLO") == 0) {
        g_state = RBOT_DISARMED;
        g_err = 0;
        g_wd_fired = false;
        stop_motors();
        touch_wd();
        tx_frame(seq, "HELLO_ACK", RBOT_PROTO_VERSION, "DISARMED", RBOT_FW_VERSION, NULL);
        return;
    }

    if (strcmp(f->cmd, "PING") == 0) {
        touch_wd();
        char seqs[8];
        snprintf(seqs, sizeof(seqs), "%04d", seq);
        tx_frame(seq, "PONG", seqs, NULL);
        return;
    }

    if (strcmp(f->cmd, "ARM") == 0) {
        if (g_state == RBOT_FAULT) {
            reply_nak(seq, "ARM", "FAULT");
            return;
        }
        g_state = RBOT_ARMED;
        touch_wd();
        reply_ack(seq, "ARM");
        return;
    }

    if (strcmp(f->cmd, "DISARM") == 0) {
        g_state = RBOT_DISARMED;
        stop_motors();
        touch_wd();
        reply_ack(seq, "DISARM");
        return;
    }

    if (strcmp(f->cmd, "STOP") == 0) {
        stop_motors();
        touch_wd();
        reply_ack(seq, "STOP");
        return;
    }

    if (strcmp(f->cmd, "ESTOP") == 0) {
        g_state = RBOT_FAULT;
        g_err = 2;
        stop_motors();
        const char *reason = f->arg_count > 0 ? f->args[0] : "unknown";
        reply_ack(seq, "ESTOP");
        tx_evt("FAULT_ESTOP", reason);
        return;
    }

    if (strcmp(f->cmd, "RESET") == 0) {
        if (g_state != RBOT_FAULT) {
            reply_nak(seq, "RESET", "BAD_ARGS");
            return;
        }
        g_state = RBOT_DISARMED;
        g_err = 0;
        stop_motors();
        reply_ack(seq, "RESET");
        return;
    }

    if (strcmp(f->cmd, "GET_STATUS") == 0) {
        touch_wd();
        char status[80];
        const char *st = (g_state == RBOT_ARMED) ? "ARMED" :
                         (g_state == RBOT_FAULT) ? "FAULT" : "DISARMED";
        snprintf(status, sizeof(status), "%s:L:%d:R:%d:WD:%d:ERR:%d",
                 st, g_left, g_right, g_hal.wd_ms, g_err);
        tx_frame(seq, "STATUS", status, NULL);
        return;
    }

    if (strcmp(f->cmd, "SET_CFG") == 0) {
        if (f->arg_count < 2) {
            reply_nak(seq, "SET_CFG", "BAD_ARGS");
            return;
        }
        if (strcmp(f->args[0], "pwm_max") == 0) {
            g_hal.pwm_max = atoi(f->args[1]);
            if (g_hal.pwm_max > 100) g_hal.pwm_max = 100;
            if (g_hal.pwm_max < 0) g_hal.pwm_max = 0;
        } else if (strcmp(f->args[0], "wd_ms") == 0) {
            g_hal.wd_ms = atoi(f->args[1]);
            if (g_hal.wd_ms < 100) g_hal.wd_ms = 100;
            if (g_hal.wd_ms > 2000) g_hal.wd_ms = 2000;
        } else {
            reply_nak(seq, "SET_CFG", "BAD_ARGS");
            return;
        }
        reply_ack(seq, "SET_CFG");
        return;
    }

    if (strcmp(f->cmd, "DRIVE") == 0) {
        if (f->arg_count < 2) {
            reply_nak(seq, "DRIVE", "BAD_ARGS");
            return;
        }
        if (g_state == RBOT_FAULT) {
            reply_nak(seq, "DRIVE", "FAULT");
            return;
        }
        if (g_state != RBOT_ARMED) {
            reply_nak(seq, "DRIVE", "NOT_ARMED");
            return;
        }
        apply_drive(atoi(f->args[0]), atoi(f->args[1]));
        touch_wd();
        reply_ack(seq, "DRIVE");
        return;
    }

    reply_nak(seq, f->cmd, "UNKNOWN_CMD");
}

void rbot_app_init(const rbot_hal_t *hal)
{
    g_hal = *hal;
    if (g_hal.pwm_max <= 0) g_hal.pwm_max = 60;
    if (g_hal.wd_ms <= 0) g_hal.wd_ms = 500;
    g_state = RBOT_DISARMED;
    g_rx_len = 0;
    stop_motors();
    touch_wd();
    if (g_hal.uart_write) {
        g_hal.uart_write("RBOT FW1.0.0 ready\r\n");
    }
}

static void process_line(const char *line)
{
    rbot_frame_t frame;
    if (rbot_parse_line(line, &frame)) {
        handle_frame(&frame);
    }
}

void rbot_app_on_byte(uint8_t ch)
{
    if (ch == '\r') {
        return;
    }
    if (ch == '\n') {
        if (g_rx_len == 0) {
            return;
        }
        if (!g_line_ready) {
            memcpy(g_line, g_rx, (size_t)g_rx_len);
            g_line[g_rx_len] = '\0';
            g_line_ready = true;
        }
        g_rx_len = 0;
        return;
    }
    if (g_rx_len < RBOT_RX_MAX - 1) {
        g_rx[g_rx_len++] = (char)ch;
    } else {
        g_rx_len = 0;
    }
}

void rbot_app_tick(void)
{
    if (g_line_ready) {
        g_line_ready = false;
        process_line(g_line);
    }

    if (g_state != RBOT_ARMED || !g_hal.millis) {
        return;
    }
    uint32_t now = g_hal.millis();
    if (now - g_last_cmd_ms > (uint32_t)g_hal.wd_ms) {
        stop_motors();
        g_state = RBOT_DISARMED;
        g_err = 1;
        if (!g_wd_fired) {
            g_wd_fired = true;
            char detail[16];
            snprintf(detail, sizeof(detail), "%lu", (unsigned long)(now - g_last_cmd_ms));
            tx_evt("WD_TRIGGER", detail);
        }
    }
}

rbot_state_t rbot_app_state(void) { return g_state; }
int rbot_app_left(void) { return g_left; }
int rbot_app_right(void) { return g_right; }
