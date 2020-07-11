.PHONY: regress
.SUFFIXES: .3 .3.html .8 .8.html .1 .1.html .xml .html

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

# File-system directory where "installwww" installs.
# You probably aren't going to use that!
WWWDIR		 = /var/www/vhosts/kristaps.bsd.lv/htdocs/kcaldav

# This is the relative URI of the server executable.
CGIURI		 = /cgi-bin/kcaldav

# This is the file-system directory of the CGI script.
CGIPREFIX	 = /var/www/cgi-bin

# Where do we put the system log?
# This must be writable by the server process and relative to the
# chroot(2), if applicable.
LOGFILE		 = /logs/kcaldav-system.log

# Set -D DEBUG=1 to produce debugging information in LOGFILE.
# Set -D DEBUG=2 for LOTS of debugging information.
# If you want to send me a debug report, please use -DDEBUG=2.
#CFLAGS		+= -DDEBUG=1

# Override this to be empty if you don't want static compilation.
# It's set to be -static if the compiler allows it.
#LDADD_STATIC	 =

sinclude Makefile.local

# ####################################################################
# You probably don't want to change anything after this point.
# ####################################################################

PKG_STATIC	!= [ -z "$(LDADD_STATIC)" ] || echo "--static"

BINLIBS_DEP	 = sqlite3 expat
BINLIBS_DEF	 = -lsqlite3 -lexpat
BINLIBS_PKG	!= pkg-config --libs $(BINLIBS_DEP) || echo "$(BINLIBS_DEF)"
BINLIBS		 = $(BINLIBS_PKG) -lm $(LDADD_MD5) $(LDADD)

CGILIBS_DEP	 = kcgi-xml kcgi-json sqlite3 zlib expat
CGILIBS_DEF	 = -lkcgixml -lkcgijson -lkcgi -lsqlite3 -lz -lexpat
CGILIBS_PKG	!= pkg-config $(PKG_STATIC) --libs $(CGILIBS_DEP) || echo "$(CGILIBS_DEF)"
CGILIBS		 = $(CGILIBS_PKG) -lm $(LDADD_MD5) $(LDADD)

CFLAGS_PKG	!= pkg-config --cflags $(CGILIBS_DEP) $(BINLIBS_DEP) || \
			echo "$(CGILIBS_DEF) $(BINLIBS_DEF)"
CFLAGS		+= $(CFLAGS_PKG)

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
		   libkcaldav.3.html
MANS		 = kcaldav.in.8 \
		   kcaldav.passwd.in.1 \
		   libkcaldav.3
JSMINS		 = collection.min.js \
		   home.min.js
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
VERSION		 = 0.1.13
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

www: kcaldav.tgz kcaldav.tgz.sha512 $(HTMLS) atom.xml

afl: all
	$(INSTALL_PROGRAM) test-ical afl/test-ical
	$(INSTALL_PROGRAM) test-caldav afl/test-caldav

updatecgi: all
	mkdir -p $(DESTDIR)$(CGIPREFIX)
	$(INSTALL_PROGRAM) kcaldav $(DESTDIR)$(CGIPREFIX)

installcgi: all
	mkdir -p $(DESTDIR)$(CGIPREFIX)
	mkdir -p $(DESTDIR)$(HTDOCSPREFIX)
	mkdir -p $(DESTDIR)$(CALPREFIX)
	$(INSTALL_PROGRAM) kcaldav $(DESTDIR)$(CGIPREFIX)
	$(INSTALL_DATA) $(JSMINS) $(BHTMLS) style.css $(DESTDIR)$(HTDOCSPREFIX)

install: all
	mkdir -p $(DESTDIR)$(BINDIR)
	mkdir -p $(DESTDIR)$(LIBDIR)
	mkdir -p $(DESTDIR)$(INCLUDEDIR)
	mkdir -p $(DESTDIR)$(MANDIR)/man8
	mkdir -p $(DESTDIR)$(MANDIR)/man3
	mkdir -p $(DESTDIR)$(MANDIR)/man1
	$(INSTALL_PROGRAM) kcaldav.passwd $(DESTDIR)$(BINDIR)
	$(INSTALL_MAN) kcaldav.passwd.1 $(DESTDIR)$(MANDIR)/man1
	$(INSTALL_MAN) kcaldav.8 $(DESTDIR)$(MANDIR)/man8
	$(INSTALL_MAN) libkcaldav.3 $(DESTDIR)$(MANDIR)/man3
	$(INSTALL_LIB) libkcaldav.a $(DESTDIR)$(LIBDIR)
	$(INSTALL_DATA) libkcaldav.h $(DESTDIR)$(INCLUDEDIR)

installwww: www
	mkdir -p $(DESTDIR)$(WWWDIR)/snapshots
	$(INSTALL_DATA) index.css $(HTMLS) atom.xml $(DESTDIR)$(WWWDIR)
	$(INSTALL_DATA) kcaldav.tgz kcaldav.tgz.sha512 $(DESTDIR)$(WWWDIR)/snapshots/
	$(INSTALL_DATA) kcaldav.tgz $(DESTDIR)$(WWWDIR)/snapshots/kcaldav-$(VERSION).tgz
	$(INSTALL_DATA) kcaldav.tgz.sha512 $(DESTDIR)$(WWWDIR)/snapshots/kcaldav-$(VERSION).tgz.sha512

kcaldav.tgz.sha512: kcaldav.tgz
	openssl dgst -hex -sha512 kcaldav.tgz >$@

kcaldav.tgz: Makefile
	mkdir -p .dist/kcaldav-$(VERSION)
	mkdir -p .dist/kcaldav-$(VERSION)/regress
	mkdir -p .dist/kcaldav-$(VERSION)/regress/caldav
	mkdir -p .dist/kcaldav-$(VERSION)/regress/ical
	$(INSTALL) -m 0644 $(ALLSRCS) .dist/kcaldav-$(VERSION)
	$(INSTALL) -m 0755 configure .dist/kcaldav-$(VERSION)
	$(INSTALL) -m 0644 regress/ical/*.ics .dist/kcaldav-$(VERSION)/regress/ical
	$(INSTALL) -m 0644 regress/caldav/*.xml .dist/kcaldav-$(VERSION)/regress/caldav
	( cd .dist && tar zcf ../$@ kcaldav-$(VERSION) )
	rm -rf .dist

libkcaldav.a: $(LIBOBJS)
	$(AR) -rs $@ $(LIBOBJS)

kcaldav: $(BINOBJS) $(OBJS) compats.o libkcaldav.a
	$(CC) -o $@ $(LDADD_STATIC) $(BINOBJS) $(OBJS) compats.o libkcaldav.a $(LDFLAGS) $(CGILIBS) 

kcaldav.passwd: kcaldav.passwd.o $(OBJS) compats.o libkcaldav.a
	$(CC) -o $@ kcaldav.passwd.o $(OBJS) compats.o libkcaldav.a $(LDFLAGS) $(BINLIBS)

test-ical: test-ical.o compats.o libkcaldav.a
	$(CC) -o $@ test-ical.o compats.o libkcaldav.a $(LDFLAGS) $(BINLIBS)

test-rrule: test-rrule.o compats.o libkcaldav.a
	$(CC) -o $@ test-rrule.o compats.o libkcaldav.a $(LDFLAGS) $(BINLIBS)

test-nonce: test-nonce.o $(OBJS) compats.o libkcaldav.a
	$(CC) -o $@ test-nonce.o $(OBJS) compats.o libkcaldav.a $(LDFLAGS) $(BINLIBS)

test-caldav: test-caldav.o compats.o libkcaldav.a
	$(CC) -o $@ test-caldav.o compats.o libkcaldav.a $(LDFLAGS) $(BINLIBS)

$(ALLOBJS): extern.h libkcaldav.h config.h

$(BINOBJS): kcaldav.h

index.html: index.xml versions.xml
	sblg -t index.xml -o $@ versions.xml

archive.html: archive.xml versions.xml
	sblg -t archive.xml -o $@ versions.xml

atom.xml: versions.xml atom-template.xml
	sblg -s date -o $@ -a versions.xml

kcaldav.8: kcaldav.in.8
	sed -e "s!@CALDIR@!$(CALDIR)!g" \
	    -e "s!@CGIURI@!$(CGIURI)!g" \
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

regress: test-caldav test-ical
	@for f in regress/caldav/*.xml ; \
	do \
		set -e ; \
		printf "%s... " "$$f" ; \
		./test-caldav $$f >/dev/null 2>&1 ; \
		if [ $$? -eq 0 ] ; \
		then \
			echo "ok" ; \
		else \
			echo "fail" ; \
		fi ; \
		set +e ; \
	done
	@for f in regress/ical/*.ics ; \
	do \
		set -e ; \
		printf "%s... " "$$f" ; \
		./test-ical $$f >/dev/null 2>&1 ; \
		if [ $$? -eq 0 ] ; \
		then \
			echo "ok" ; \
		else \
			echo "fail" ; \
		fi ; \
		set +e ; \
	done

distcheck: kcaldav.tgz.sha512 kcaldav.tgz
	mandoc -Tlint -Werror *.in.[13]
	newest=`grep "<h1>" versions.xml | tail -1 | sed 's![ 	]*!!g'` ; \
	       [ "$$newest" = "<h1>$(VERSION)</h1>" ] || \
		{ echo "Version $(VERSION) not newest in versions.xml" 1>&2 ; exit 1 ; }
	[ "`openssl dgst -sha512 -hex kcaldav.tgz`" = "`cat kcaldav.tgz.sha512`" ] || \
 		{ echo "Checksum does not match." 1>&2 ; exit 1 ; }
	rm -rf .distcheck
	mkdir -p .distcheck
	( cd .distcheck && tar -zvxpf ../kcaldav.tgz )
	( cd .distcheck/kcaldav-$(VERSION) && ./configure PREFIX=prefix )
	( cd .distcheck/kcaldav-$(VERSION) && $(MAKE) )
	( cd .distcheck/kcaldav-$(VERSION) && $(MAKE) regress )
	( cd .distcheck/kcaldav-$(VERSION) && $(MAKE) install )
	rm -rf .distcheck

clean:
	rm -f $(ALLOBJS) $(BINS) kcaldav.8 kcaldav.passwd.1 libkcaldav.a kcaldav-sql.c
	rm -rf *.dSYM
	rm -f $(HTMLS) atom.xml $(BHTMLS) $(JSMINS) kcaldav.tgz kcaldav.tgz.sha512

distclean: clean
	rm -f config.h config.log Makefile.configure

.8.8.html .3.3.html .1.1.html:
	mandoc -Ostyle=https://bsd.lv/css/mandoc.css -Thtml $< >$@

.xml.html:
	sed -e "s!@HTDOCS@!$(HTDOCS)!g" \
	    -e "s!@VERSION@!$(VERSION)!g" \
	    -e "s!@CGIURI@!$(CGIURI)!g" $< >$@

home.min.js: md5.js script.js home.js
	( cat md5.js ; \
	  cat script.js ; \
  	  cat home.js ; ) | \
		sed -e "s!@HTDOCS@!$(HTDOCS)!g" \
		    -e "s!@VERSION@!$(VERSION)!g" \
		    -e "s!@CGIURI@!$(CGIURI)!g" >$@

collection.min.js: script.js collection.js
	( cat script.js ; \
  	  cat collection.js ; ) | \
		sed -e "s!@HTDOCS@!$(HTDOCS)!g" \
		    -e "s!@VERSION@!$(VERSION)!g" \
		    -e "s!@CGIURI@!$(CGIURI)!g" >$@
