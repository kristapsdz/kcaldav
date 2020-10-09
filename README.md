## Synopsis

**kcaldav** is a simple, safe, and minimal CalDAV server running on the
[BCHS](https://learnbchs.org) software stack.

This repository consists of bleeding-edge code between versions: to keep
up to date with the current stable release of **kcaldav**, visit the
[website](https://kristaps.bsd.lv/kcaldav).
The website also contains canonical installation, deployment, examples,
and usage documentation.

What follows describes using the bleeding-edge version of the system.

## Installation

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

## Tests

To contribute to **kcaldav**, write regression tests!  It uses the
iCalendar regressions from [ical4j](https://github.com/ical4j/ical4j),
but can always use more.  To run these:

```sh
make regress
```

Most of the critical infrastructure can be run through 
[AFL](https://lcamtuf.coredump.cx/afl/).  To do this, run:

```sh
make afl CC=afl-clang
```

Or use `afl-gcc` instead---it shouldn't matter.  This installs AFL-built
binaries into the *afl* directory.  To run these on the iCalendar
library:

```sh
cd ical
sh ./dict.sh
afl-fuzz -i in -x dict -o out -- ../test-ical @@
```

For CalDAV:

```sh
cd caldav
afl-fuzz -i in -o out -- ../test-caldav @@
```

## License

All sources use the ISC (like OpenBSD) license.
See the [LICENSE.md](LICENSE.md) file for details.
