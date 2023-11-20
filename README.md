# Introduction

**kcaldav** is a simple, safe, and minimal CalDAV server running on the
[BCHS](https://learnbchs.org) software stack.

This repository consists of bleeding-edge code between versions: to keep
up to date with the current stable release of **kcaldav**, visit the
[website](https://kristaps.bsd.lv/kcaldav).
The website also contains canonical installation, deployment, examples,
and usage documentation.

What follows describes using the bleeding-edge version of the system.

# Installation

To use the bleeding-edge version of **kcaldav** (instead of from your
system's packages or a stable version), the process it the similar as
for source releases.

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

To create the database for initial use (or to manage it), follow 
[kcaldav.passwd(1)](man/kcaldav.passwd.in.1).

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
