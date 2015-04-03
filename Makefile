.SUFFIXES: .5 .8 .5.html .8.html

# You'll absolutely want to change this. 
# It is the directory prepended to all calendar requests.
# It should not end in a trailing slash.
CALDIR		 = /caldav
CGIPREFIX	 = /var/www/cgi-bin
PREFIX		 = /usr/local

# Add any special library directories here.
CPPFLAGS	+= -I/usr/local/include
BINLDFLAGS	 = -L/usr/local/lib
BINLIBS		 = -lkcgi -lkcgixml -lz $(LIBS) 

# For OpenBSD:
LIBS		 = -lexpat -lutil 
STATIC		 = -static

# For Mac OS X:
#LIBS		 = -lexpat 
#STATIC		 = 

# For Linux:
#LIBS		 = -lexpat -lutil -lbsd
#STATIC		 = 

# You probably don't want to change anything after this.
BINS		 = kcaldav \
		   kcaldav.passwd \
		   test-auth \
		   test-caldav \
		   test-config \
		   test-ical \
		   test-prncpl
TESTSRCS 	 = test-auth.c \
		   test-caldav.c \
		   test-config.c \
		   test-ical.c \
		   test-prncpl.c
TESTOBJS 	 = test-auth.o \
		   test-caldav.o \
		   test-config.o \
		   test-ical.o \
		   test-prncpl.o
MANS		 = kcaldav.8 \
		   kcaldav.conf.5 \
		   kcaldav.passwd.5
HTMLS	 	 = index.html \
		   kcaldav.8.html \
		   kcaldav.conf.5.html \
		   kcaldav.passwd.5.html
MANS		 = kcaldav.8 \
		   kcaldav.conf.5 \
		   kcaldav.passwd.5
CTESTSRCS	 = test-explicit_bzero.c \
		   test-fparseln.c \
		   test-memmem.c \
		   test-open-lock.c \
		   test-reallocarray.c
ALLSRCS		 = Makefile \
		   $(TESTSRCS) \
		   $(CTESTSRCS) \
		   $(MANS) \
		   buf.c \
		   caldav.c \
		   collection.c \
		   compat-explicit_bzero.c \
		   compat-fparseln.c \
		   compat-memmem.c \
		   compat-reallocarray.c \
		   config.c \
		   ctag.c \
		   delete.c \
		   err.c \
		   get.c \
		   httpauth.c \
		   ical.c \
		   kcaldav.c \
		   kcaldav.passwd.c \
		   md5.c \
		   open.c \
		   options.c \
		   quota.c \
		   principal.c \
		   propfind.c \
		   put.c \
		   resource.c \
		   util.c
OBJS		 = buf.o \
		   caldav.o \
		   compat-explicit_bzero.o \
		   compat-fparseln.o \
		   compat-memmem.o \
		   compat-reallocarray.o \
		   config.o \
		   ctag.o \
		   err.o \
		   httpauth.o \
		   ical.o \
		   md5.o \
		   open.o \
		   principal.o \
		   quota.o
BINOBJS		 = collection.o \
		   delete.o \
		   get.o \
		   kcaldav.o \
		   options.o \
		   propfind.o \
		   put.o \
		   resource.o \
		   util.o
ALLOBJS		 = $(TESTOBJS) \
		   $(BINOBJS) \
		   $(OBJS) \
		   kcaldav.passwd.o
VERSIONS	 = version_0_0_4.xml \
		   version_0_0_5.xml \
		   version_0_0_6.xml \
		   version_0_0_7.xml
VERSION		 = 0.0.7
CFLAGS 		+= -g -W -Wall -Wstrict-prototypes -Wno-unused-parameter -Wwrite-strings
CFLAGS		+= -DCALDIR=\"$(CALDIR)\"

all: $(BINS) kcaldav.8

www: kcaldav.tgz kcaldav.tgz.sha512 $(HTMLS)

afl: all
	install -m 0555 test-ical afl/test-ical

config.h: config.h.pre config.h.post configure $(CTESTSRCS)
	rm -f config.log
	CC="$(CC)" CFLAGS="$(CFLAGS)" ./configure

installcgi: all
	mkdir -p $(CGIPREFIX)
	install -m 0555 kcaldav $(CGIPREFIX)

install: installcgi
	mkdir -p $(PREFIX)/man8
	mkdir -p $(PREFIX)/man5
	install -m 0444 kcaldav.conf.5 $(PREFIX)/man5
	install -m 0444 kcaldav.passwd.5 $(PREFIX)/man5
	install -m 0444 kcaldav.8 $(PREFIX)/man8

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
	cp $(ALLSRCS) .dist/kcaldav-$(VERSION)
	(cd .dist && tar zcf ../$@ kcaldav-$(VERSION))
	rm -rf .dist

kcaldav: $(BINOBJS) $(OBJS)
	$(CC) $(BINCFLAGS) -o $@ $(STATIC) $(BINOBJS) $(OBJS) $(BINLDFLAGS) $(BINLIBS) 

kcaldav.passwd: kcaldav.passwd.o $(OBJS)
	$(CC) -o $@ kcaldav.passwd.o $(OBJS) $(LIBS)

test-ical: test-ical.o $(OBJS)
	$(CC) -o $@ test-ical.o $(OBJS) $(LIBS)

test-auth: test-auth.o $(OBJS)
	$(CC) -o $@ test-auth.o $(OBJS) $(LIBS)

test-caldav: test-caldav.o $(OBJS)
	$(CC) -o $@ test-caldav.o $(OBJS) $(LIBS)

test-config: test-config.o $(OBJS)
	$(CC) -o $@ test-config.o $(OBJS) $(LIBS)

test-prncpl: test-prncpl.o $(OBJS)
	$(CC) -o $@ test-prncpl.o $(OBJS) $(LIBS)

$(ALLOBJS): extern.h md5.h config.h

$(BINOBJS): kcaldav.h

index.html: index.xml $(VERSIONS)
	sblg -t index.xml -o- $(VERSIONS) | sed "s!@VERSION@!$(VERSION)!g" >$@

kcaldav.8: kcaldav.in.8
	sed "s!@CALDIR@!$(CALDIR)!" kcaldav.in.8 >$@

clean:
	rm -f $(ALLOBJS) $(BINS) kcaldav.8
	rm -rf *.dSYM
	rm -f $(HTMLS) kcaldav.tgz kcaldav.tgz.sha512
	rm -f test-memmem test-reallocarray test-open-lock text-explicit_bzero
	rm -f config.h config.log

.8.8.html .5.5.html:
	mandoc -Thtml $< >$@
