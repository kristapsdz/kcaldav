/*
 * Copyright (c) Kristaps Dzonsons <kristaps@bsd.lv>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#include "config.h"

#include <sys/statvfs.h>

#include <assert.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sqlite3.h>

#include "libkcaldav.h"
#include "db.h"

/*
 * How many nonces do we allow in the database.
 * Too few nonces and a clever attacker can SYN-flood the nonce
 * database, and too many and it'll just be ponderous.
 */
#define NONCEMAX 1000

/*
 * Length of nonce string w/o NUL terminator.
 */
#define NONCESZ	 16

enum sqlstmt {
	SQL_COL_GET,
	SQL_COL_GET_ID,
	SQL_COL_INSERT,
	SQL_COL_ITER,
	SQL_COL_REMOVE,
	SQL_COL_UPDATE,
	SQL_COL_UPDATE_CTAG,
	SQL_NONCE_COUNT,
	SQL_NONCE_GET_COUNT,
	SQL_NONCE_INSERT,
	SQL_NONCE_REMOVE,
	SQL_NONCE_REMOVE_MULTI,
	SQL_NONCE_UPDATE,
	SQL_OWNER_GET,
	SQL_OWNER_INSERT,
	SQL_PRNCPL_GET,
	SQL_PRNCPL_GET_ID,
	SQL_PRNCPL_INSERT,
	SQL_PRNCPL_UPDATE,
	SQL_PROXY_INSERT,
	SQL_PROXY_ITER,
	SQL_PROXY_ITER_PRNCPL,
	SQL_PROXY_REMOVE,
	SQL_PROXY_UPDATE,
	SQL_RES_GET,
	SQL_RES_GET_ETAG,
	SQL_RES_INSERT,
	SQL_RES_ITER,
	SQL_RES_REMOVE,
	SQL_RES_REMOVE_ETAG,
	SQL_RES_UPDATE,
	SQL__MAX
};

static const char *sqls[SQL__MAX] = {
	/* SQL_COL_GET */
	"SELECT url,displayname,colour,description,ctag,id "
		"FROM collection WHERE principal=? AND url=?",
	/* SQL_COL_GET_ID */
	"SELECT url,displayname,colour,description,ctag,id "
		"FROM collection WHERE principal=? AND id=?",
	/* SQL_COL_INSERT */
	"INSERT INTO collection (principal, url) VALUES (?,?)",
	/* SQL_COL_ITER */
	"SELECT url,displayname,colour,description,ctag,id "
		"FROM collection WHERE principal=?",
	/* SQL_COL_REMOVE */
	"DELETE FROM collection WHERE id=?",
	/* SQL_COL_UPDATE */
	"UPDATE collection SET displayname=?,colour=?,description=? "
		"WHERE id=?",
	/* SQL_COL_UPDATE_CTAG */
	"UPDATE collection SET ctag=ctag+1 WHERE id=?",
	/* SQL_NONCE_COUNT */
	"SELECT count(*) FROM nonce",
	/* SQL_NONCE_GET_COUNT */
	"SELECT count FROM nonce WHERE nonce=?",
	/* SQL_NONCE_INSERT */
	"INSERT INTO nonce (nonce) VALUES (?)",
	/* SQL_NONCE_REMOVE */
	"DELETE FROM nonce WHERE nonce=?",
	/* SQL_NONCE_REMOVE_MULTI */
	"DELETE FROM nonce WHERE id IN (SELECT id FROM nonce LIMIT 20)",
	/* SQL_NONCE_UDPATE */
	"UPDATE nonce SET count=? WHERE nonce=?",
	/* SQL_OWNER_GET */
	"SELECT owneruid FROM database",
	/* SQL_OWNER_INSERT */
	"INSERT INTO database (owneruid) VALUES (?)",
	/* SQL_PRNCPL_GET */
	"SELECT hash,id,email FROM principal WHERE name=?",
	/* SQL_PRNCPL_GET_ID */
	"SELECT id FROM principal WHERE email=?",
	/* SQL_PRNCPL_INSERT */
	"INSERT INTO principal (name,hash,email) VALUES (?,?,?)",
	/* SQL_PRNCPL_UPDATE */
	"UPDATE principal SET hash=?,email=? WHERE id=?",
	/* SQL_PROXY_INSERT */
	"INSERT INTO proxy (principal,proxy,bits) VALUES (?, ?, ?)",
	/* SQL_PROXY_ITER */
	"SELECT email,name,bits,principal,proxy.id FROM proxy "
		"INNER JOIN principal ON principal.id=principal "
		"WHERE proxy=?",
	/* SQL_PROXY_ITER_PRNCPL */
	"SELECT email,name,bits,proxy,proxy.id FROM proxy "
		"INNER JOIN principal ON principal.id=proxy "
		"WHERE principal=?",
	/* SQL_PROXY_REMOVE */
	"DELETE FROM proxy WHERE principal=? AND proxy=?",
	/* SQL_PROXY_UPDATE */
	"UPDATE proxy SET bits=? WHERE principal=? AND proxy=?",
	/* SQL_RES_GET */
	"SELECT data,etag,url,id,collection FROM resource "
		"WHERE collection=? AND url=?",
	/* SQL_RES_GET_ETAG */
	"SELECT id FROM resource WHERE url=? AND collection=? "
		"AND etag=?",
	/* SQL_RES_INSERT */
	"INSERT INTO resource (data,url,collection,etag) "
		"VALUES (?,?,?,?)",
	/* SQL_RES_ITER */
	"SELECT data,etag,url,id,collection FROM resource "
		"WHERE collection=?",
	/* SQL_RES_REMOVE */
	"DELETE FROM resource WHERE url=? AND collection=?",
	/* SQL_RES_REMOVE_ETAG */
	"DELETE FROM resource WHERE url=? AND collection=? "
		"AND etag=?",
	/* SQL_RES_UPDATE */
	"UPDATE resource SET data=?,etag=? WHERE id=?",
};

/* Wrappers for debugging functions. */

static void	 	 kdbg(const char *, ...)
			 __attribute__((format(printf, 1, 2)));
static void 	 	 kinfo(const char *, ...)
			 __attribute__((format(printf, 1, 2)));
static void	 	 kerr(const char *, ...)
			 __attribute__((format(printf, 1, 2)));
static void	 	 kerrx(const char *, ...)
			 __attribute__((format(printf, 1, 2)));

/* The database (or NULL) and it's location. */

static sqlite3		*db;
static char		 dbname[PATH_MAX];

/* Identifier and private data to provide to db_msg functions. */

static const char	*msg_ident;
static void		*msg_arg;

/* 
 * Message callbacks: debugging (lowest priority), info (informational,
 * low priority), and errors.
 */

static db_msg		 msg_dbg;
static db_msg		 msg_info;
static db_msg		 msg_err;
static db_msg		 msg_errx;

void
db_set_msg_arg(void *arg)
{

	msg_arg = arg;
}

void
db_set_msg_ident(const char *ident)
{

	msg_ident = ident;
}

void
db_set_msg_dbg(db_msg msg)
{

	msg_dbg = msg;
}

void
db_set_msg_errx(db_msg msg)
{

	msg_errx = msg;
}

void
db_set_msg_err(db_msg msg)
{

	msg_err = msg;
}

void
db_set_msg_info(db_msg msg)
{

	msg_info = msg;
}

/*
 * Log information.
 * This means an operation that changed the database.
 */
static void
kinfo(const char *fmt, ...)
{
	va_list	 ap;

	if (msg_info == NULL)
		return;

	va_start(ap, fmt);
	msg_info(msg_arg, msg_ident, fmt, ap);
	va_end(ap);
}

/*
 * Debugging log.
 * This changed the database---but in a minor way.
 * The best example is nonce updates, which aren't important.
 */
static void
kdbg(const char *fmt, ...)
{
	va_list	 ap;

	if (msg_dbg == NULL)
		return;

	va_start(ap, fmt);
	msg_dbg(msg_arg, msg_ident, fmt, ap);
	va_end(ap);
}

/*
 * Error callback.
 * This will probably trigger application exit.
 */
static void
kerr(const char *fmt, ...)
{
	va_list	 ap;
	
	if (msg_errx == NULL)
		return;

	va_start(ap, fmt);
	msg_err(msg_arg, msg_ident, fmt, ap);
	va_end(ap);
}

/*
 * Error callback (no errno).
 * This will probably trigger application exit.
 */
static void
kerrx(const char *fmt, ...)
{
	va_list	 ap;
	
	if (msg_errx == NULL)
		return;

	va_start(ap, fmt);
	msg_errx(msg_arg, msg_ident, fmt, ap);
	va_end(ap);
}

/*
 * Close the database and reset the database filename.
 * This should be called on exit time with atexit(3).
 */
static void
db_close(void)
{

	if (sqlite3_close(db) != SQLITE_OK)
		kerrx("%s", sqlite3_errmsg(db));

	db = NULL;
	explicit_bzero(dbname, PATH_MAX);
}

/*
 * Finalise and nullify a statement.
 * Use this instead of sqlite3_finalize() so we catch any re-uses of the
 * statement object.
 */
static void
db_finalise(sqlite3_stmt **stmt)
{

	if (*stmt == NULL)
		return;
	sqlite3_finalize(*stmt);
	*stmt = NULL;
}

/*
 * Provided mainly for Linux that doesn't have arc4random.
 * Returns a (non-cryptographic) random number.
 */
static uint32_t
get_random(void)
{
#if HAVE_ARC4RANDOM
	return arc4random();
#else
	return random();
#endif
}

/*
 * Provided mainly for Linux that doesn't have arc4random.
 * Returns a (non-cryptographic) random number between [0, sz).
 */
static uint32_t
get_random_uniform(size_t sz)
{
#if HAVE_ARC4RANDOM
	return arc4random_uniform(sz);
#else
	return random() % sz;
#endif
}

/*
 * Sleep for a random period in a sequence of sleeps.
 * This is influenced by the PRNG so that we don't have all consumers
 * sleeping and waking at the same time.
 */
static void
db_sleep(size_t attempt)
{

	if (attempt < 10)
		usleep(get_random_uniform(100000));
	else
		usleep(get_random_uniform(400000));
}

/*
 * Interior function managing step errors.
 */
static int
db_step_inner(sqlite3_stmt *stmt, int constrained)
{
	int	 rc;
	size_t	 attempt = 0;
again:
	assert(stmt != NULL);
	assert(db != NULL);

	rc = sqlite3_step(stmt);
	switch (rc) {
	case SQLITE_BUSY:
		db_sleep(attempt++);
		goto again;
	case SQLITE_LOCKED:
		kdbg("sqlite3_step: %s (re-trying)", 
			sqlite3_errmsg(db));
		db_sleep(attempt++);
		goto again;
	case SQLITE_PROTOCOL:
		kdbg("sqlite3_step: %s (re-trying)", 
			sqlite3_errmsg(db));
		db_sleep(attempt++);
		goto again;
	case SQLITE_DONE:
		/* FALLTHROUGH */
	case SQLITE_ROW:
		return rc;
	case SQLITE_CONSTRAINT:
		if (constrained)
			return rc;
		break;
	default:
		break;
	}

	kerrx("sqlite3_step: %s", sqlite3_errmsg(db));
	return rc;
}

/*
 * Step through any results where the return code can also be
 * SQL_CONSTRAINT, i.e., we've violated our constraints.
 * If we were using db_step() on that, it'd return failure.
 * Returns the sqlite3 error code.
 */
static int
db_step_constrained(sqlite3_stmt *stmt)
{

	return db_step_inner(stmt, 1);
}

/*
 * Step through any results on a prepared statement.
 * We report errors on anything other than SQLITE_ROW and SQLITE_DONE.
 * Returns the sqlite3 error code.
 */
static int
db_step(sqlite3_stmt *stmt)
{

	return db_step_inner(stmt, 0);
}

/*
 * Bind a 64-bit integer "v" to the statement.
 * Return zero on failure, non-zero on success.
 */
static int
db_bindint(sqlite3_stmt *stmt, size_t pos, int64_t v)
{

	assert(pos > 0);
	if (sqlite3_bind_int64(stmt, pos, v) == SQLITE_OK)
		return 1;
	kerrx("sqlite3_bind_int64: %s", sqlite3_errmsg(db));
	return 0;
}

/*
 * Bind a NUL-terminated string "name" to the statement.
 * Return zero on failure, non-zero on success.
 */
static int
db_bindtext(sqlite3_stmt *stmt, size_t pos, const char *name)
{

	assert(pos > 0);
	if (sqlite3_bind_text(stmt, 
	    pos, name, -1, SQLITE_STATIC) == SQLITE_OK)
		return 1;
	kerrx("sqlite3_bind_text: %s", sqlite3_errmsg(db));
	return 0;
}

/*
 * Execute a non-parameterised SQL statement.
 * Returns the sqlite3 error code, reporting the error if it doesn't
 * equal SQLITE_OK.
 */
static int
db_exec(const char *sql)
{
	size_t	attempt = 0;
	int	rc;
again:
	assert(NULL != db);

	rc = sqlite3_exec(db, sql, NULL, NULL, NULL);
	switch (rc) {
	case SQLITE_BUSY:
		db_sleep(attempt++);
		goto again;
	case SQLITE_LOCKED:
		kdbg("sqlite3_exec: %s (re-trying)", 
			sqlite3_errmsg(db));
		db_sleep(attempt++);
		goto again;
	case SQLITE_PROTOCOL:
		kdbg("sqlite3_exec: %s (re-trying)", 
			sqlite3_errmsg(db));
		db_sleep(attempt++);
		goto again;
	case SQLITE_OK:
		return rc;
	default:
		break;
	}

	kerrx("sqlite3_exec: %s", sqlite3_errmsg(db));
	return rc;
}

static int
db_trans_open(void)
{

	return(SQLITE_OK == db_exec("BEGIN IMMEDIATE TRANSACTION"));
}

static int
db_trans_rollback(void)
{

	return(SQLITE_OK == db_exec("ROLLBACK TRANSACTION"));
}

static int
db_trans_commit(void)
{

	return(SQLITE_OK == db_exec("COMMIT TRANSACTION"));
}

/*
 * Prepare an SQL statement.
 * If errors occur, any fledgling statement is destroyed, so this always
 * returns non-NULL on success.
 */
static sqlite3_stmt *
db_prepare(const char *sql)
{
	sqlite3_stmt	*stmt;
	size_t		 attempt = 0;
	int		 rc;
again:
	assert(NULL != db);

	rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
	switch (rc) {
	case SQLITE_BUSY:
		db_sleep(attempt++);
		goto again;
	case SQLITE_LOCKED:
		kdbg("sqlite3_prepare_v2: %s (re-trying)", 
			sqlite3_errmsg(db));
		db_sleep(attempt++);
		goto again;
	case SQLITE_PROTOCOL:
		kdbg("sqlite3_prepare_v2: %s (re-trying)", 
			sqlite3_errmsg(db));
		db_sleep(attempt++);
		goto again;
	case SQLITE_OK:
		return stmt;
	default:
		break;
	}

	kerrx("sqlite3_stmt: %s", sqlite3_errmsg(db));
	sqlite3_finalize(stmt);
	return NULL;
}

/*
 * Initialise the database, creating it if "create" is specified.
 * Note that "dir" refers to the director of creation, not the database
 * file itself.
 * Returns zero on failure, non-zero on success.
 */
int
db_init(const char *dir, int create)
{
	size_t	 attempt = 0, sz;
	int	 rc;

	/* 
	 * Register exit hook for the destruction of the database. 
	 * This allows us to ignore closing the database properly.
	 */

	if (atexit(db_close) == -1) {
		kerr("atexit");
		return 0;
	}

	/* Format the name of the database. */

	sz = strlcpy(dbname, dir, sizeof(dbname));
	if (sz > sizeof(dbname)) {
		kerrx("%s: too long", dir);
		return 0;
	} else if ('/' == dbname[sz - 1])
		dbname[sz - 1] = '\0';

	sz = strlcat(dbname, "/kcaldav.db", sizeof(dbname));
	if (sz >= sizeof(dbname)) {
		kerrx("%s: too long", dir);
		return 0;
	}

again:
	rc = sqlite3_open_v2(dbname, &db, 
		SQLITE_OPEN_READWRITE | 
		(create ? SQLITE_OPEN_CREATE : 0),
		NULL);
	switch (rc) {
	case SQLITE_BUSY:
		db_sleep(attempt++);
		goto again;
	case SQLITE_LOCKED:
		kdbg("sqlite3_open_v2: %s (re-trying)", 
			sqlite3_errmsg(db));
		db_sleep(attempt++);
		goto again;
	case SQLITE_PROTOCOL:
		kdbg("sqlite3_open_v2: %s (re-trying)", 
			sqlite3_errmsg(db));
		db_sleep(attempt++);
		goto again;
	case SQLITE_OK:
		sqlite3_busy_timeout(db, 1000);
		return SQLITE_OK == 
		       db_exec("PRAGMA foreign_keys = ON;");
	default:
		break;
	} 

	kerrx("sqlite3_open_v2: %s", sqlite3_errmsg(db));
	return 0;
}

/*
 * Update the tag ("ctag") for the collection.
 * Return zero on failure, non-zero on success.
 */
static int
db_collection_update_ctag(int64_t id)
{
	sqlite3_stmt	*stmt;

	if ((stmt = db_prepare(sqls[SQL_COL_UPDATE_CTAG])) == NULL)
		goto err;
	else if (!db_bindint(stmt, 1, id))
		goto err;
	else if (db_step(stmt) != SQLITE_DONE)
		goto err;

	kdbg("updated ctag: collection-%" PRId64, id);
	db_finalise(&stmt);
	return 1;
err:
	db_finalise(&stmt);
	return 0;
}

/*
 * Delete the nonce row.
 * Return zero on failure, non-zero on success.
 */
int
db_nonce_delete(const char *nonce, const struct prncpl *p)
{
	sqlite3_stmt	*stmt;

	if ((stmt = db_prepare(sqls[SQL_NONCE_REMOVE])) == NULL)
		goto err;
	else if (!db_bindtext(stmt, 1, nonce))
		goto err;
	else if (db_step(stmt) != SQLITE_DONE)
		goto err;

	db_finalise(&stmt);
	kdbg("deleted nonce: %s", nonce);
	return 1;
err:
	db_finalise(&stmt);
	return 0;
}

/*
 * See if the nonce count is valid.
 * Return the corresponding error code.
 */
enum nonceerr
db_nonce_validate(const char *nonce, int64_t count)
{
	sqlite3_stmt	*stmt;
	int64_t		 cmp;

	if ((stmt = db_prepare(sqls[SQL_NONCE_GET_COUNT])) == NULL)
		goto err;
	else if (!db_bindtext(stmt, 1, nonce))
		goto err;

	switch (db_step(stmt)) {
	case SQLITE_ROW:
		cmp = sqlite3_column_int64(stmt, 0);
		db_finalise(&stmt);
		if (count < cmp) {
			kerrx("nonce replay attack: %s, "
				"%" PRId64 " < %" PRId64, 
				nonce, count, cmp);
			return NONCE_REPLAY;
		}
		return NONCE_OK;
	case SQLITE_DONE:
		db_finalise(&stmt);
		return NONCE_NOTFOUND;
	default:
		break;
	}
err:
	db_finalise(&stmt);
	return NONCE_ERR;
}

/*
 * Validate then update nonce to be one greater than the given count.
 * Returns the corresponding error code.
 */
enum nonceerr
db_nonce_update(const char *nonce, int64_t count)
{
	enum nonceerr	 er;
	sqlite3_stmt	*stmt;

	if (!db_trans_open())
		return NONCE_ERR;

	if ((er = db_nonce_validate(nonce, count)) != NONCE_OK) {
		db_trans_rollback();
		return er;
	}

	/* FIXME: check for (unlikely) integer overflow. */

	if ((stmt = db_prepare(sqls[SQL_NONCE_UPDATE])) == NULL)
		goto err;
	else if (!db_bindint(stmt, 1, count + 1))
		goto err;
	else if (!db_bindtext(stmt, 2, nonce))
		goto err;
	else if (db_step(stmt) != SQLITE_DONE)
		goto err;

	db_finalise(&stmt);
	db_trans_commit();
	kdbg("nonce updated: %s, count "
		"%" PRId64, nonce, count + 1);
	return NONCE_OK;
err:
	db_finalise(&stmt);
	db_trans_rollback();
	return NONCE_ERR;
}

/*
 * Create a new nonce and on success set its value in "np".
 * This is in static storage and is overwritten with every call.
 * Return zero on failure, non-zero on success.
 */
int
db_nonce_new(char **np)
{
	static char	 nonce[NONCESZ + 1];
	int64_t		 count;
	sqlite3_stmt	*stmt;
	int		 rc;
	size_t		 i;

	if (!db_trans_open())
		return 0;

	/* 
	 * If we have more than NONCEMAX nonces in the database, then
	 * cull the first 20 to make room for more.
	 */

	if ((stmt = db_prepare(sqls[SQL_NONCE_COUNT])) == NULL)
		goto err;
	else if ((rc = db_step(stmt)) == SQLITE_ROW)
		count = sqlite3_column_int64(stmt, 0);
	else if (rc == SQLITE_DONE)
		count = 0;
	else
		goto err;

	db_finalise(&stmt);

	if (count >= NONCEMAX) {
		kdbg("culling from nonce database");
		stmt = db_prepare(sqls[SQL_NONCE_REMOVE_MULTI]);
		if (stmt == NULL)
			goto err;
		else if (db_step(stmt) != SQLITE_DONE)
			goto err;
		db_finalise(&stmt);
	}

	/* 
	 * Generate a random nonce and insert it into the database.
	 * Let the uniqueness constraint guarantee that the nonce is
	 * actually unique within the system.
	 */

	if ((stmt = db_prepare(sqls[SQL_NONCE_INSERT])) == NULL)
		goto err;

	for (;;) {
		for (i = 0; i < sizeof(nonce) - 1; i++)
			snprintf(nonce + i, 2, "%X", 
				get_random_uniform(16));
		if (!db_bindtext(stmt, 1, nonce))
			goto err;
		rc = db_step_constrained(stmt);
		if (rc == SQLITE_CONSTRAINT) {
			sqlite3_reset(stmt);
			continue;
		} else if (rc != SQLITE_DONE)
			goto err;
		break;
	}

	db_finalise(&stmt);
	db_trans_commit();
	*np = nonce;
	kdbg("nonce created: %s", *np);
	return 1;
err:
	db_finalise(&stmt);
	db_trans_rollback();
	return 0;
}

/*
 * Create a new collection.
 * Return zero if the collection exists, <0 on error, >0 on success.
 */
static int
db_collection_new_byid(const char *url, int64_t id)
{
	sqlite3_stmt	*stmt;
	int		 rc;

	if ((stmt = db_prepare(sqls[SQL_COL_INSERT])) == NULL)
		goto err;
	else if (!db_bindint(stmt, 1, id))
		goto err;
	else if (!db_bindtext(stmt, 2, url))
		goto err;

	rc = db_step_constrained(stmt);
	db_finalise(&stmt);

	if (rc == SQLITE_DONE) {
		kinfo("collection created: %s", url);
		return 1;
	} else if (rc == SQLITE_CONSTRAINT)
		return 0;
err:
	db_finalise(&stmt);
	return (-1);
}

int
db_collection_new(const char *url, const struct prncpl *p)
{

	return db_collection_new_byid(url, p->id);
}

/*
 * Allocate a new principal with a single collection: the principal
 * collection (empty collection name) and a calendar collection
 * (/calendars).
 * Accept their login name, the password hash, and their email.
 * This returns <0 on system failure, 0 if the principal already exists,
 * and >0 if the principal was created.
 */
int
db_prncpl_new(const char *name, const char *hash, 
	const char *email, const char *directory)
{
	sqlite3_stmt	*stmt;
	int		 rc;
	int64_t		 lastid;

	assert(directory != NULL && directory[0] != '\0');

	if (!db_trans_open()) 
		return (-1);

	if ((stmt = db_prepare(sqls[SQL_PRNCPL_INSERT])) == NULL)
		goto err;
	else if (!db_bindtext(stmt, 1, name))
		goto err;
	else if (!db_bindtext(stmt, 2, hash))
		goto err;
	else if (!db_bindtext(stmt, 3, email))
		goto err;

	rc = db_step_constrained(stmt);

	if (rc != SQLITE_DONE && rc != SQLITE_CONSTRAINT)
		goto err;

	db_finalise(&stmt);

	if (SQLITE_CONSTRAINT == rc) {
		db_trans_rollback();
		return 0;
	}

	kinfo("principal created: %s, %s", email, name);

	lastid = sqlite3_last_insert_rowid(db);
	if (db_collection_new_byid(directory, lastid) > 0) {
		db_trans_commit();
		kinfo("principal collection created: %s", directory);
		return 1;
	}

err:
	db_finalise(&stmt);
	db_trans_rollback();
	return (-1);
}

/*
 * Change the hash and e-mail of a principal to the hash set in the
 * principal object.
 * This returns zero on constraint failure (email), >0 on success, <0 on
 * failure.
 */
int
db_prncpl_update(const struct prncpl *p)
{
	sqlite3_stmt	*stmt;
	int		 rc;

	if ((stmt = db_prepare(sqls[SQL_PRNCPL_UPDATE])) == NULL)
		goto err;
	else if (!db_bindtext(stmt, 1, p->hash))
		goto err;
	else if (!db_bindtext(stmt, 2, p->email))
		goto err;
	else if (!db_bindint(stmt, 3, p->id))
		goto err;

	rc = db_step_constrained(stmt);
	if (rc != SQLITE_CONSTRAINT && rc != SQLITE_DONE)
		goto err;

	db_finalise(&stmt);
	kinfo("principal updated");
	return rc == SQLITE_DONE;
err:
	db_finalise(&stmt);
	return (-1);
}

static int
db_collection_get(struct coln **pp, sqlite3_stmt *stmt)
{
	int	 rc;

	if ((rc = db_step(stmt)) == SQLITE_DONE) 
		return 0;
	else if (rc != SQLITE_ROW)
		return (-1);

	if ((*pp = calloc(1, sizeof(struct coln))) == NULL) {
		kerr(NULL);
		return (-1);
	}

	(*pp)->url = strdup
		((char *)sqlite3_column_text(stmt, 0));
	(*pp)->displayname = strdup
		((char *)sqlite3_column_text(stmt, 1));
	(*pp)->colour = strdup
		((char *)sqlite3_column_text(stmt, 2));
	(*pp)->description = strdup
		((char *)sqlite3_column_text(stmt, 3));
	(*pp)->ctag = sqlite3_column_int64(stmt, 4);
	(*pp)->id = sqlite3_column_int64(stmt, 5);

	if (NULL != (*pp)->url &&
	    NULL != (*pp)->displayname &&
	    NULL != (*pp)->colour &&
	    NULL != (*pp)->description)
		return 1;

	kerr(NULL);
	db_collection_free(*pp);
	return (-1);
}

/*
 * Delete a proxy.
 * Returns zero on failure, non-zero on success.
 */
int
db_proxy_remove(const struct prncpl *p, int64_t id)
{
	sqlite3_stmt	*stmt;

	if ((stmt = db_prepare(sqls[SQL_PROXY_REMOVE])) == NULL)
		goto err;
	else if (!db_bindint(stmt, 1, p->id))
		goto err;
	else if (!db_bindint(stmt, 2, id))
		goto err;
	else if (db_step(stmt) != SQLITE_DONE)
		goto err;

	db_finalise(&stmt);
	kinfo("deleted proxy (maybe) to %" PRId64, id);
	return 1;
err:
	db_finalise(&stmt);
	return 0;
}

/*
 * Create or modify a proxy.
 * This allows the principal "id" to access "bits" on the principal "p".
 * Returns <0 on system failure, 0 if the "id" principal does not exist,
 * or >0 if everything went well.
 */
int
db_proxy(const struct prncpl *p, int64_t id, int64_t bits)
{
	int	 	 rc = -1;
	sqlite3_stmt	*stmt;

	assert(bits == 1 || bits == 2);

	/* Transaction to wrap the test-set. */

	if (!db_trans_open())
		return (-1);

	/* First try to create a new one. */

	if ((stmt = db_prepare(sqls[SQL_PROXY_INSERT])) == NULL)
		goto err;
	else if (!db_bindint(stmt, 1, p->id))
		goto err;
	else if (!db_bindint(stmt, 2, id))
		goto err;
	else if (!db_bindint(stmt, 3, bits))
		goto err;

	rc = db_step_constrained(stmt);
	db_finalise(&stmt);

	if (rc == SQLITE_DONE) {
		if (!db_trans_commit())
			return (-1);
		kinfo("proxy created to %" PRId64 
			": %" PRId64, id, bits);
		return 1;
	} else if (rc != SQLITE_CONSTRAINT)
		goto err;

	/* Field (might?) exist. */

	if ((stmt = db_prepare(sqls[SQL_PROXY_UPDATE])) == NULL)
		goto err;
	else if (!db_bindint(stmt, 1, bits))
		goto err;
	else if (!db_bindint(stmt, 2, p->id))
		goto err;
	else if (!db_bindint(stmt, 3, id))
		goto err;

	rc = db_step_constrained(stmt);
	db_finalise(&stmt);

	if (SQLITE_CONSTRAINT == rc) {
		if (!db_trans_rollback())
			return (-1);
		return 0;
	} else if (rc != SQLITE_DONE)
		goto err;

	/* All is well. */

	if (!db_trans_commit())
		return(-1);

	kinfo("proxy updated to %" PRId64 ": %" PRId64, id, bits);
	return 1;
err:
	db_finalise(&stmt);
	db_trans_rollback();
	return rc;
}

/*
 * Load collection and, on success, set it in "pp" which must be freed
 * later.
 * Returns <0 on failure, 0 if collection not found, >0 on success.
 */
int
db_collection_loadid(struct coln **pp, int64_t id, int64_t pid)
{
	sqlite3_stmt	*stmt;
	int	 	 rc = -1;

	if ((stmt = db_prepare(sqls[SQL_COL_GET_ID])) == NULL)
		goto err;
	else if (!db_bindint(stmt, 1, pid))
		goto err;
	else if (!db_bindint(stmt, 2, id))
		goto err;

	if ((rc = db_collection_get(pp, stmt)) >= 0) {
		db_finalise(&stmt);
		return rc;
	}
err:
	db_finalise(&stmt);
	return rc;
}

/*
 * Load collection and, on success, set it in "pp" which must be freed
 * later.
 * Returns <0 on failure, 0 if collection not found, >0 on success.
 */
int
db_collection_load(struct coln **pp, const char *url, int64_t id)
{
	sqlite3_stmt	*stmt;
	int	 	 rc = -1;

	*pp = NULL;

	if ((stmt = db_prepare(sqls[SQL_COL_GET])) == NULL)
		goto err;
	else if (!db_bindint(stmt, 1, id))
		goto err;
	else if (!db_bindtext(stmt, 2, url))
		goto err;

	if ((rc = db_collection_get(pp, stmt)) >= 0) {
		db_finalise(&stmt);
		return rc;
	}
err:
	db_finalise(&stmt);
	return rc;
}

void
db_collection_free(struct coln *p)
{

	if (NULL == p)
		return;

	free(p->url);
	free(p->displayname);
	free(p->colour);
	free(p->description);
	free(p);
}

/*
 * Get a principal's identifier.
 * Return the identifier or <0 on failure.
 */
int64_t
db_prncpl_identify(const char *email)
{
	sqlite3_stmt	*stmt;
	int64_t		 id;
	int		 rc;

	if ((stmt = db_prepare(sqls[SQL_PRNCPL_GET_ID])) == NULL)
		goto err;
	else if (!db_bindtext(stmt, 1, email))
		goto err;
	
	if ((rc = db_step(stmt)) == SQLITE_DONE) {
		db_finalise(&stmt);
		return 0;
	} else if (rc != SQLITE_ROW)
		goto err;

	id = sqlite3_column_int64(stmt, 0);
	db_finalise(&stmt);
	return id;
err:
	db_finalise(&stmt);
	return -1;
}

/*
 * Load the principal "name" into "pp", allocating it.
 * Returns 0 if the principal isn't found <0 on error, >0 on success.
 * Principal is only set if >0 return value.
 */
int
db_prncpl_load(struct prncpl **pp, const char *name)
{
	struct prncpl	*p;
	size_t		 i;
	sqlite3_stmt	*stmt;
	int		 c, rc = -1;
	void		*vp;
	struct statvfs	 sfs;

	p = *pp = calloc(1, sizeof(struct prncpl));
	if (p == NULL) {
		kerr(NULL);
		return (-1);
	}

	if ((stmt = db_prepare(sqls[SQL_PRNCPL_GET])) == NULL)
		goto err;
	else if (!db_bindtext(stmt, 1, name))
		goto err;

	if ((c = db_step(stmt)) == SQLITE_DONE) {
		rc = 0;
		goto err;
	} else if (c != SQLITE_ROW)
		goto err;

	p->name = strdup(name);
	p->hash = strdup((char *)sqlite3_column_text(stmt, 0));
	p->id = sqlite3_column_int(stmt, 1);
	p->email = strdup((char *)sqlite3_column_text(stmt, 2));

	db_finalise(&stmt);

	if (NULL == p->name || NULL == p->hash || NULL == p->email) {
		kerr(NULL);
		goto err;
	}

	if (statvfs(dbname, &sfs) == -1) {
		kerr("statvfs: %s", dbname);
		goto err;
	}

	p->quota_used = sfs.f_blocks * sfs.f_bsize;
	p->quota_avail = sfs.f_bfree * sfs.f_bsize;

	/* Read in each collection owned by the given principal. */

	if ((stmt = db_prepare(sqls[SQL_COL_ITER])) == NULL)
		goto err;
	else if (!db_bindint(stmt, 1, p->id))
		goto err;

	while ((c = db_step(stmt)) == SQLITE_ROW) {
		vp = reallocarray(p->cols, 
			p->colsz + 1, sizeof(struct coln));
		if (vp == NULL) {
			kerr(NULL);
			goto err;
		}
		p->cols = vp;
		i = p->colsz++;
		p->cols[i].url = strdup
			((char *)sqlite3_column_text(stmt, 0));
		p->cols[i].displayname = strdup
			((char *)sqlite3_column_text(stmt, 1));
		p->cols[i].colour = strdup
			((char *)sqlite3_column_text(stmt, 2));
		p->cols[i].description = strdup
			((char *)sqlite3_column_text(stmt, 3));
		p->cols[i].ctag = sqlite3_column_int64(stmt, 4);
		p->cols[i].id = sqlite3_column_int64(stmt, 5);

		if (NULL == p->cols[i].url ||
		    NULL == p->cols[i].displayname ||
		    NULL == p->cols[i].colour ||
		    NULL == p->cols[i].description) {
			kerr(NULL);
			goto err;
		}
	}

	if (c != SQLITE_DONE)
		goto err;
	db_finalise(&stmt);

	/* Read all reverse proxies. */

	if ((stmt = db_prepare(sqls[SQL_PROXY_ITER])) == NULL)
		goto err;
	else if (!db_bindint(stmt, 1, p->id))
		goto err;

	while ((c = db_step(stmt)) == SQLITE_ROW) {
		vp = reallocarray(p->rproxies, 
			p->rproxiesz + 1, sizeof(struct proxy));
		if (vp == NULL) {
			kerr(NULL);
			goto err;
		}
		p->rproxies = vp;
		i = p->rproxiesz++;
		p->rproxies[i].email = strdup
			((char *)sqlite3_column_text(stmt, 0));
		p->rproxies[i].name = strdup
			((char *)sqlite3_column_text(stmt, 1));
		p->rproxies[i].bits = sqlite3_column_int64(stmt, 2);
		p->rproxies[i].proxy = sqlite3_column_int64(stmt, 3);
		p->rproxies[i].id = sqlite3_column_int64(stmt, 4);
		if (NULL == p->rproxies[i].email ||
		    NULL == p->rproxies[i].name) {
			kerr(NULL);
			goto err;
		}
	}

	if (c != SQLITE_DONE)
		goto err;
	db_finalise(&stmt);

	if ((stmt = db_prepare(sqls[SQL_PROXY_ITER_PRNCPL])) == NULL)
		goto err;
	else if (!db_bindint(stmt, 1, p->id))
		goto err;

	while ((c = db_step(stmt)) == SQLITE_ROW) {
		vp = reallocarray(p->proxies,
			 p->proxiesz + 1, sizeof(struct proxy));
		if (vp == NULL) {
			kerr(NULL);
			goto err;
		}
		p->proxies = vp;
		i = p->proxiesz++;
		p->proxies[i].email = strdup
			((char *)sqlite3_column_text(stmt, 0));
		p->proxies[i].name = strdup
			((char *)sqlite3_column_text(stmt, 1));
		p->proxies[i].bits = sqlite3_column_int64(stmt, 2);
		p->proxies[i].proxy = sqlite3_column_int64(stmt, 3);
		p->proxies[i].id = sqlite3_column_int64(stmt, 4);

		if (NULL == p->proxies[i].email ||
		    NULL == p->proxies[i].name) {
			kerr(NULL);
			goto err;
		}
	}

	if (c != SQLITE_DONE)
		goto err;
	db_finalise(&stmt);
	return 1;
err:
	*pp = NULL;
	db_prncpl_free(p);
	db_finalise(&stmt);
	return rc;
}

/*
 * Push the display name, colour, and description of a collection from
 * "c" into database storage.
 * Returns zero on failure, non-zero on success.
 */
int
db_collection_update(const struct coln *c, const struct prncpl *p)
{
	sqlite3_stmt	*stmt;

	if ((stmt = db_prepare(sqls[SQL_COL_UPDATE])) == NULL)
		goto err;
	else if (!db_bindtext(stmt, 1, c->displayname))
		goto err;
	else if (!db_bindtext(stmt, 2, c->colour))
		goto err;
	else if (!db_bindtext(stmt, 3, c->description))
		goto err;
	else if (!db_bindint(stmt, 4, c->id))
		goto err;
	else if (db_step(stmt) != SQLITE_DONE)
		goto err;

	db_finalise(&stmt);
	kinfo("collection updated: %" PRId64, c->id);

	if (db_collection_update_ctag(c->id))
		return 1;
err:
	db_finalise(&stmt);
	return 0;
}

/*
 * List all resources in a collection.
 * Return zero on failure, non-zero on success.
 * This can return failure after the callback has been invoked.
 */
int
db_collection_resources(void (*fp)(const struct res *, void *), 
	int64_t colid, void *arg)
{
	sqlite3_stmt	*stmt;
	size_t		 rsz, sz;
	int		 rc;
	char		*er;
	struct res	 p;

	if ((stmt = db_prepare(sqls[SQL_RES_ITER])) == NULL)
		goto err;
	else if (!db_bindint(stmt, 1, colid))
		goto err;

	while ((rc = db_step(stmt)) == SQLITE_ROW) {
		memset(&p, 0, sizeof(struct res));
		p.data = (char *)sqlite3_column_text(stmt, 0);
		p.etag = (char *)sqlite3_column_text(stmt, 1);
		p.url = (char *)sqlite3_column_text(stmt, 2);
		p.id = sqlite3_column_int64(stmt, 3);
		p.collection = sqlite3_column_int64(stmt, 4);
		sz = strlen(p.data);
		p.ical = ical_parse(NULL, p.data, sz, &rsz, &er);
		if (p.ical == NULL) {
			kerrx("ical_parse: %s", er);
			free(er);
			goto err;
		} else if (rsz != sz)
			kdbg("ical_parse: trailing bytes (%zu < %zu)",
				rsz, sz);
		(*fp)(&p, arg);
		ical_free(p.ical);
	}
	if (rc != SQLITE_DONE)
		goto err;
	db_finalise(&stmt);
	return 1;
err:
	db_finalise(&stmt);
	return 0;
}

/*
 * Delete collection from database without verifying that it exists.
 * Return zero on failure, non-zero on success.
 */
int
db_collection_remove(int64_t id, const struct prncpl *p)
{
	sqlite3_stmt	*stmt;

	if ((stmt = db_prepare(sqls[SQL_COL_REMOVE])) == NULL)
		goto err;
	else if (!db_bindint(stmt, 1, id))
		goto err;
	else if (db_step(stmt) != SQLITE_DONE)
		goto err;

	db_finalise(&stmt);
	kinfo("collection removed (maybe): %" PRId64, id);
	return 1;
err:
	db_finalise(&stmt);
	return 0;
}

/*
 * Delete a resource from the collection and update the ctag.
 * Only do this if the resource matches the given tag or if the resource
 * wasn't found (so it's kinda... already deleted?).
 * Returns zero on failure, non-zero on success.
 */
int
db_resource_delete(const char *url, const char *tag, int64_t colid)
{
	sqlite3_stmt	*stmt;
	int		 rc;

	if (!db_trans_open()) 
		return 0;

	/* Throw away the results of this query. */

	if ((stmt = db_prepare(sqls[SQL_RES_GET_ETAG])) == NULL)
		goto err;
	else if (!db_bindtext(stmt, 1, url))
		goto err;
	else if (!db_bindint(stmt, 2, colid))
		goto err;
	else if (!db_bindtext(stmt, 3, tag))
		goto err;
	
	rc = db_step(stmt);
	db_finalise(&stmt);

	if (rc == SQLITE_DONE) {
		db_trans_rollback();
		return 1;
	} else if (rc != SQLITE_ROW)
		goto err;

	if ((stmt = db_prepare(sqls[SQL_RES_REMOVE_ETAG])) == NULL)
		goto err;
	else if (!db_bindtext(stmt, 1, url))
		goto err;
	else if (!db_bindint(stmt, 2, colid))
		goto err;
	else if (!db_bindtext(stmt, 3, tag))
		goto err;
	else if (db_step(stmt) != SQLITE_DONE)
		goto err;

	if (db_collection_update_ctag(colid)) {
		db_finalise(&stmt);
		db_trans_commit();
		kinfo("resource removed: %s", url);
		return 1;
	}
err:
	db_finalise(&stmt);
	db_trans_rollback();
	return 0;
}

/*
 * Delete a resource from the collection and update the ctag.
 * This is NOT a safe operation: it deletes without checking the etag
 * for correctness.
 * Returns zero on failure, non-zero on success.
 */
int
db_resource_remove(const char *url, int64_t colid)
{
	sqlite3_stmt	*stmt;

	if ((stmt = db_prepare(sqls[SQL_RES_REMOVE])) == NULL)
		goto err;
	else if (!db_bindtext(stmt, 1, url))
		goto err;
	else if (!db_bindint(stmt, 2, colid))
		goto err;
	else if (db_step(stmt) != SQLITE_DONE)
		goto err;

	db_finalise(&stmt);
	kinfo("resource removed (unsafe): %s", url);

	if (db_collection_update_ctag(colid))
		return 1;
err:
	db_finalise(&stmt);
	return 0;
}

/*
 * Create a new resource at "url" in "colid".
 * It initialises the etag to a random number and updates the
 * collection etag as well.
 * This returns <0 if a system error occurs, 0 if a resource by that
 * name already exists, or >0 on success.
 */
int
db_resource_new(const char *data, const char *url, int64_t colid)
{
	sqlite3_stmt	*stmt;
	int		 rc;
	char		 etag[64];

	snprintf(etag, sizeof(etag), "%" PRIu32 "-%" PRIu32, 
		get_random(), get_random());

	if ((stmt = db_prepare(sqls[SQL_RES_INSERT])) == NULL)
		goto err;
	else if (!db_bindtext(stmt, 1, data))
		goto err;
	else if (!db_bindtext(stmt, 2, url))
		goto err;
	else if (!db_bindint(stmt, 3, colid))
		goto err;
	else if (!db_bindtext(stmt, 4, etag))
		goto err;

	rc = db_step_constrained(stmt);
	db_finalise(&stmt);

	if (rc == SQLITE_CONSTRAINT)
		return 0;
	else if (rc != SQLITE_DONE)
		goto err;

	kinfo("resource created: %s", url);

	if (db_collection_update_ctag(colid))
		return 1;
err:
	db_finalise(&stmt);
	return (-1);
}

/*
 * Update the resource at "url" in collection "colid" with new data,
 * updating its and the collection's etag to a random number.
 * Make sure that the existing etag matches "digest".
 * Returns <0 on error, 0 if the resource couldn't be found, or >0 on
 * success.
 */
int
db_resource_update(const char *data, const char *url, 
	const char *digest, int64_t colid)
{
	sqlite3_stmt	*stmt = NULL;
	struct res	*res = NULL;
	int		 rc;
	int64_t		 id;
	char		 etag[64];

	snprintf(etag, sizeof(etag), "%" PRIu32 "-%" PRIu32,
		get_random(), get_random());

	if (!db_trans_open()) 
		return (-1);

	if ((rc = db_resource_load(&res, url, colid)) == 0) {
		db_trans_rollback();
		return 0;
	} else if (rc < 0)
		goto err;

	/* Check digest equality. */

	if (strcmp(res->etag, digest)) {
		db_trans_rollback();
		db_resource_free(res);
		return 0;
	}

	id = res->id;
	db_resource_free(res);

	if ((stmt = db_prepare(sqls[SQL_RES_UPDATE])) == NULL)
		goto err;
	else if (!db_bindtext(stmt, 1, data))
		goto err;
	else if (!db_bindtext(stmt, 2, etag))
		goto err;
	else if (!db_bindint(stmt, 3, id))
		goto err;
	else if (db_step(stmt) != SQLITE_DONE)
		goto err;

	db_finalise(&stmt);

	if (db_collection_update_ctag(colid)) {
		db_trans_commit();
		kinfo("resource updated: %s", url);
		return 1;
	}
err:
	db_finalise(&stmt);
	db_trans_rollback();
	return (-1);
}

/*
 * Load the resource named "url" within collection "colid".
 * Returns zero if the resource was not found, <0 if the retrieval
 * failed in some way, or >0 if the retrieval was successful.
 * On success, "pp" is set to the resource.
 */
int
db_resource_load(struct res **pp, const char *url, int64_t colid)
{
	sqlite3_stmt	*stmt;
	int		 rc;
	char		*er;
	size_t		 sz, rsz;

	*pp = NULL;
	if ((stmt = db_prepare(sqls[SQL_RES_GET])) == NULL)
		goto err;
	else if (!db_bindint(stmt, 1, colid))
		goto err;
	else if (!db_bindtext(stmt, 2, url))
		goto err;

	if ((rc = db_step(stmt)) == SQLITE_ROW) {
		if ((*pp = calloc(1, sizeof(struct res))) == NULL) {
			kerr(NULL);
			goto err;
		}
		(*pp)->data = strdup
			((char *)sqlite3_column_text(stmt, 0));
		(*pp)->etag = strdup
			((char *)sqlite3_column_text(stmt, 1));
		(*pp)->url = strdup
			((char *)sqlite3_column_text(stmt, 2));
		(*pp)->id = sqlite3_column_int64(stmt, 3);
		(*pp)->collection = sqlite3_column_int64(stmt, 4);
		if ((*pp)->data == NULL || 
		    (*pp)->etag == NULL ||
		    (*pp)->url == NULL) {
			kerr(NULL);
			goto err;
		}

		/* Parse the full iCalendar. */

		sz = strlen((*pp)->data);
		(*pp)->ical = ical_parse(NULL, (*pp)->data, sz, &rsz,
			&er);
		if ((*pp)->ical == NULL) {
			kerrx("ical_parse: %s", er);
			free(er);
			goto err;
		} else if (rsz != sz)
			kdbg("ical_parse: trailing bytes (%zu < %zu)",
				rsz, sz);
		db_finalise(&stmt);
		return 1;
	} else if (rc == SQLITE_DONE) {
		db_finalise(&stmt);
		return 0;
	}

err:
	if (*pp != NULL) {
		free((*pp)->etag);
		free((*pp)->data);
		free((*pp)->url);
		free(*pp);
	}
	db_finalise(&stmt);
	return (-1);
}

/*
 * This checks the ownership of a database file.
 * If the file is newly-created, it creates the database schema and
 * initialises the ownership to the given identifier.
 * Return <0 on failure, 0 if the owner doesn't match the real user, or
 * >0 on success.
 */
int
db_owner_check_or_set(int64_t id)
{
	sqlite3_stmt	*stmt;
	int64_t		 oid;

	if ((stmt = db_prepare(sqls[SQL_OWNER_GET])) != NULL) {
		if (db_step(stmt) != SQLITE_ROW) 
			goto err;
		oid = sqlite3_column_int64(stmt, 0);
		db_finalise(&stmt);
		if (id == 0 && oid != id)
			kinfo("root overriding: %" PRId64, oid);
		return id == 0 || oid == id;
	}

	/*
	 * Assume that the database has not been initialised and try to
	 * do so here with a hardcoded schema.
	 */

	if (db_exec(db_sql) != SQLITE_OK)
		goto err;

	/* Finally, insert our database record. */

	if ((stmt = db_prepare(sqls[SQL_OWNER_INSERT])) == NULL)
		goto err;
	else if (!db_bindint(stmt, 1, id))
		goto err;
	else if (db_step(stmt) != SQLITE_DONE) 
		goto err;

	db_finalise(&stmt);
	return 1;
err:
	db_finalise(&stmt);
	return (-1);
}

void
db_prncpl_free(struct prncpl *p)
{
	size_t	 i;

	if (NULL == p)
		return;

	free(p->name);
	free(p->hash);
	free(p->email);
	for (i = 0; i < p->colsz; i++) {
		free(p->cols[i].url);
		free(p->cols[i].displayname);
		free(p->cols[i].colour);
		free(p->cols[i].description);
	}
	free(p->cols);
	for (i = 0; i < p->proxiesz; i++) {
		free(p->proxies[i].email);
		free(p->proxies[i].name);
	}
	free(p->proxies);
	for (i = 0; i < p->rproxiesz; i++) {
		free(p->rproxies[i].email);
		free(p->rproxies[i].name);
	}
	free(p->rproxies);
	free(p);
}

void
db_resource_free(struct res *p)
{

	if (NULL == p)
		return;
	free(p->etag);
	free(p->url);
	free(p->data);
	ical_free(p->ical);
	free(p);
}
