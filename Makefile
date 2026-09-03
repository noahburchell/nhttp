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

WARN := -Wall -Wextra -Wpedantic \
	-Wshadow -Wundef -Wvla -Walloca -Wwrite-strings -Wpointer-arith \
	-Wcast-align=strict -Wcast-qual -Wbad-function-cast -Wcast-function-type \
	-Wconversion -Wsign-conversion -Wfloat-equal -Wdouble-promotion \
	-Wshift-overflow=2 -Warray-bounds=2 -Wstringop-overflow=4 \
	-Wformat=2 -Wformat-overflow=2 -Wformat-truncation=2 -Wformat-signedness \
	-Wstrict-prototypes -Wold-style-definition -Wmissing-prototypes \
	-Wmissing-declarations -Wmissing-include-dirs -Wnested-externs \
	-Wredundant-decls -Wunused-macros -Winit-self \
	-Wswitch-enum -Wimplicit-fallthrough=5 \
	-Wduplicated-cond -Wduplicated-branches -Wlogical-op \
	-Wjump-misses-init -Wnull-dereference -Wtrampolines \
	-Wdisabled-optimization -Wstack-protector -Woverlength-strings

BIN = nhttp
SRC = src/main.c

all: $(BIN)

$(BIN): $(SRC)
	$(CC) $(REQUIRED_CPPFLAGS) $(CPPFLAGS) $(REQUIRED_CFLAGS) $(CFLAGS) $(WARN) $(REQUIRED_LDFLAGS) $(LDFLAGS) -o $@ $(SRC)

install: $(BIN)
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(BIN) $(DESTDIR)$(BINDIR)/$(BIN)

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(BIN)

clean:
	rm -f $(BIN)

.PHONY: all install uninstall clean
