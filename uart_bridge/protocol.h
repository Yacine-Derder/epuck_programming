#ifndef BRIDGE_PROTOCOL_H
#define BRIDGE_PROTOCOL_H
#define MAX_LINE 64
#define MIN_STREAM_MS 50
#define MAX_STREAM_MS 10000
#define MOTOR_TIMEOUT_MS 250

typedef enum {
    CMD_ERROR, CMD_PING, CMD_ARM, CMD_MOTORS, CMD_STOP,
    CMD_READ, CMD_STREAM, CMD_HEARTBEAT, CMD_STATUS
} CommandType;
typedef struct {
    CommandType type;
    int left;
    int right;
    unsigned int period_ms;
} Command;
Command parse_command(const char *line);
#endif
