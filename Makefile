CC       ?= cc
CPPFLAGS ?= -D_POSIX_C_SOURCE=200809L -Iinclude
CFLAGS   ?= -Wall -Wextra -Wpedantic -O2
LDLIBS   ?= -lg15daemon_client -lg15render

PREFIX          ?= /usr/local
DESTDIR         ?=
BINDIR           ?= $(PREFIX)/bin
SYSTEMD_USER_DIR ?= $(PREFIX)/lib/systemd/user
DOCDIR           ?= $(PREFIX)/share/doc/g15netspeed

TARGET       = g15netspeed
KEYTEST      = g15keytest
CORE_SOURCES = src/config.c src/history.c src/metrics.c src/network.c
SOURCES      = src/main.c src/display.c $(CORE_SOURCES)
TEST_TARGET  = build/test-core
SERVICE      = build/g15netspeed.service

.PHONY: all clean install test test-core tools FORCE

all: $(TARGET)

$(TARGET): $(SOURCES) $(wildcard include/g15netspeed/*.h)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $(SOURCES) $(LDLIBS)

tools: $(KEYTEST)

$(KEYTEST): tools/g15keytest.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $< -lg15daemon_client

build:
	mkdir -p $@

$(SERVICE): packaging/systemd/g15netspeed.service.in FORCE | build
	sed 's|@BINDIR@|$(BINDIR)|g' $< > $@

FORCE:

$(TEST_TARGET): tests/test-core.c $(CORE_SOURCES) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ tests/test-core.c $(CORE_SOURCES)

test-core: $(TEST_TARGET)
	./$(TEST_TARGET)

test: $(TARGET) test-core
	sh tests/test-cli.sh ./$(TARGET)

clean:
	rm -f $(TARGET) $(KEYTEST)
	rm -rf build

install: $(TARGET) $(SERVICE)
	install -Dm755 $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)
	install -Dm644 $(SERVICE) \
		$(DESTDIR)$(SYSTEMD_USER_DIR)/g15netspeed.service
	install -Dm644 config/g15netspeed.conf.example \
		$(DESTDIR)$(DOCDIR)/g15netspeed.conf.example
	install -Dm644 docs/DPMS-NVIDIA-HINWEIS.md \
		$(DESTDIR)$(DOCDIR)/DPMS-NVIDIA-HINWEIS.md
