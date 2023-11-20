# Introduction

**kcaldav** is a simple, safe, and minimal CalDAV server running on the
[BCHS](https://learnbchs.org) software stack.

To keep up to date with the current stable release of **kcaldav**, visit
https://kristaps.bsd.lv/kcaldav.  The website also contains canonical
installation, deployment, examples, and usage documentation.

# Installation

You'll need a C compiler ([gcc](https://gcc.gnu.org/) or
[clang](https://clang.llvm.org/)), [zlib](https://zlib.net) (*zlib* or
*zlib-dev* for some package managers),
[libexpat](https://libexpat.github.io/) (*libexpat-dev*),
[kcgi](https://kristaps.bsd.lv/kcgi), [sqlite3](https://sqlite3.org)
(*libsqlite3-dev*), and BSD make (*bmake* for some managers) for
building.

Begin by cloning or downloading.  Then configure with `./configure`,
compile with `make` (BSD make, so it may be `bmake` on your system),
then `make install` (or use `sudo` or `doas`, if applicable). 

If not done yet, optionally create a *Makefile.local* to override values
in [Makefile](Makefile) for your target file-system.

```sh
./configure
make install
make installcgi
```

The database hasn't updated in a long, long time, so there are no
special commands for updating it.  When updates do happen, I'll work out
a process for doing so.

A common idiom for installing on Linux is to use
[libbsd](https://libbsd.freedesktop.org/wiki/) as noted in the
[oconfigure](https://github.com/kristapsdz/oconfigure) documentation:

```
CFLAGS=$(pkg-config --cflags libbsd-overlay) \
    ./configure LDFLAGS=$(pkg-config --libs libbsd-overlay)
make
make install
```

# Deployment

To create the database for initial use (or to manage it), follow 
[kcaldav.passwd(1)](man/kcaldav.passwd.in.1).

If errors occur during operation, see
[kcaldav.conf.5](man/kcaldav.conf.5) for how to configure the system for
more debugging information.

# Regression and fuzzing

To contribute to **kcaldav**, write regression tests!

Right now, the regression suite only parses valid iCalendar files from
[ical4j](https://github.com/ical4j/ical4j), but in no way makes sure
that the information has been properly parsed.  Same with the CalDAV
tests.  To run these:

```sh
make regress
```

A major missing component is a series of regression tests that actually
makes sure that parsed content is sane.

Most of the critical infrastructure can also be run through 
[AFL](https://lcamtuf.coredump.cx/afl/).  To do this, run:

```sh
make clean
make afl CC=afl-clang
```

Or use `afl-gcc` instead---it shouldn't matter.  This installs AFL-built
binaries into the *afl* directory.  To run these on the iCalendar
library:

```sh
cd ical
sh ./dict.sh # Generates dictionary files.
afl-fuzz -i in -x dict -o out -- ../test-ical @@
```

For CalDAV:

```sh
cd caldav
afl-fuzz -i in -o out -- ../test-caldav @@
```

I'd love for more tests on the server infrastructure itself, but am not
sure how to effect this properly.

# Sources

The source code is laid out fairly consistently and easily.

The centre of the system is in *libkcaldav.a*, which contains both the
CalDAV and iCalendar functions in [libkcaldav.h](libkcaldav.h).  It's
implemented by [ical.c](ical.c) and [caldav.c](caldav.c).

The database interface is [db.h](db.h), manipulating the schema in
[kcaldav.sql](kcaldav.sql).  This is used by server and by the
built programs and manages users, collections, resources, proxies, etc.
It internally uses the [libkcaldav.h](libkcaldav.h) functions.  It is
implemented in [db.c](db.c).

The built programs are the command-line utility
[kcaldav.passwd(1)](man/kcaldav.passwd.in.1) and CGI server 
[kcaldav(8)](man/kcaldav.in.8).

The CGI server is described in [server.h](server.h) and implemented by
the majority of the source code files with entry point
[kcaldav.c](kcaldav.c]).

The command-line utility is the standalone
[kcaldav.passwd.c](kcaldav.passwd.c).

The portability glue throughout the system (e.g., `HAVE_xxx` macros,
*config.h*, etc.) is managed by
[oconfigure](https://github.com/kristapsdz/oconfigure).

# License

All sources use the ISC (like OpenBSD) license.
See the [LICENSE.md](LICENSE.md) file for details.
