/*	$Id$ */
/*
 * Copyright (c) 2015 Kristaps Dzonsons <kristaps@bsd.lv>
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

#ifdef __linux__
#include <sys/vfs.h>
#else
#include <sys/param.h>
#include <sys/mount.h>
#endif

#include <assert.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#ifdef __linux__
#include <bsd/stdlib.h>
#include <bsd/string.h>
#endif
#include <unistd.h>

#include <kcgi.h>
#include <kcgixml.h>
#include <sqlite3.h>

#include "extern.h"

static sqlite3	*db;
static char	 dbname[PATH_MAX];

#define NONCEMAX 1000
#define NONCESZ	 16

/*
 * Close the database and reset the database filename.
 * This should be called on exit time with atexit(3).
 */
static void
db_close(void)
{

	if (SQLITE_OK != sqlite3_close(db))
		perror(sqlite3_errmsg(db));

	db = NULL;
	dbname[0] = '\0';
}

/*
 * Finalise and nullify a statement.
 * Use this instead of sqlite3_finalize() so we catch any re-uses of the
 * statement object.
 */
static void
db_finalise(sqlite3_stmt **stmt)
{

	sqlite3_finalize(*stmt);
	*stmt = NULL;
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
	int	 rc;

	assert(NULL != db);
	rc = sqlite3_step(stmt);
	if (SQLITE_ROW == rc || 
		 SQLITE_DONE == rc || 
		 SQLITE_CONSTRAINT == rc) 
		return(rc);
	kerrx("%s: %s", dbname, sqlite3_errmsg(db));
	return(rc);
}

/*
 * Step through any results on a prepared statement.
 * We report errors on anything other than SQLITE_ROW and SQLITE_DONE.
 * Returns the sqlite3 error code.
 */
static int
db_step(sqlite3_stmt *stmt)
{
	int	 rc;

	assert(NULL != db);
	rc = sqlite3_step(stmt);
	if (SQLITE_ROW == rc || 
		 SQLITE_DONE == rc)
		return(rc);
	kerrx("%s: %s", dbname, sqlite3_errmsg(db));
	return(rc);
}

/*
 * Bind a 64-bit integer "v" to the statement.
 * Return zero on failure, non-zero on success.
 */
static int
db_bindint(sqlite3_stmt *stmt, size_t pos, int64_t v)
{
	int	 rc;

	assert(NULL != db);
	assert(pos > 0);
	rc = sqlite3_bind_int64(stmt, pos, v);
	if (SQLITE_OK == rc) 
		return(1);
	kerrx("%s: %s", dbname, sqlite3_errmsg(db));
	return(0);
}

/*
 * Bind a nil-terminated string "name" to the statement.
 * Return zero on failure, non-zero on success.
 */
static int
db_bindtext(sqlite3_stmt *stmt, size_t pos, const char *name)
{
	int	 rc;

	assert(NULL != db);
	assert(pos > 0);
	rc = sqlite3_bind_text(stmt, pos, name, -1, SQLITE_STATIC);
	if (SQLITE_OK == rc) 
		return(1);
	kerrx("%s: %s", dbname, sqlite3_errmsg(db));
	return(0);
}

/*
 * Execute a non-parameterised SQL statement.
 * Returns the sqlite3 error code, reporting the error if it doesn't
 * equal SQLITE_OK.
 */
static int
db_exec(const char *sql)
{
	int	 rc;

	assert(NULL != db);
	rc = sqlite3_exec(db, sql, NULL, NULL, NULL);
	if (SQLITE_OK == rc)
		return(rc);
	kerrx("%s: %s", dbname, sqlite3_errmsg(db));
	return(rc);
}

static int
db_trans_open(int immediate)
{
	const char	*sql;

	sql = immediate ? 
		"BEGIN TRANSACTION IMMEDIATE" :
		"BEGIN TRANSACTION";

	if (SQLITE_OK == db_exec(sql))
		return(1);
	kerrx("%s: %s", dbname, sqlite3_errmsg(db));
	return(0);
}

static int
db_trans_rollback(void)
{

	if (SQLITE_OK == db_exec("ROLLBACK TRANSACTION"))
		return(1);
	kerrx("%s: %s", dbname, sqlite3_errmsg(db));
	return(0);
}

static int
db_trans_commit(void)
{

	if (SQLITE_OK == db_exec("COMMIT TRANSACTION"))
		return(1);
	kerrx("%s: %s", dbname, sqlite3_errmsg(db));
	return(0);
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
	int		 rc;

	assert(NULL != db);
	rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
	if (SQLITE_OK == rc) 
		return(stmt);
	kerrx("%s: %s", dbname, sqlite3_errmsg(db));
	sqlite3_finalize(stmt);
	return(NULL);
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
	size_t	 i, sz;
	int	 rc;

	/* 
	 * Register exit hook for the destruction of the database. 
	 * This allows us to ignore closing the database properly.
	 */
	if (-1 == atexit(db_close)) {
		kerr("atexit");
		return(0);
	}

	/* Format the name of the database. */
	sz = strlcpy(dbname, dir, sizeof(dbname));
	if (sz  > sizeof(dbname)) {
		printf("%s: too long", dir);
		return(0);
	} else if ('/' == dbname[sz - 1])
		dbname[sz - 1] = '\0';

	sz = strlcat(dbname, "/kcaldav.db", sizeof(dbname));
	if (sz >= sizeof(dbname)) {
		kerrx("%s: too long", dir);
		return(0);
	}

	/* 
	 * Try to open the database.
	 * We repeat our try 100 times, waiting for a random value
	 * within 100,000 microseconds (1/10 second) to try again.
	 * Exit on any non-block-related errors.
	 */
	for (i = 0; i < 100; i++) {
		rc = sqlite3_open_v2(dbname, &db, 
			SQLITE_OPEN_READWRITE | 
			(create ? SQLITE_OPEN_CREATE : 0),
			NULL);
		if (SQLITE_OK == rc)
			break;
		if (SQLITE_BUSY == rc || SQLITE_LOCKED == rc) {
			usleep(arc4random_uniform(100000));
			continue;
		} 
		kerrx("%s: %s", dbname, sqlite3_errmsg(db));
		return(0);
	}

	/* Too many failures. */
	if (i == 100) {
		kerrx("%s: open timeout", dbname);
		return(0);
	}

	/*
	 * Install the default timeout for busy-waiting on the database
	 * to be 1 second.
	 */
	sqlite3_busy_timeout(db, 1000);
	kdbg("%s: database ready", dbname);
	if (SQLITE_OK != db_exec("PRAGMA foreign_keys = ON")) {
		kerrx("%s: cannot enable foreign keys", dbname);
		return(0);
	}
	return(1);
}

static int
db_collection_ctag(int64_t id)
{
	const char	*sql;
	sqlite3_stmt	*stmt;

	sql = "UPDATE collection SET ctag=ctag+1 WHERE id=?";
	if (NULL == (stmt = db_prepare(sql)))
		goto err;
	else if ( ! db_bindint(stmt, 1, id))
		goto err;
	else if (SQLITE_DONE != db_step(stmt))
		goto err;

	db_finalise(&stmt);
	return(1);
err:
	kerrx("%s: %s: failure", dbname, __func__);
	db_finalise(&stmt);
	return(0);
}

enum nonceerr
db_nonce_validate(const char *nonce, size_t count)
{
	const char	*sql;
	sqlite3_stmt	*stmt;
	size_t		 cmp;
	int		 rc;

	sql = "SELECT count FROM nonce WHERE nonce=?";
	if (NULL == (stmt = db_prepare(sql)))
		goto err;
	else if ( ! db_bindtext(stmt, 1, nonce))
		goto err;
	
	if (SQLITE_ROW == (rc = db_step(stmt))) {
		cmp = (size_t)sqlite3_column_int64(stmt, 0);
		db_finalise(&stmt);
		return(count < cmp ? NONCE_REPLAY : NONCE_OK);
	} else if (SQLITE_DONE != rc)
		goto err;

	db_finalise(&stmt);
	return(NONCE_NOTFOUND);
err:
	kerrx("%s: %s: failure", dbname, __func__);
	db_finalise(&stmt);
	return(NONCE_ERR);
}

enum nonceerr
db_nonce_update(const char *nonce, size_t count)
{
	enum nonceerr	 er;
	sqlite3_stmt	*stmt;
	const char	*sql;
	int		 rc;

	rc = -2;
	stmt = NULL;

	if ( ! db_trans_open(1))
		return(NONCE_ERR);

	if (NONCE_OK != (er = db_nonce_validate(nonce, count)))  {
		db_trans_rollback();
		return(er);
	}

	/* FIXME: check for (unlikely) integer overflow. */
	sql = "UPDATE nonce SET count=? WHERE nonce=?";
	if (NULL == (stmt = db_prepare(sql)))
		goto err;
	else if ( ! db_bindint(stmt, 1, count + 1))
		goto err;
	else if ( ! db_bindtext(stmt, 2, nonce))
		goto err;
	else if (SQLITE_DONE != (rc = db_step(stmt)))
		goto err;

	db_finalise(&stmt);
	db_trans_commit();
	kdbg("nonce updated: %s, count %zu", nonce, count + 1);
	return(NONCE_OK);
err:
	db_finalise(&stmt);
	db_trans_rollback();
	db_exec("ROLLBACK TRANSACTION");
	kerrx("%s: %s: failure", dbname, __func__);
	return(NONCE_ERR);
}

int
db_nonce_new(char **np)
{
	static char	 nonce[NONCESZ + 1];
	int64_t		 count;
	const char	*sql;
	sqlite3_stmt	*stmt;
	int		 rc;
	size_t		 i;

	if ( ! db_trans_open(1))
		return(0);

	/* How many nonces do we have? */
	sql = "SELECT count(*) FROM nonce";
	if (NULL == (stmt = db_prepare(sql)))
		goto err;
	else if (SQLITE_ROW == (rc = db_step(stmt)))
		count = sqlite3_column_int64(stmt, 0);
	else if (SQLITE_DONE == rc)
		count = 0;
	else
		goto err;

	kdbg("nonce count: %" PRId64, count);
	db_finalise(&stmt);

	/* 
	 * If we have more than NONCEMAX nonces in the database, then
	 * cull the first 20 to make room for more.
	 */
	if (count >= NONCEMAX) {
		kdbg("culling from nonce database");
		sql = "DELETE FROM nonce WHERE id IN "
			"(SELECT id FROM nonce LIMIT 20)";
		if (NULL == (stmt = db_prepare(sql)))
			goto err;
		else if (SQLITE_DONE != db_step(stmt))
			goto err;
		db_finalise(&stmt);
	}

	/* 
	 * Generate a random nonce and insert it into the database.
	 * Let the uniqueness constraint guarantee that the nonce is
	 * actually unique within the system.
	 */
	sql = "INSERT INTO nonce (nonce) VALUES (?)";
	if (NULL == (stmt = db_prepare(sql)))
		goto err;
	for (;;) {
		for (i = 0; i < sizeof(nonce) - 1; i++)
			snprintf(nonce + i, 2, "%01X", 
				arc4random_uniform(128));
		if ( ! db_bindtext(stmt, 1, nonce))
			goto err;
		kdbg("nonce attempt: %s", nonce);
		rc = db_step_constrained(stmt);
		if (SQLITE_CONSTRAINT == rc) {
			kdbg("nonce exists: %s", nonce);
			sqlite3_reset(stmt);
			continue;
		} else if (SQLITE_DONE != rc)
			goto err;
		break;
	}

	db_finalise(&stmt);
	db_trans_commit();
	*np = nonce;
	kdbg("nonce created: %s", *np);
	return(1);
err:
	db_finalise(&stmt);
	db_trans_rollback();
	kerrx("%s: %s: failure", dbname, __func__);
	return(0);
}

int
db_collection_new(const char *url, int64_t principal)
{
	const char	*sql;
	sqlite3_stmt	*stmt;
	int		 rc;

	sql = "INSERT INTO collection "
		"(principal, url) VALUES (?,?)";
	if (NULL == (stmt = db_prepare(sql)))
		goto err;
	else if ( ! db_bindint(stmt, 1, principal))
		goto err;
	else if ( ! db_bindtext(stmt, 2, url))
		goto err;

	rc = db_step_constrained(stmt);
	db_finalise(&stmt);
	if (SQLITE_CONSTRAINT == rc) 
		return(0);
	else if (SQLITE_DONE == rc)
		return(1);
err:
	db_finalise(&stmt);
	kerrx("%s: %s: failure", dbname, __func__);
	return(-1);

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
	const char	*sql;
	sqlite3_stmt	*stmt;
	int		 rc;
	int64_t		 lastid;

	assert(directory && '\0' != directory[0]);

	if ( ! db_trans_open(0)) 
		return(0);

	sql = "INSERT INTO principal "
		"(name,hash,email) VALUES (?,?,?)";
	if (NULL == (stmt = db_prepare(sql)))
		goto err;
	else if ( ! db_bindtext(stmt, 1, name))
		goto err;
	else if ( ! db_bindtext(stmt, 2, hash))
		goto err;
	else if ( ! db_bindtext(stmt, 3, email))
		goto err;
	rc = db_step_constrained(stmt);
	if (SQLITE_DONE != rc && SQLITE_CONSTRAINT != rc)
		goto err;
	db_finalise(&stmt);

	lastid = sqlite3_last_insert_rowid(db);
	if (db_collection_new(directory, lastid) > 0) {
		db_trans_commit();
		return(1);
	}
err:
	db_finalise(&stmt);
	db_trans_rollback();
	kerrx("%s: %s: failure", dbname, __func__);
	return(-1);
}

/*
 * Change the hash of a principal to the hash set in the principal
 * object.
 * This returns zero on failure and non-zero on success.
 */
int
db_prncpl_update(const struct prncpl *p)
{
	const char	*sql;
	sqlite3_stmt	*stmt;
	int		 rc;

	sql = "UPDATE principal SET hash=? WHERE id=?";
	if (NULL == (stmt = db_prepare(sql)))
		goto err;
	else if ( ! db_bindtext(stmt, 1, p->hash))
		goto err;
	else if ( ! db_bindint(stmt, 2, p->id))
		goto err;
	else if (SQLITE_DONE != (rc = db_step(stmt)))
		goto err;
	db_finalise(&stmt);
	return(SQLITE_DONE == rc);
err:
	db_finalise(&stmt);
	kerrx("%s: %s: failure", dbname, __func__);
	return(0);
}

int
db_collection_load(struct coln **pp, const char *url, int64_t id)
{
	const char	*sql;
	sqlite3_stmt	*stmt;
	int	 	 rc;

	sql = "SELECT url,displayname,colour,description,ctag,id "
		"FROM collection WHERE principal=? AND url=?";
	if (NULL == (stmt = db_prepare(sql)))
		goto err;
	else if ( ! db_bindint(stmt, 1, id))
		goto err;
	else if ( ! db_bindtext(stmt, 2, url))
		goto err;

	if (SQLITE_ROW == (rc = db_step(stmt))) {
		*pp = calloc(1, sizeof(struct coln));
		if (NULL == *pp) {
			kerr(NULL);
			goto err;
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

		if (NULL == (*pp)->url ||
			 NULL == (*pp)->displayname ||
			 NULL == (*pp)->colour ||
			 NULL == (*pp)->description) {
			kerr(NULL);
			goto err;
		}
		db_finalise(&stmt);
		return(1);
	} else if (SQLITE_DONE == rc) {
		db_finalise(&stmt);
		return(0);
	} 
err:
	db_finalise(&stmt);
	kerrx("%s: %s: failure", dbname, __func__);
	return(-1);
}

int
db_prncpl_load(struct prncpl **pp, const char *name)
{
	struct prncpl	*p;
	const char	*sql;
	size_t		 i;
	sqlite3_stmt	*stmt;
	int		 c, rc;
	void		*vp;
	struct statfs	 sfs;

	rc = -1;
	p = *pp = calloc(1, sizeof(struct prncpl));
	if (NULL == p) {
		kerr(NULL);
		return(-1);
	}

	sql = "SELECT hash,id,email FROM principal WHERE name=?";
	if (NULL == (stmt = db_prepare(sql)))
		goto err;
	else if ( ! db_bindtext(stmt, 1, name))
		goto err;

	if (SQLITE_DONE == (c = db_step(stmt))) {
		kinfo("%s: user not found: %s", dbname, name);
		rc = 0;
		goto err;
	} else if (SQLITE_ROW != c)
		goto err;

	/* Copy out of the database... */
	p->name = strdup(name);
	p->hash = strdup((char *)sqlite3_column_text(stmt, 0));
	p->id = sqlite3_column_int(stmt, 1);
	p->email = strdup((char *)sqlite3_column_text(stmt, 2));

	db_finalise(&stmt);

	if (NULL == p->name || 
		 NULL == p->hash ||
		 NULL == p->email) {
		kerr(NULL);
		goto err;
	}

	if (-1 == statfs(dbname, &sfs)) {
		kerr("%s: statfs", dbname);
		goto err;
	}

	p->quota_used = sfs.f_blocks * sfs.f_bsize;
	p->quota_avail = sfs.f_bfree * sfs.f_bsize;

	/* Read in each collection owned by the given principal. */

	sql = "SELECT url,displayname,colour,description,ctag,id "
		"FROM collection WHERE principal=?";
	if (NULL == (stmt = db_prepare(sql)))
		goto err;
	else if ( ! db_bindint(stmt, 1, p->id))
		goto err;

	while (SQLITE_ROW == (c = db_step(stmt))) {
		vp = reallocarray
			(p->cols, 
			 p->colsz + 1,
			 sizeof(struct coln));
		if (NULL == vp) {
			kerr(NULL);
			db_finalise(&stmt);
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
			db_finalise(&stmt);
			goto err;
		}
	}

	if (SQLITE_DONE != c)
		goto err;

	db_finalise(&stmt);
	return(1);
err:
	*pp = NULL;
	prncpl_free(p);
	db_finalise(&stmt);
	kerrx("%s: %s: failure", dbname, __func__);
	return(rc);
}

int
db_collection_update(const struct coln *p)
{
	const char	*sql;
	sqlite3_stmt	*stmt;
	int		 rc;

	sql = "UPDATE collection SET displayname=?,"
		"colour=?,description=? WHERE id=?";
	if (NULL == (stmt = db_prepare(sql)))
		goto err;
	else if ( ! db_bindtext(stmt, 1, p->displayname))
		goto err;
	else if ( ! db_bindtext(stmt, 2, p->colour))
		goto err;
	else if ( ! db_bindtext(stmt, 3, p->description))
		goto err;
	else if ( ! db_bindint(stmt, 4, p->id))
		goto err;
	else if (SQLITE_DONE != (rc = db_step(stmt)))
		goto err;

	db_finalise(&stmt);
	if (db_collection_ctag(p->id))
		return(1);
err:
	db_finalise(&stmt);
	kerrx("%s: %s: failure", dbname, __func__);
	return(0);
}

int
db_collection_resources(void (*fp)(const struct res *, void *), 
	int64_t colid, void *arg)
{
	const char	*sql;	
	sqlite3_stmt	*stmt;
	int		 rc;
	struct res	 p;

	sql = "SELECT data,etag,url,id,collection "
		"FROM resource WHERE collection=?";

	if (NULL == (stmt = db_prepare(sql)))
		goto err;
	else if ( ! db_bindint(stmt, 1, colid))
		goto err;

	while (SQLITE_ROW == (rc = db_step(stmt))) {
		memset(&p, 0, sizeof(struct res));
		p.data = (char *)sqlite3_column_text(stmt, 0);
		p.etag = sqlite3_column_int64(stmt, 1);
		p.url = (char *)sqlite3_column_text(stmt, 2);
		p.id = sqlite3_column_int64(stmt, 3);
		p.collection = sqlite3_column_int64(stmt, 4);
		p.ical = ical_parse(NULL, p.data, strlen(p.data));
		if (NULL == p.ical)
			goto err;
		(*fp)(&p, arg);
		ical_free(p.ical);
	}
	if (SQLITE_DONE != rc)
		goto err;
	db_finalise(&stmt);
	return(1);
err:
	db_finalise(&stmt);
	kerrx("%s: %s: failure", dbname, __func__);
	return(0);
}

int
db_collection_remove(int64_t colid)
{
	const char	*sql;	
	sqlite3_stmt	*stmt;

	sql = "DELETE FROM collection WHERE id=?";
	if (NULL == (stmt = db_prepare(sql)))
		goto err;
	else if ( ! db_bindint(stmt, 1, colid))
		goto err;
	else if (SQLITE_DONE != db_step(stmt))
		goto err;

	db_finalise(&stmt);
	return(1);
err:
	db_finalise(&stmt);
	kerrx("%s: %s: failure", dbname, __func__);
	return(0);
}

/*
 * Delete a resource from the collection and update the ctag.
 * Only do this if the resource matches the given tag.
 * Returns zero on failure, non-zero on success.
 */
int
db_resource_delete(const char *url, int64_t tag, int64_t colid)
{
	const char	*sql;	
	sqlite3_stmt	*stmt;
	int		 rc;

	if ( ! db_trans_open(1)) 
		return(0);

	sql = "SELECT * FROM resource WHERE "
		"url=? AND collection=? AND etag=?";
	if (NULL == (stmt = db_prepare(sql)))
		goto err;
	else if ( ! db_bindtext(stmt, 1, url))
		goto err;
	else if ( ! db_bindint(stmt, 2, colid))
		goto err;
	else if ( ! db_bindint(stmt, 3, tag))
		goto err;
	
	rc = db_step(stmt);
	db_finalise(&stmt);

	if (SQLITE_ROW != rc) {
		db_trans_rollback();
		return(0);
	} else if (SQLITE_DONE == rc)
		goto err;

	sql = "DELETE FROM resource WHERE "
		"url=? AND collection=? AND etag=?";
	if (NULL == (stmt = db_prepare(sql)))
		goto err;
	else if ( ! db_bindtext(stmt, 1, url))
		goto err;
	else if ( ! db_bindint(stmt, 2, colid))
		goto err;
	else if ( ! db_bindint(stmt, 3, tag))
		goto err;
	else if (SQLITE_DONE != db_step(stmt))
		goto err;

	db_finalise(&stmt);
	if (db_collection_ctag(colid)) {
		db_trans_commit();
		return(1);
	}
err:
	db_finalise(&stmt);
	db_trans_rollback();
	kerrx("%s: %s: failure", dbname, __func__);
	return(0);
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
	const char	*sql;	
	sqlite3_stmt	*stmt;

	sql = "DELETE FROM resource WHERE "
		"url=? AND collection=?";
	if (NULL == (stmt = db_prepare(sql)))
		goto err;
	else if ( ! db_bindtext(stmt, 1, url))
		goto err;
	else if ( ! db_bindint(stmt, 2, colid))
		goto err;
	else if (SQLITE_DONE != db_step(stmt))
		goto err;

	db_finalise(&stmt);
	if (db_collection_ctag(colid))
		return(1);
err:
	db_finalise(&stmt);
	kerrx("%s: %s: failure", dbname, __func__);
	return(0);
}

/*
 * Create a new resource at "url" in "colid".
 * This returns <0 if a system error occurs, 0 if a resource by that
 * name already exists, or >0 on success.
 */
int
db_resource_new(const char *data, const char *url, int64_t colid)
{
	const char	*sql;	
	sqlite3_stmt	*stmt;
	int		 rc;

	sql = "INSERT INTO resource "
		"(data,url,collection) VALUES (?,?,?)";
	if (NULL == (stmt = db_prepare(sql)))
		goto err;
	else if ( ! db_bindtext(stmt, 1, data))
		goto err;
	else if ( ! db_bindtext(stmt, 2, url))
		goto err;
	else if ( ! db_bindint(stmt, 3, colid))
		goto err;

	rc = db_step_constrained(stmt);
	db_finalise(&stmt);

	if (SQLITE_CONSTRAINT == rc) 
		return(0);
	else if (SQLITE_DONE != rc)
		goto err;

	if (db_collection_ctag(colid))
		return(1);
err:
	db_finalise(&stmt);
	kerrx("%s: %s: failure", dbname, __func__);
	return(-1);
}

/*
 * Update the resource at "url" in collection "colid" with new data,
 * increasing its etag and the colection's ctag.
 * Make sure that the existing etag is set to "digest".
 * Returns <0 on system error, 0 if the resource couldn't be found, or
 * >0 on success.
 */
int
db_resource_update(const char *data, const char *url, 
	int64_t digest, int64_t colid)
{
	sqlite3_stmt	*stmt;
	struct res	*res;
	int		 rc;
	int64_t		 id;
	const char	*sql;

	if ( ! db_trans_open(1)) 
		return(-1);

	stmt = NULL;
	res = NULL;

	if (0 == (rc = db_resource_load(&res, url, colid))) {
		db_trans_rollback();
		return(0);
	} else if (rc < 0)
		goto err;

	if (res->etag != digest) {
		db_trans_rollback();
		res_free(res);
		return(0);
	}

	id = res->id;
	res_free(res);

	sql = "UPDATE resource SET data=?,etag=etag+1 WHERE id=?";
	if (NULL == (stmt = db_prepare(sql)))
		goto err;
	else if ( ! db_bindtext(stmt, 1, data))
		goto err;
	else if ( ! db_bindint(stmt, 2, id))
		goto err;
	else if (SQLITE_DONE != db_step(stmt))
		goto err;

	db_finalise(&stmt);

	if (db_collection_ctag(colid)) {
		db_trans_commit();
		return(1);
	}
err:
	res_free(res);
	db_finalise(&stmt);
	db_trans_rollback();
	kerrx("%s: %s: failure", dbname, __func__);
	return(-1);
}

/*
 * Load the resource named "url" within collection "colid".
 * Returns zero if the resource was not found, <0 if the retrieval
 * failed in some way, or >1 if the retrieval was successful.
 * On success, "pp" is set to the resource.
 */
int
db_resource_load(struct res **pp, const char *url, int64_t colid)
{
	const char	*sql;	
	sqlite3_stmt	*stmt;
	int		 rc;

	*pp = NULL;
	sql = "SELECT data,etag,url,id,collection FROM resource "
		"WHERE collection=? AND url=?";
	if (NULL == (stmt = db_prepare(sql)))
		goto err;
	else if ( ! db_bindint(stmt, 1, colid))
		goto err;
	else if ( ! db_bindtext(stmt, 2, url))
		goto err;

	rc = db_step(stmt);
	if (SQLITE_ROW == rc) {
		*pp = calloc(1, sizeof(struct res));
		if (NULL == *pp) {
			kerr(NULL);
			goto err;
		}
		(*pp)->data = strdup
			((char *)sqlite3_column_text(stmt, 0));
		(*pp)->etag = sqlite3_column_int64(stmt, 1);
		(*pp)->url = strdup
			((char *)sqlite3_column_text(stmt, 2));
		(*pp)->id = sqlite3_column_int64(stmt, 3);
		(*pp)->collection = sqlite3_column_int64(stmt, 4);
		if (NULL == (*pp)->data || NULL == (*pp)->url) {
			kerr(NULL);
			goto err;
		}
		/* Parse the full iCalendar. */
		(*pp)->ical = ical_parse(NULL, 
			(*pp)->data, strlen((*pp)->data));
		if (NULL == (*pp)->ical) {
			kerrx("%s: failed to parse ical", url);
			goto err;
		}
		db_finalise(&stmt);
		return(1);
	} else if (SQLITE_DONE == rc) {
		db_finalise(&stmt);
		return(0);
	}
err:
	if (NULL != *pp) {
		free((*pp)->data);
		free((*pp)->url);
		free(*pp);
	}
	db_finalise(&stmt);
	kerrx("%s: %s: failure", dbname, __func__);
	return(-1);
}

/*
 * This checks the ownership of a database file.
 * If the file is newly-created, it creates the database schema and
 * initialises the ownership to the given identifier.
 */
int
db_owner_check_or_set(int64_t id)
{
	sqlite3_stmt	*stmt;
	const char	*sql;
	int64_t		 oid;

	sql = "SELECT owneruid FROM database";
	if (NULL != (stmt = db_prepare(sql))) {
		if (SQLITE_ROW == db_step(stmt)) {
			oid = sqlite3_column_int64(stmt, 0);
			db_finalise(&stmt);
			if (0 == id && oid != id)
				kinfo("root overriding database "
					"owner: %" PRId64, oid);
			return(0 == id || oid == id);
		}
		goto err;
	}

	/*
	 * Assume that the database has not been initialised and try to
	 * do so here with a hardcoded schema.
	 */
	sql = "\
CREATE TABLE resource (\n\
	collection INTEGER NOT NULL,\n\
	url TEXT NOT NULL,\n\
	etag INT NOT NULL DEFAULT(1),\n\
	data TEXT NOT NULL,\n\
	id INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL,\n\
	unique (url,collection),\n\
	FOREIGN KEY (collection) REFERENCES collection(id) ON DELETE CASCADE\n\
);\n\
CREATE TABLE collection (\n\
	principal INTEGER REFERENCES principal(id) NOT NULL,\n\
	url TEXT NOT NULL,\n\
	displayname TEXT NOT NULL DEFAULT(\'Calendar\'),\n\
	colour TEXT NOT NULL DEFAULT(\'#B90E28FF\'),\n\
	description TEXT NOT NULL DEFAULT(\'\'),\n\
	ctag INT NOT NULL DEFAULT(1),\n\
	id INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL,\n\
	unique (url,principal)\n\
);\n\
CREATE TABLE nonce (\n\
	nonce TEXT NOT NULL,\n\
	count INT NOT NULL DEFAULT(0),\n\
	id INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL,\n\
	unique (nonce)\n\
);\n\
CREATE TABLE principal (\n\
	name TEXT NOT NULL,\n\
	hash TEXT NOT NULL,\n\
	email TEXT NOT NULL,\n\
	id INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL,\n\
	unique (name)\n\
);\n\
CREATE TABLE database (\n\
	owneruid INTEGER NOT NULL\n\
);";
	if (SQLITE_OK != db_exec(sql))
		goto err;

	/* Finally, insert our database record. */
	sql = "INSERT INTO database (owneruid) VALUES (?)";
	if (NULL == (stmt = db_prepare(sql)))
		goto err;
	else if ( ! db_bindint(stmt, 1, id))
		goto err;
	else if (SQLITE_DONE != db_step(stmt)) 
		goto err;

	db_finalise(&stmt);
	return(1);
err:
	db_finalise(&stmt);
	kerrx("%s: %s: failure", dbname, __func__);
	return(-1);
}
