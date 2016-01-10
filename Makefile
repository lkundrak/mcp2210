NAME = mcp2210
VERSION = 1.0
DIST = $(NAME)-$(VERSION)
SONAME = libmcp2210.so.1

CFLAGS = -Wall -g -O0

override POD2MAN_FLAGS += --utf8
override POD2MAN_FLAGS += --date 2016-01-10
override POD2MAN_FLAGS += --center "MCP2210 Library"
override POD2MAN_FLAGS += --release $(DIST)

MAN1 += mcp2210-util.1
MAN3 += libmcp2210.3
MAN3 += libmcp2210_general.3
MAN3 += libmcp2210_eeprom.3
MAN3 += libmcp2210_status.3
MAN3 += libmcp2210_chip.3
MAN3 += libmcp2210_gpio.3
MAN3 += libmcp2210_spi.3
MAN3 += libmcp2210_usb.3
DOC = mcp2210.pdf
LIB = libmcp2210.so.$(VERSION)

PREFIX = /usr/local
BINDIR = $(DESTDIR)$(PREFIX)/bin
LIBDIR = $(DESTDIR)$(PREFIX)/lib
INCLUDEDIR = $(DESTDIR)$(PREFIX)/include
MANDIR = $(DESTDIR)$(PREFIX)/share/man
MAN1DIR = $(MANDIR)/man1
MAN3DIR = $(MANDIR)/man3
DOCDIR = $(DESTDIR)$(PREFIX)/share/doc/$(NAME)

all: mcp2210-util $(DOC) $(MAN) $(LIB)
mcp2210.o: mcp2210.h
mcp2210-util.o: mcp2210.h
mcp2210-util: mcp2210-util.o mcp2210.o

%.1: %.pod
	pod2man --section 1 $(POD2MAN_FLAGS) $< >$@

%.3: %.pod
	pod2man --section 3 $(POD2MAN_FLAGS) $< >$@

mcp2210.pdf: $(MAN1) $(MAN3)
	groff -Tpdf -man $(MAN1) $(MAN3) >$@

$(LIB): mcp2210.c
	$(CC) -fPIC -shared -Wl,-soname=$(SONAME) -o $@ $<

dist:
	git archive --prefix=$(DIST)/ HEAD |gzip >$(DIST).tar.gz

distcheck: dist
	rm -rf $(DIST)
	tar xzf $(DIST).tar.gz
	$(MAKE) -C $(DIST)
	$(MAKE) -C $(DIST) install PREFIX=instdir

install:
	mkdir -p $(BINDIR) $(MAN1DIR) $(MAN3DIR) $(DOCDIR) $(LIBDIR)
	install -m755 mcp2210-util $(BINDIR)
	install -m644 $(MAN1) $(MAN1DIR)
	install -m644 $(MAN3) $(MAN3DIR)
	install -m644 $(LIB) $(LIBDIR)
	ln -sf $(LIB) $(LIBDIR)/$(SONAME)
	ln -sf $(LIB) $(LIBDIR)/libmcp2210.so
	install -m644 mcp2210.h $(INCLUDEDIR)
	-install -m644 $(DOC) $(DOCDIR)

clean:
	rm -rf mcp2210-util mcp2210.pdf *.o *.3 *.so* instdir $(DIST)
