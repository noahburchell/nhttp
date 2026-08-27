CC      ?= cc
CFLAGS  ?= -O2
CPPFLAGS ?=
LDFLAGS ?=

PREFIX  ?= /usr
BINDIR  ?= $(PREFIX)/bin

REQUIRED_CPPFLAGS = -D_DEFAULT_SOURCE -D_FILE_OFFSET_BITS=64 -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=3
REQUIRED_CFLAGS   = -std=c11 -Wall -Wextra \
                    -fstack-protector-strong -fstack-clash-protection \
                    -fcf-protection=full -fPIE
REQUIRED_LDFLAGS  = -pie -Wl,-z,relro,-z,now,-z,noexecstack

BIN = nhttp
SRC = src/main.c

all: $(BIN)

$(BIN): $(SRC)
	$(CC) $(REQUIRED_CPPFLAGS) $(CPPFLAGS) $(REQUIRED_CFLAGS) $(CFLAGS) $(REQUIRED_LDFLAGS) $(LDFLAGS) -o $@ $(SRC)

install: $(BIN)
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(BIN) $(DESTDIR)$(BINDIR)/$(BIN)

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(BIN)

clean:
	rm -f $(BIN)

.PHONY: all install uninstall clean
