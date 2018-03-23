.SUFFIXES: .3 .3.html .5 .8 .5.html .8.html .1 .1.html .xml .html .min.js .js

include Makefile.configure

# You WILL need to edit this for your needs.
# Put your overrides in Makefile.local.
# I have added defaults for a straightforward OpenBSD installation.
# For compile-time variables like PREFIX, CFLAGS, etc., see the
# configure script.

# ####################################################################
# Stuff to override in Makefile.local...
# ####################################################################

# This is the directory prepended to all calendar requests.
# It is relative to the CGI process's file-system root.
# It contains the database.
# You want it to be mode-rwx for the SQLite files.
CALDIR		 = /caldav

# This is the file-system directory of CALDIR.
CALPREFIX	 = /var/www/caldav

# This is the URI of the static (CSS, JS) files.
HTDOCS	 	 = /kcaldav
#HTDOCS		 = /

# This is the file-system directory of HTDOCS.
# I'm pretty sure you want to override this...
HTDOCSPREFIX	 = /var/www/vhosts/www.bsd.lv/htdocs/kcaldav
#HTDOCSPREFIX	 = /var/www/htdocs

# This is the relative URI of the server executable.
CGIURI		 = /cgi-bin/kcaldav.cgi

# This is the file-system directory of the CGI script.
CGIPREFIX	 = /var/www/cgi-bin

# Where do we put the system log?
# This must be writable by the server process and relative to the
# chroot(2), if applicable.
LOGFILE	= /logs/kcaldav-system.log

# Set -D DEBUG=1 to produce debugging information in LOGFILE.
# Set -D DEBUG=2 for LOTS of debugging information.
# If you want to send me a debug report, please use -DDEBUG=2.
#CFLAGS		+= -DDEBUG=1

# Lastly, we set whether we're statically compiling.
STATIC		 = -static

sinclude Makefile.local

# ####################################################################
# You probably don't want to change anything after this point.
# ####################################################################

LIBS		 = -lexpat -lsqlite3 -lm -lpthread $(LDADD)
BINLIBS		 = -lkcgi -lkcgixml -lkcgijson -lz $(LIBS) 
BINS		 = kcaldav \
		   kcaldav.passwd \
		   test-caldav \
		   test-ical \
		   test-nonce \
		   test-rrule
TESTSRCS 	 = test-caldav.c \
		   test-ical.c \
		   test-nonce.c \
		   test-rrule.c
TESTOBJS 	 = test-caldav.o \
		   test-ical.o \
		   test-nonce.o \
		   test-rrule.o
HTMLS	 	 = archive.html \
		   index.html \
		   kcaldav.8.html \
		   kcaldav.passwd.1.html \
		   libkcaldav.3.html \
		   schema.html
MANS		 = kcaldav.in.8 \
		   kcaldav.passwd.in.1 \
		   libkcaldav.3
JSMINS		 = collection.min.js \
		   home.min.js \
		   md5.min.js \
		   script.min.js
ALLSRCS		 = Makefile \
		   $(TESTSRCS) \
		   $(MANS) \
		   buf.c \
		   caldav.c \
		   collection.js \
		   collection.xml \
		   compats.c \
		   datetime.c \
		   db.c \
		   delete.c \
		   dynamic.c \
		   err.c \
		   extern.h \
		   get.c \
		   home.js \
		   home.xml \
		   ical.c \
		   kcaldav.c \
		   kcaldav.h \
		   kcaldav.passwd.c \
		   kcaldav.sql \
		   libkcaldav.h \
		   md5.js \
		   options.c \
		   principal.c \
		   propfind.c \
		   property.c \
		   proppatch.c \
		   put.c \
		   resource.c \
		   script.js \
		   style.css \
		   tests.c \
		   util.c
LIBOBJS		 = buf.o \
		   caldav.o \
		   compats.o \
		   datetime.o \
		   err.o \
		   ical.o
OBJS		 = db.o \
		   kcaldav-sql.o \
		   principal.o \
		   resource.o
BINOBJS		 = delete.o \
		   dynamic.o \
		   get.o \
		   kcaldav.o \
		   options.o \
		   propfind.o \
		   property.o \
		   proppatch.o \
		   put.o \
		   util.o
ALLOBJS		 = $(TESTOBJS) \
		   $(LIBOBJS) \
		   $(BINOBJS) \
		   $(OBJS) \
		   kcaldav.passwd.o
VERSION		 = 0.1.4
CFLAGS		+= -DCALDIR=\"$(CALDIR)\"
CFLAGS		+= -DHTDOCS=\"$(HTDOCS)\"
CFLAGS		+= -DVERSION=\"$(VERSION)\"
CFLAGS		+= -DLOGFILE=\"$(LOGFILE)\"
BHTMLS		 = collection.html \
		   home.html
DOTFLAGS	 = -h "BGCOLOR=\"red\"" \
		   -t "CELLBORDER=\"0\"" \
		   -t "CELLSPACING=\"0\""

all: $(BINS) kcaldav.8 kcaldav.passwd.1 $(BHTMLS) $(JSMINS)

www: kcaldav.tgz kcaldav.tgz.sha512 $(HTMLS)

afl: all
	install -m 0555 test-ical afl/test-ical
	install -m 0555 test-caldav afl/test-caldav

updatecgi: all
	mkdir -p $(CGIPREFIX)
	install -m 0555 kcaldav $(CGIPREFIX)
	install -m 0555 kcaldav $(CGIPREFIX)/kcaldav.cgi

installcgi: all
	mkdir -p $(CGIPREFIX)
	mkdir -p $(HTDOCSPREFIX)
	mkdir -p $(CALPREFIX)
	install -m 0555 kcaldav $(CGIPREFIX)
	install -m 0555 kcaldav $(CGIPREFIX)/kcaldav.cgi
	install -m 0444 $(JSMINS) $(BHTMLS) style.css $(HTDOCSPREFIX)

install: all
	mkdir -p $(PREFIX)/bin
	mkdir -p $(PREFIX)/lib
	mkdir -p $(PREFIX)/include
	mkdir -p $(PREFIX)/man/man8
	mkdir -p $(PREFIX)/man/man3
	mkdir -p $(PREFIX)/man/man1
	install -m 0555 kcaldav.passwd $(PREFIX)/bin
	install -m 0444 kcaldav.passwd.1 $(PREFIX)/man/man1
	install -m 0444 kcaldav.8 $(PREFIX)/man/man8
	install -m 0444 libkcaldav.3 $(PREFIX)/man/man3
	install -m 0444 libkcaldav.a $(PREFIX)/lib
	install -m 0444 libkcaldav.h $(PREFIX)/include

installwww: www
	mkdir -p $(PREFIX)/snapshots
	install -m 0444 index.css mandoc.css schema.css schema.png imageMapResizer.min.js $(HTMLS) $(PREFIX)
	install -m 0444 kcaldav.tgz kcaldav.tgz.sha512 $(PREFIX)/snapshots/
	install -m 0444 kcaldav.tgz $(PREFIX)/snapshots/kcaldav-$(VERSION).tgz
	install -m 0444 kcaldav.tgz.sha512 $(PREFIX)/snapshots/kcaldav-$(VERSION).tgz.sha512

kcaldav.tgz.sha512: kcaldav.tgz
	openssl dgst -sha512 kcaldav.tgz >$@

kcaldav.tgz:
	mkdir -p .dist/kcaldav-$(VERSION)
	install -m 0644 $(ALLSRCS) .dist/kcaldav-$(VERSION)
	install -m 0755 configure .dist/kcaldav-$(VERSION)
	(cd .dist && tar zcf ../$@ kcaldav-$(VERSION))
	rm -rf .dist

libkcaldav.a: $(LIBOBJS)
	$(AR) -rs $@ $(LIBOBJS)

kcaldav: $(BINOBJS) $(OBJS) libkcaldav.a
	$(CC) $(BINCFLAGS) -o $@ $(STATIC) $(BINOBJS) $(OBJS) libkcaldav.a $(LDFLAGS) $(BINLIBS) 

kcaldav.passwd: kcaldav.passwd.o $(OBJS) libkcaldav.a
	$(CC) -o $@ kcaldav.passwd.o $(OBJS) libkcaldav.a $(LDFLAGS) $(LIBS)

test-ical: test-ical.o libkcaldav.a
	$(CC) -o $@ test-ical.o libkcaldav.a $(LDFLAGS) $(LIBS)

test-rrule: test-rrule.o libkcaldav.a
	$(CC) -o $@ test-rrule.o libkcaldav.a $(LDFLAGS) $(LIBS)

test-nonce: test-nonce.o $(OBJS) libkcaldav.a
	$(CC) -o $@ test-nonce.o $(OBJS) libkcaldav.a $(LDFLAGS) $(LIBS)

test-caldav: test-caldav.o libkcaldav.a
	$(CC) -o $@ test-caldav.o libkcaldav.a $(LDFLAGS) $(LIBS)

$(ALLOBJS): extern.h libkcaldav.h config.h

$(BINOBJS): kcaldav.h

index.html: index.xml versions.xml
	sblg -t index.xml -o- versions.xml | sed "s!@VERSION@!$(VERSION)!g" >$@

archive.html: archive.xml versions.xml
	sblg -t archive.xml -o- versions.xml | sed "s!@VERSION@!$(VERSION)!g" >$@

schema.html: schema.xml schema.png kcaldav.sql
	( sed -n '1,/@SCHEMA@/p' schema.xml ; \
	  sqlite2html kcaldav.sql ; \
	  sqlite2dot $(DOTFLAGS) kcaldav.sql | dot -Tcmapx ; \
	  sed -n '/@SCHEMA@/,$$p' schema.xml ; ) >$@

schema.png: kcaldav.sql
	sqlite2dot $(DOTFLAGS) kcaldav.sql | dot -Tpng >$@

kcaldav.8: kcaldav.in.8
	sed -e "s!@CALDIR@!$(CALDIR)!g" \
	    -e "s!@PREFIX@!$(PREFIX)!g" kcaldav.in.8 >$@

kcaldav.passwd.1: kcaldav.passwd.in.1
	sed -e "s!@CALDIR@!$(CALDIR)!g" \
	    -e "s!@PREFIX@!$(PREFIX)!g" kcaldav.passwd.in.1 >$@

# We generate a database on-the-fly.
kcaldav-sql.c: kcaldav.sql
	( echo "#include <stdlib.h>"; \
	  echo "#include <stdint.h>"; \
	  echo "#include <time.h>"; \
	  echo "#include \"extern.h\""; \
	  printf "const char *db_sql = \""; \
	  grep -v '^[ 	]*--' kcaldav.sql | sed -e 's!$$!\\n\\!' ; \
	  echo '";'; ) >$@

clean:
	rm -f $(ALLOBJS) $(BINS) kcaldav.8 kcaldav.passwd.1 libkcaldav.a kcaldav-sql.c
	rm -rf *.dSYM
	rm -f $(HTMLS) $(BHTMLS) $(JSMINS) kcaldav.tgz kcaldav.tgz.sha512
	rm -f test-memmem test-reallocarray test-explicit_bzero
	rm -f schema.png 

distclean: clean
	rm -f config.h config.log Makefile.configure

.8.8.html .5.5.html .3.3.html .1.1.html:
	mandoc -Thtml -Ostyle=mandoc.css $< >$@

.xml.html:
	sed -e "s!@HTDOCS@!$(HTDOCS)!g" \
	    -e "s!@VERSION@!$(VERSION)!g" \
	    -e "s!@CGIURI@!$(CGIURI)!g" $< >$@

.js.min.js:
	sed -e "s!@HTDOCS@!$(HTDOCS)!g" \
	    -e "s!@VERSION@!$(VERSION)!g" \
	    -e "s!@CGIURI@!$(CGIURI)!g" $< >$@
