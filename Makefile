VERSION = 1.1.5

PREFIX ?= /usr/local
MANDIR ?= $(PREFIX)/share/man

OBJ = clientwin.o dsimple.o xwininfo.o
LDLIBS ?= -lxcb-shape -lxcb-icccm -lxcb
CPPFLAGS += -DPACKAGE_STRING='"xwininfo $(VERSION)"'

all: xwininfo
xwininfo: $(OBJ)
$(OBJ): clientwin.h dsimple.h

clean:
	$(RM) $(OBJ) xwininfo xwininfo-$(VERSION).tar*

dist:
	mkdir -p xwininfo-$(VERSION)
	tar -cf xwininfo-$(VERSION).tar xwininfo-$(VERSION)
	rm -rf xwininfo-$(VERSION)

install: all
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	mkdir -p $(DESTDIR)$(MANDIR)/man1
	cp -f xwininfo $(DESTDIR)$(PREFIX)/bin
	sed -e 's/@VERSION@/$(VERSION)/g' xwininfo.1 > $(DESTDIR)$(MANDIR)/man1/xwininfo.1

uninstall:
	$(RM) $(DESTDIR)$(PREFIX)/bin/xwininfo
	$(RM) $(DESTDIR)$(MANDIR)/man1/xwininfo.1

.PHONY: all clean install uninstall
