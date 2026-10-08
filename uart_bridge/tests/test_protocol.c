#include <assert.h>
#include <stdio.h>
#include "protocol.h"
int main(void)
{
    Command c;
    const char *invalid[] = {"", "ping", "PING extra", "M", "M 1", "M 1 2 3",
        "M 1001 0", "M 0 -1001", "M 1x 2", "M 1 2x", "M 1.0 2", "M + 2",
        "M 99999999999999999999999999999 0", "STREAM -1", "STREAM 49",
        "STREAM 10001", "STREAM 50x", "STREAM 100 extra", "ARM extra"};
    unsigned int i;
    assert(parse_command("PING").type == CMD_PING);
    assert(parse_command("ARM").type == CMD_ARM);
    assert(parse_command("STOP").type == CMD_STOP);
    assert(parse_command("R").type == CMD_READ);
    assert(parse_command("HB").type == CMD_HEARTBEAT);
    assert(parse_command("STATUS").type == CMD_STATUS);
    c = parse_command("M -1000 1000");
    assert(c.type == CMD_MOTORS && c.left == -1000 && c.right == 1000);
    assert(parse_command("M 0 0").type == CMD_MOTORS);
    assert(parse_command("M 1 2  ").type == CMD_MOTORS);
    assert(parse_command("STREAM 0").period_ms == 0);
    assert(parse_command("STREAM 50").type == CMD_STREAM);
    assert(parse_command("STREAM 10000").period_ms == 10000);
    for (i = 0; i < sizeof(invalid)/sizeof(invalid[0]); ++i)
        assert(parse_command(invalid[i]).type == CMD_ERROR);
    puts("Protocol tests passed: commands, limits, malformed input and overflow.");
    return 0;
}
