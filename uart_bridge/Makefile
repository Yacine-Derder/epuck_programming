PROG = uart_bridge
EPUCKLIBROOT = ../epuck/EpuckDevelopmentTree/library
CC = pic30-elf-gcc
CPPFLAGS = -I. -I$(EPUCKLIBROOT) -I$(EPUCKLIBROOT)/std_microchip/support/h -I$(EPUCKLIBROOT)/std_microchip/include
CFLAGS = -mcpu=30F6014A -O1 -Wall -Wextra
LDFLAGS = -mcpu=30F6014A -L$(EPUCKLIBROOT)/std_microchip/lib -Wl,--defsym=__ICD2RAM=1 -Wl,--script=$(EPUCKLIBROOT)/std_microchip/support/gld/p30f6014a.gld
LDLIBS = -Wl,--start-group -lpic30-elf -lm-elf -lc-elf -Wl,--end-group
# Build selected library sources locally; avoid relying on stale prebuilt objects.
OBJS = main.o protocol.o e_init_port.o e_led.o e_motors.o e_ad_conv.o e_prox_timer2.o

.PHONY: all clean test
all: $(PROG).hex
$(PROG).hex: $(PROG).cof
	pic30-elf-bin2hex $<
$(PROG).cof: $(OBJS)
	$(CC) $(LDFLAGS) $(OBJS) $(LDLIBS) -o $@
e_init_port.o e_led.o e_motors.o: %.o: $(EPUCKLIBROOT)/motor_led/%.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
e_ad_conv.o e_prox_timer2.o: %.o: $(EPUCKLIBROOT)/a_d/%.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
main.o protocol.o: protocol.h
clean:
	rm -f $(OBJS) $(PROG).cof $(PROG).hex
test:
	gcc -std=c99 -Wall -Wextra -Werror -I. protocol.c tests/test_protocol.c -o /tmp/epuck_bridge_protocol_test
	/tmp/epuck_bridge_protocol_test
