# You'll absolutely want to change this.
# It is the directory prepended to all calendar requests.
# It should not end in a trailing slash.
CALDIR		 = /tmp

# You probably don't want to change anything after this.
SRCS		 = main.c test-ical.c test-caldav.c caldav.c ical.c buf.c config.c
OBJS		 = caldav.o ical.o buf.o md5.o config.o
ALLOBJS		 = main.o test-ical.o test-caldav.o $(OBJS)
VERSIONS	 = version_0_0_4.xml
VERSION		 = 0.0.4
CFLAGS 		+= -g -W -Wall -Wstrict-prototypes -Wno-unused-parameter -Wwrite-strings
CFLAGS		+= -DCALDIR=\"$(CALDIR)\"

all: kcaldav test-ical test-caldav

www: index.html kcaldav.tgz kcaldav.tgz.sha512

install: all
	mkdir -p $(PREFIX)
	install -m 0755 kcaldav $(PREFIX)/kcaldav.cgi

installwww: www
	mkdir -p $(PREFIX)/snapshots
	install -m 0444 index.html index.css $(HTMLS) $(PREFIX)
	install -m 0444 sample.c $(PREFIX)/sample.c.txt
	install -m 0444 kcaldav.tgz kcaldav.tgz.sha512 $(PREFIX)/snapshots/
	install -m 0444 kcaldav.tgz $(PREFIX)/snapshots/kcaldav-$(VERSION).tgz
	install -m 0444 kcaldav.tgz.sha512 $(PREFIX)/snapshots/kcaldav-$(VERSION).tgz.sha512

kcaldav.tgz.sha512: kcaldav.tgz
	openssl dgst -sha512 kcaldav.tgz >$@

kcaldav.tgz:
	mkdir -p .dist/kcaldav-$(VERSION)
	cp $(SRCS) Makefile .dist/kcaldav-$(VERSION)
	(cd .dist && tar zcf ../$@ kcaldav-$(VERSION))
	rm -rf .dist

kcaldav: main.o $(OBJS)
	$(CC) -o $@ main.o $(OBJS) -lkcgi -lkcgixml -lexpat -lz

test-ical: test-ical.o $(OBJS)
	$(CC) -o $@ test-ical.o $(OBJS) -lexpat

test-caldav: test-caldav.o $(OBJS)
	$(CC) -o $@ test-caldav.o $(OBJS) -lexpat

$(ALLOBJS): extern.h md5.h

index.html: index.xml $(VERSIONS)
	sblg -t index.xml -o- $(VERSIONS) | sed "s!@VERSION@!$(VERSION)!g" >$@

clean:
	rm -f $(ALLOBJS) kcaldav test-ical test-caldav
	rm -rf kcaldav.dSYM test-ical.dSYM test-caldav.dSYM
	rm -f index.html kcaldav.tgz kcaldav.tgz.sha512
