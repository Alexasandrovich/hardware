#ifndef RBOT_PROTOCOL_H
#define RBOT_PROTOCOL_H

#include <stdbool.h>
#include <stdint.h>

#define RBOT_PROTO_VERSION "RBOT/1"
#define RBOT_FW_VERSION    "FW1.0.0"
#define RBOT_RX_MAX        128

typedef struct {
    int      seq;
    char     cmd[16];
    char     args[4][32];
    int      arg_count;
    bool     bad_cs;
} rbot_frame_t;

uint8_t rbot_xor_checksum(const char *body);
bool rbot_parse_line(const char *line, rbot_frame_t *out);
int rbot_build_frame(char *buf, int buf_len, int seq, const char *cmd, ...);

#endif
