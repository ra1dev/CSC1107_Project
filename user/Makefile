CC ?= gcc
CFLAGS ?= -Wall -Wextra -O2 -std=c11
LDFLAGS ?=
TLS_LDLIBS ?= -lssl -lcrypto

.PHONY: all clean

all: kbmon kbmon_tls

kbmon: kbmon.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

kbmon_tls: kbmon_tls.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS) $(TLS_LDLIBS)

clean:
	rm -f kbmon kbmon_tls
