CC      = gcc
CFLAGS  = -Wall -Wextra -O2
LDFLAGS = -lg15daemon_client -lg15render -lm

TARGET  = g15netspeed
SRC     = g15netspeed.c

.PHONY: all clean install

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f $(TARGET)

install: $(TARGET)
	install -Dm755 $(TARGET) /usr/local/bin/$(TARGET)
