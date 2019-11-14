PRAGMA journal_mode = WAL;
PRAGMA foreign_keys=ON;

-- A resource is a ``file'' managed by the CalDAV server.
-- For us, files are always iCal files.

CREATE TABLE resource (
	-- To which collection to be we belong?
	collection INTEGER NOT NULL,
	-- The URL of the resource component, e.g., ``foo.ics''.
	url TEXT NOT NULL,
	-- The file's current etag (in the HTTP sense).
	etag TEXT NOT NULL DEFAULT('1'),
	-- The iCal data as a nil-terminated string.
	data TEXT NOT NULL,
	id INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL,
	-- Currently unused.
	flags INTEGER NOT NULL DEFAULT(0),
	unique (url,collection),
	FOREIGN KEY (collection) REFERENCES collection(id) ON DELETE CASCADE
);

-- A collection is a calendar directory.
-- Collections, in kCalDAV, only contain resources: we do not allow
-- nested collections.
-- This just keeps things simpler.

CREATE TABLE collection (
	-- Who owns this collection?
	principal INTEGER NOT NULL,
	-- The URL (path component), e.g., ``foobar'' of
	-- ``foobar/foo.ics''.
	url TEXT NOT NULL,
	-- The free-form name of the calendar.
	displayname TEXT NOT NULL DEFAULT('Calendar'),
	-- The colour (an Apple extension) for calendar showing.
	colour TEXT NOT NULL DEFAULT('#B90E28FF'),
	-- A free-form description of the calendar.
	description TEXT NOT NULL DEFAULT(''),
	-- The HTTP ctag of the collection.
	ctag INT NOT NULL DEFAULT(1),
	id INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL,
	-- Currently unused.
	flags INTEGER NOT NULL DEFAULT(0),
	unique (url,principal),
	FOREIGN KEY (principal) REFERENCES principal(id) ON DELETE CASCADE
);

-- Proxies function as a delegation mechanism: the @proxy.proxy user
-- will have access to the collections and resources of
-- @"proxy.principal".

CREATE TABLE proxy (
	-- The one whose stuff will be accessed by @"proxy.proxy".
	principal INTEGER NOT NULL,
	-- The one accessing the stuff of @"proxy.principal".
	proxy INTEGER NOT NULL,
	-- Mode bits PROXY_READ and PROXY_WRITE.
	bits INTEGER NOT NULL DEFAULT(0),
	id INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL,
	-- Currently unused.
	flags INTEGER NOT NULL DEFAULT(0),
	unique (principal,proxy),
	FOREIGN KEY (principal) REFERENCES principal(id) ON DELETE CASCADE,
	FOREIGN KEY (proxy) REFERENCES principal(id) ON DELETE CASCADE
);

-- A nonce is used by the HTTP digest authentication.
-- We limit the size of this table in the software, since it's basically
-- touchable by the open Internet.

CREATE TABLE nonce (
	-- The value of the nonce.
	nonce TEXT NOT NULL,
	-- How many times the nonce has been used.
	count INT NOT NULL DEFAULT(0),
	id INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL,
	unique (nonce)
);

-- A principal is a user.

CREATE TABLE principal (
	-- Free-form user name.
	name TEXT NOT NULL,
	-- Password hash.
	hash TEXT NOT NULL,
	-- E-mail address.
	email TEXT NOT NULL,
	-- Currently unused.
	flags INTEGER NOT NULL DEFAULT(0),
	id INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL,
	unique (email),
	unique (name)
);

-- We use this to determine first whether the database has been opened
-- and created or not; second, to see whether the person accessing the
-- database on the local system is allowed.
-- Of course, root can override everything.

CREATE TABLE database (
	-- Owner uid.
	owneruid INTEGER NOT NULL
);
