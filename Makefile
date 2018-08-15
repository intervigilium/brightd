PREFIX=$(DESTDIR)/usr
BINDIR=bin/
#NO_X11=1  # (Uncomment this to disable X11 Support)

PACKAGE_VERSION=`awk '/RELEASE/ {print $$3}' brightd.c | tr -d \" | head -n1`

CFLAGS=-g -Wall -pedantic
MAN_NO_X11=1
ifndef NO_X11
MAN_NO_X11=0
A_CFLAGS=-lX11 -lXss -DX11
endif

all: brightd brightd.1

brightd: brightd.c
	gcc $(CFLAGS) -o $@ $@.c $(A_CFLAGS)

brightd.1:
	sed -re 's/^\.nr no_x11 [01]/.nr no_x11 $(MAN_NO_X11)/' brightd.1.tpl > brightd.1

install:
	install -Ds brightd $(PREFIX)/$(BINDIR)/brightd
	install -D brightd.service /lib/systemd/system/brightd.service
	install -D brightd.1 $(PREFIX)/share/man/man1/brightd.1

uninstall:
	rm $(PREFIX)/$(BINDIR)/brightd
	rm $(PREFIX)/share/man/man1/brightd.1

clean:
	rm -f brightd brightd.1

# Source tarball generation
source:
	mkdir brightd-$(PACKAGE_VERSION)/
	cp brightd.{1.tpl,c} ChangeLog gpl.txt Makefile README brightd-$(PACKAGE_VERSION)/
	tar cjf brightd-$(PACKAGE_VERSION).tar.bz2 brightd-$(PACKAGE_VERSION)/
	rm -rf brightd-$(PACKAGE_VERSION)/
