CC      ?= cc
CFLAGS  ?= -O2
CPPFLAGS ?=
LDFLAGS ?=

PREFIX  ?= /usr
BINDIR  ?= $(PREFIX)/bin

REQUIRED_CPPFLAGS = -D_DEFAULT_SOURCE -D_FILE_OFFSET_BITS=64
REQUIRED_CFLAGS   = -std=c11 -Wall -Wextra

BIN = nhttp
SRC = src/main.c

all: $(BIN)

$(BIN): $(SRC)
	$(CC) $(REQUIRED_CPPFLAGS) $(CPPFLAGS) $(REQUIRED_CFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $(SRC)

install: $(BIN)
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(BIN) $(DESTDIR)$(BINDIR)/$(BIN)

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(BIN)

clean:
	rm -f $(BIN)

.PHONY: all install uninstall clean
