/**
 *
 * Copyright (C) 2015 OpenSIPS Foundation
 *
 * This file is part of opensips, a free SIP server.
 *
 * opensips is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version
 *
 * opensips is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * History
 * -------
 *  2015-02-18  initial version (Ionut Ionita)
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>

#include "../../dprint.h"
#include "../../db/db_query.h"
#include "../../db/db_async.h"
#include "../../db/db_ut.h"
#include "../../db/db_insertq.h"
#include "../../db/db_id.h"
#include "../../mem/mem.h"
#include "my_con.h"
#include "db_sqlite.h"

extern struct db_sqlite_extension_list *extension_list;

#define SQLITE_ID "sqlite:/"

int db_sqlite_connect(struct my_con* ptr)
{
	sqlite3* con;
	char* url;
	char* errmsg;
	struct db_sqlite_extension_list *iter;

	/* if connection already in use, close it first*/
	if (ptr->init)
		sqlite3_close_v2(ptr->con);

	ptr->init = 1;

	url = ptr->id->url;
	/* parsing has already been done we know that we've got "sqlite:/" there*/
	url += sizeof(SQLITE_ID) - 1;

	if (sqlite3_open(url, &con) != SQLITE_OK) {
		LM_ERR("Can't open database: %s\n", sqlite3_errmsg((sqlite3*)ptr->con));
		return -1;
	}

	/* trying to load extensions */
	if (extension_list) {
		if (sqlite3_enable_load_extension(con, 1)) {
			LM_ERR("failed to enable extension loading\n");
			return -1;
		}

		iter=extension_list;
		for (iter=extension_list; iter; iter=iter->next) {
			if (sqlite3_load_extension(con, iter->ldpath,
						iter->entry_point, &errmsg)) {
				LM_ERR("failed to load!"
						"Extension [%s]! Entry point [%s]!"
						"Errmsg [%s]!\n",
						iter->ldpath, iter->entry_point,
						errmsg);
				goto out_free;
			}
			LM_INFO("Extension [%s] loaded!\n", iter->ldpath);
		}

		if (sqlite3_enable_load_extension(con, 0)) {
			LM_ERR("failed to enable extension loading\n");
			return -1;
		}
	}



	ptr->con = con;

	return 0;

out_free:
	while (extension_list) {
		iter=extension_list;
		extension_list=extension_list->next;
		pkg_free(iter);
	}
	return -1;
}

/**
 * Create a new connection structure,
 * open the sqlite connection and set reference count to 1
 */
struct my_con* db_sqlite_new_connection(const struct db_id* id)
{

	struct my_con* ptr;

	if (!id) {
		LM_ERR("invalid parameter value\n");
		return 0;
	}

	ptr = (struct my_con*)pkg_malloc(sizeof(struct my_con));
	if (!ptr) {
		LM_ERR("no private memory left\n");
		return 0;
	}

	memset(ptr, 0, sizeof(struct my_con));
	ptr->ref = 1;

	ptr->id = (struct db_id*)id;

	if (db_sqlite_connect(ptr)!=0) {
		LM_ERR("initial connect failed\n");
		goto err;
	}
	return ptr;
err:
	if (ptr && ptr->con) pkg_free(ptr->con);
	if (ptr) pkg_free(ptr);
	return 0;
}

/*
 *	Actually free prep_stmt structure
*/
static void db_sqlite_free_pq(struct prep_stmt *pq_ptr)
{
	struct my_stmt_ctx *ctx;
	struct my_stmt_ctx *ctx2;

	if ( pq_ptr == NULL )
		return;

	for(ctx=pq_ptr->stmts ; ctx ; ) {
		ctx2 = ctx;
		ctx = ctx->next;
		if (ctx2->stmt)
			sqlite3_finalize(ctx2->stmt);
		pkg_free(ctx2);
	}


	/* free in part and the struct */
	pkg_free(pq_ptr);
}


/*
**	Free all allocated prep_stmt structures
 */
void db_sqlite_free_stmt_list(struct prep_stmt *head)
{
	struct prep_stmt *pq_ptr;

	while ( head!= NULL ) {
		pq_ptr = head;
		head = head->next;
		db_sqlite_free_pq(pq_ptr);
	}
}

/**
 * Close the connection and release memory
 */
void db_sqlite_free_connection(struct pool_con* con)
{
	if (!con) return;

	struct my_con * _c;
	_c = (struct my_con*) con;
	struct db_sqlite_extension_list *foo=NULL;

	while (extension_list) {
		foo=extension_list;
		extension_list=extension_list->next;
		pkg_free(foo);
	}

	if (_c->ps_list) db_sqlite_free_stmt_list(_c->ps_list);
	if (_c->id) free_db_id(_c->id);
	if (_c->con) {
		sqlite3_close(_c->con);
	}
	pkg_free(_c);
}
