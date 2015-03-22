SRCS		 = main.c test-ical.c test-caldav.c caldav.c ical.c buf.c config.c
OBJS		 = caldav.o ical.o buf.o md5.o config.o
ALLOBJS		 = main.o test-ical.o test-caldav.o $(OBJS)
CALDIR		 = /tmp
CFLAGS 		+= -g -W -Wall -Wstrict-prototypes -Wno-unused-parameter -Wwrite-strings -DCALDIR=\"$(CALDIR)\"

all: kcaldav test-ical test-caldav

install: all
	mkdir -p $(PREFIX)
	install -m 0755 kcaldav $(PREFIX)/kcaldav.cgi

kcaldav: main.o $(OBJS)
	$(CC) -o $@ main.o $(OBJS) -lkcgi -lkcgixml -lexpat -lz

test-ical: test-ical.o $(OBJS)
	$(CC) -o $@ test-ical.o $(OBJS) -lexpat

test-caldav: test-caldav.o $(OBJS)
	$(CC) -o $@ test-caldav.o $(OBJS) -lexpat

$(ALLOBJS): extern.h md5.h

clean:
	rm -f $(ALLOBJS) kcaldav test-ical test-caldav
	rm -rf kcaldav.dSYM test-ical.dSYM test-caldav.dSYM
