CFLAGS 		+= -g -W -Wall -Wstrict-prototypes -Wno-unused-parameter -Wwrite-strings -DHAVE_CONFIG_H
SRCS		 = main.c test-ical.c test-caldav.c caldav.c ical.c buf.o
OBJS		 = caldav.o ical.o buf.o
ALLOBJS		 = main.o test-ical.o test-caldav.o $(OBJS)

all: kcaldav test-ical test-caldav

kcaldav: main.o $(OBJS)
	$(CC) -o $@ main.o $(OBJS) -lkcgi -lexpat -lz

test-ical: test-ical.o $(OBJS)
	$(CC) -o $@ test-ical.o $(OBJS) -lexpat

test-caldav: test-caldav.o $(OBJS)
	$(CC) -o $@ test-caldav.o $(OBJS) -lexpat

$(ALLOBJS): extern.h

clean:
	rm -f $(ALLOBJS) kcaldav test-ical test-caldav
	rm -rf kcaldav.dSYM test-ical.dSYM test-caldav.dSYM
