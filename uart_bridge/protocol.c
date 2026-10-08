#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "protocol.h"

static int number(const char **cursor, long *value)
{
    char *end;
    const char *p = *cursor;
    while (*p == ' ' || *p == '\t') ++p;
    if (!*p) return 0;
    errno = 0;
    *value = strtol(p, &end, 10);
    if (p == end || errno == ERANGE) return 0;
    if (*end && *end != ' ' && *end != '\t') return 0;
    *cursor = end;
    return 1;
}

static int end_of_line(const char *p)
{
    while (*p == ' ' || *p == '\t') ++p;
    return *p == '\0';
}

Command parse_command(const char *line)
{
    Command cmd = {CMD_ERROR, 0, 0, 0};
    const char *p;
    long a, b;
    if (!strcmp(line, "PING")) cmd.type = CMD_PING;
    else if (!strcmp(line, "ARM")) cmd.type = CMD_ARM;
    else if (!strcmp(line, "STOP")) cmd.type = CMD_STOP;
    else if (!strcmp(line, "R")) cmd.type = CMD_READ;
    else if (!strcmp(line, "HB")) cmd.type = CMD_HEARTBEAT;
    else if (!strcmp(line, "STATUS")) cmd.type = CMD_STATUS;
    else if (line[0] == 'M' && line[1] == ' ') {
        p = line + 2;
        if (number(&p, &a) && number(&p, &b) && end_of_line(p)
                && a >= -1000 && a <= 1000 && b >= -1000 && b <= 1000) {
            cmd.type = CMD_MOTORS;
            cmd.left = (int)a;
            cmd.right = (int)b;
        }
    } else if (!strncmp(line, "STREAM ", 7)) {
        p = line + 7;
        if (number(&p, &a) && end_of_line(p)
                && (a == 0 || (a >= MIN_STREAM_MS && a <= MAX_STREAM_MS))) {
            cmd.type = CMD_STREAM;
            cmd.period_ms = (unsigned int)a;
        }
    }
    return cmd;
}
