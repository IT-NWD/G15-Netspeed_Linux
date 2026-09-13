CC       ?= gcc
CPPFLAGS ?= -D_POSIX_C_SOURCE=200809L
CFLAGS   ?= -Wall -Wextra -Wpedantic -O2
LDLIBS   ?= -lg15daemon_client -lg15render

TARGET  = g15netspeed
SRC     = g15netspeed.c

.PHONY: all clean install test

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $< $(LDFLAGS) $(LDLIBS)

test: $(TARGET)
	sh tests/test-cli.sh ./$(TARGET)

clean:
	rm -f $(TARGET)

install: $(TARGET)
	install -Dm755 $(TARGET) /usr/local/bin/$(TARGET)
