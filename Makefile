.PHONY: regress
.SUFFIXES: .3 .3.html .8 .8.html .1 .1.html .xml .html

include Makefile.configure

# ####################################################################
# Stuff to override in Makefile.local...
# ####################################################################

# Database directory in CGI process's file-system root.
# Should be read-write-exec for database consumers.
CALDIR		 = /caldav

# File-system directory of CALDIR.
CALPREFIX	 = /var/www/caldav

# URI (relative to the server root) of static (CSS, JS) files.
HTDOCS	 	 = /kcaldav

# File-system directory of HTDOCS.
HTDOCSPREFIX	 = /var/www/htdocs/kcaldav

# URI (relative to the server root) of the CGI script.
CGIURI		 = /cgi-bin/kcaldav

# File-system directory containing CGIURI.
CGIPREFIX	 = /var/www/cgi-bin

# Where do we get run-time kcaldav.conf(5) configuration?
# This must be writable by the server process and relative to the
# chroot(2), if applicable.
# If this file does not exist, it will be ignored; however, if the file
# has configuration errors, the process will not start.
CFGFILE	 	= /conf/kcaldav.conf

# Deprecated.  Use the configuration file instead.
#LOGFILE	 = /logs/kcaldav-system.log
# Deprecated.  Use the configuration file instead.
#CFLAGS		+= -DDEBUG=1

# Override this to be empty if you don't want static compilation.
# It's set to -static by Makefile.configure if available.
#LDADD_STATIC	 =

sinclude Makefile.local

# File-system directory where "installwww" installs.
# You probably aren't going to use that!
WWWDIR		 = /var/www/vhosts/kristaps.bsd.lv/htdocs/kcaldav

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
		   test-conf \
		   test-ical \
		   test-nonce
TESTSRCS 	 = test-caldav.c \
		   test-conf.c \
		   test-ical.c \
		   test-nonce.c \
		   test-rrule.c
TESTOBJS 	 = test-caldav.o \
		   test-conf.o \
		   test-ical.o \
		   test-nonce.o
HTMLS	 	 = archive.html \
		   index.html \
		   kcaldav.8.html \
		   kcaldav.passwd.1.html
MAN3S		 = man/caldav_free.3 \
		   man/caldav_parse.3 \
		   man/ical_free.3 \
		   man/ical_parse.3 \
		   man/ical_print.3
JSMINS		 = collection.min.js \
		   home.min.js
ALLSRCS		 = Makefile \
		   $(TESTSRCS) \
		   caldav.c \
		   collection.js \
		   collection.xml \
		   compats.c \
		   conf.c \
		   datetime.c \
		   db.c \
		   db.h \
		   delete.c \
		   dynamic.c \
		   get.c \
		   home.js \
		   home.xml \
		   ical.c \
		   kcaldav.c \
		   kcaldav.example.conf \
		   kcaldav.passwd.c \
		   kcaldav.sql \
		   libkcaldav.h \
		   md5.js \
		   options.c \
		   propfind.c \
		   property.c \
		   proppatch.c \
		   put.c \
		   script.js \
		   server.h \
		   style.css \
		   tests.c \
		   util.c
LIBOBJS		 = caldav.o \
		   ical.o
DBOBJS		 = db.o \
		   kcaldav-sql.o
BINOBJS		 = conf.o \
		   delete.o \
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
		   $(DBOBJS) \
		   compats.o \
		   kcaldav.passwd.o
VERSION		 = 0.2.1
CFLAGS		+= -DCALDIR=\"$(CALDIR)\"
CFLAGS		+= -DCALPREFIX=\"$(CALPREFIX)\"
CFLAGS		+= -DVERSION=\"$(VERSION)\"
CFLAGS		+= -DCFGFILE=\"$(CFGFILE)\"
BHTMLS		 = collection.html \
		   home.html

all: $(BINS) kcaldav.8 kcaldav.passwd.1 $(BHTMLS) $(JSMINS)

www: kcaldav.tgz kcaldav.tgz.sha512 $(HTMLS) atom.xml

afl: all
	$(INSTALL_PROGRAM) test-caldav afl/test-caldav
	$(INSTALL_PROGRAM) test-conf afl/test-conf
	$(INSTALL_PROGRAM) test-ical afl/test-ical

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
	mkdir -p $(DESTDIR)$(SHAREDIR)
	mkdir -p $(DESTDIR)$(MANDIR)/man8
	mkdir -p $(DESTDIR)$(MANDIR)/man5
	mkdir -p $(DESTDIR)$(MANDIR)/man3
	mkdir -p $(DESTDIR)$(MANDIR)/man1
	$(INSTALL_PROGRAM) kcaldav.passwd $(DESTDIR)$(BINDIR)
	$(INSTALL_MAN) kcaldav.passwd.1 $(DESTDIR)$(MANDIR)/man1
	$(INSTALL_MAN) kcaldav.8 $(DESTDIR)$(MANDIR)/man8
	$(INSTALL_MAN) ./man/kcaldav.conf.5 $(DESTDIR)$(MANDIR)/man5
	$(INSTALL_MAN) $(MAN3S) $(DESTDIR)$(MANDIR)/man3
	$(INSTALL_LIB) libkcaldav.a $(DESTDIR)$(LIBDIR)
	$(INSTALL_DATA) libkcaldav.h $(DESTDIR)$(INCLUDEDIR)
	$(INSTALL_DATA) kcaldav.example.conf $(DESTDIR)$(SHAREDIR)

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
	mkdir -p .dist/kcaldav-$(VERSION)/man
	mkdir -p .dist/kcaldav-$(VERSION)/regress
	mkdir -p .dist/kcaldav-$(VERSION)/regress/caldav
	mkdir -p .dist/kcaldav-$(VERSION)/regress/conf
	mkdir -p .dist/kcaldav-$(VERSION)/regress/ical
	$(INSTALL) -m 0644 $(ALLSRCS) .dist/kcaldav-$(VERSION)
	$(INSTALL) -m 0755 configure .dist/kcaldav-$(VERSION)
	$(INSTALL) -m 0644 $(MAN3S) .dist/kcaldav-$(VERSION)/man
	$(INSTALL) -m 0644 man/kcaldav.passwd.in.1 .dist/kcaldav-$(VERSION)/man
	$(INSTALL) -m 0644 man/kcaldav.in.8 .dist/kcaldav-$(VERSION)/man
	$(INSTALL) -m 0644 man/kcaldav.conf.5 .dist/kcaldav-$(VERSION)/man
	$(INSTALL) -m 0644 regress/caldav/*.xml .dist/kcaldav-$(VERSION)/regress/caldav
	$(INSTALL) -m 0644 regress/conf/*.conf .dist/kcaldav-$(VERSION)/regress/conf
	$(INSTALL) -m 0644 regress/ical/*.ics .dist/kcaldav-$(VERSION)/regress/ical
	( cd .dist && tar zcf ../$@ kcaldav-$(VERSION) )
	rm -rf .dist

libkcaldav.a: $(LIBOBJS)
	$(AR) -rs $@ $(LIBOBJS)

kcaldav: $(BINOBJS) $(DBOBJS) compats.o libkcaldav.a
	$(CC) -o $@ $(LDADD_STATIC) $(BINOBJS) $(DBOBJS) compats.o libkcaldav.a $(LDFLAGS) $(CGILIBS) 

kcaldav.passwd: kcaldav.passwd.o $(DBOBJS) compats.o libkcaldav.a
	$(CC) -o $@ kcaldav.passwd.o $(DBOBJS) compats.o libkcaldav.a $(LDFLAGS) $(BINLIBS)

test-conf: test-conf.o compats.o conf.o
	$(CC) -o $@ test-conf.o compats.o conf.o $(LDFLAGS) $(BINLIBS)

test-ical: test-ical.o compats.o libkcaldav.a
	$(CC) -o $@ test-ical.o compats.o libkcaldav.a $(LDFLAGS) $(BINLIBS)

test-rrule: test-rrule.o compats.o libkcaldav.a
	$(CC) -o $@ test-rrule.o compats.o libkcaldav.a $(LDFLAGS) $(BINLIBS)

test-nonce: test-nonce.o $(DBOBJS) compats.o libkcaldav.a
	$(CC) -o $@ test-nonce.o $(DBOBJS) compats.o libkcaldav.a $(LDFLAGS) $(BINLIBS)

test-caldav: test-caldav.o compats.o libkcaldav.a
	$(CC) -o $@ test-caldav.o compats.o libkcaldav.a $(LDFLAGS) $(BINLIBS)

# We can make this more refined, but this is easier.

$(ALLOBJS): config.h db.h server.h libkcaldav.h

index.html: index.xml versions.xml
	sblg -t index.xml -o $@ versions.xml

archive.html: archive.xml versions.xml
	sblg -t archive.xml -o $@ versions.xml

atom.xml: versions.xml atom-template.xml
	sblg -s date -o $@ -a versions.xml

kcaldav.8: man/kcaldav.in.8
	sed -e "s!@CALDIR@!$(CALDIR)!g" \
	    -e "s!@CGIURI@!$(CGIURI)!g" \
	    -e "s!@CALPREFIX@!$(CALPREFIX)!g" \
	    -e "s!@PREFIX@!$(PREFIX)!g" man/kcaldav.in.8 >$@

kcaldav.passwd.1: man/kcaldav.passwd.in.1
	sed -e "s!@CALPREFIX@!$(CALPREFIX)!g" \
	    -e "s!@PREFIX@!$(PREFIX)!g" man/kcaldav.passwd.in.1 >$@

# We generate a database on-the-fly.

kcaldav-sql.c: kcaldav.sql
	( echo "#include <stdarg.h>"; \
	  echo "#include <stdlib.h>"; \
	  echo "#include <stdint.h>"; \
	  echo "#include <time.h>"; \
	  echo "#include \"db.h\""; \
	  printf "const char *db_sql = \""; \
	  grep -v '^[ 	]*--' kcaldav.sql | sed -e 's!$$!\\n\\!' ; \
	  echo '";'; ) >$@

regress: test-caldav test-ical test-nonce test-conf kcaldav.sql
	@tmpdir=`mktemp -d` ; \
	 sqlite3 $$tmpdir/kcaldav.db < kcaldav.sql >/dev/null; \
	 printf "./test-nonce $${tmpdir}... " ; \
	 set -e ; \
	 ./test-nonce $$tmpdir >/dev/null 2>&1 ; \
	 if [ $$? -eq 0 ] ; \
	 then \
	 	echo "ok" ; \
	 else \
	 	echo "fail" ; \
	 fi ; \
	 set +e ; \
	 rm -rf $$tmpdir 
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
	@tmp=`mktemp` ; \
	 for f in regress/conf/*.in.conf ; \
	 do \
		set -e ; \
		printf "%s... " "$$f" ; \
		./test-conf $$f >$$tmp ; \
		if [ $$? -ne 0 ] ; \
		then \
			echo "fail (run-time)" ; \
			set +e ; \
			continue ; \
		fi ; \
		cmp -s regress/conf/`basename $$f .in.conf`.out.conf $$tmp ; \
		if [ $$? -eq 0 ] ; \
		then \
			echo "ok" ; \
		else \
			echo "fail" ; \
		fi ; \
		set +e ; \
	 done ; \
	 rm -f $$tmp
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
	mandoc -Tlint -Werror man/*.[138]
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
