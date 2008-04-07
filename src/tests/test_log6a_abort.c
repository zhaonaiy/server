/* -*- mode: C; c-basic-offset: 4 -*- */
#ident "Copyright (c) 2007 Tokutek Inc.  All rights reserved."

/* Like test_log6 except abort.
 * And abort some stuff, but not others (unlike test_log6_abort which aborts everything) */

#include <assert.h>
#include <db.h>
#include <stdlib.h>
#include <search.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <string.h>

#include "test.h"

#ifndef DB_DELETE_ANY
#define DB_DELETE_ANY 0 
#endif

// ENVDIR is defined in the Makefile

// How many iterations are we going to do insertions and deletions.  This is a bound to the number of distinct keys in the DB.
#define N 10000

int n_keys_mentioned=0;
int random_keys_mentioned[N];

DB *pending_i, *pending_d, *committed;

void insert_pending(int key, int val, DB_TXN *bookx) {
    DBT keyd,datad;
    pending_i->put(pending_i, bookx,
		   dbt_init(&keyd, &key, sizeof(key)),
		   dbt_init(&datad, &val, sizeof(val)),
		   0);
    pending_d->del(pending_d, bookx,
		   dbt_init(&keyd, &key, sizeof(key)),
		   0);
}

static void put_a_random_item (DB *db, DB_TXN *tid, int i, DB_TXN *bookx) {
    char hello[30], there[30];
    DBT key,data;
    int rand = myrandom();
    random_keys_mentioned[n_keys_mentioned++] = rand;
    insert_pending(rand, i, bookx);
    //printf("Insert %u\n", rand);
    snprintf(hello, sizeof(hello), "hello%d.%d", rand, i);
    snprintf(there, sizeof(hello), "there%d", i);
    memset(&key, 0, sizeof(key));
    memset(&data, 0, sizeof(data));
    key.data  = hello; key.size=strlen(hello)+1;
    data.data = there; data.size=strlen(there)+1;
    int r=db->put(db, tid, &key, &data, 0);
    if (r!=0) printf("%s:%d i=%d r=%d (%s)\n", __FILE__, __LINE__, i, r, strerror(r));
    assert(r==0);
}

static void delete_a_random_item (DB *db, DB_TXN *tid, DB_TXN *bookx) { 
    if (n_keys_mentioned==0) return;
    int ridx = myrandom()%n_keys_mentioned;
    int rand = random_keys_mentioned[ridx];
    DBT keyd;
    DBT vald;
    //printf("Delete %u\n", rand);
    dbt_init(&keyd, &rand, sizeof(rand));
    dbt_init(&vald, &rand, sizeof(rand));
    pending_i->del(pending_i, bookx, &keyd, 0);
    pending_i->put(pending_d, bookx, &keyd, &vald, 0);
    db->del(db, tid, &keyd, DB_DELETE_ANY);
}

static void commit_items (DB_ENV *env, int i) {
    //printf("commit_items %d\n", i);
    DB_TXN *txn;
    int r=env->txn_begin(env, 0, &txn, 0); assert(r==0);
    DBC  *cursor;
    r = pending_i->cursor(pending_i, txn, &cursor, 0); assert(r==0);
    DBT k,v;
    memset(&k,0,sizeof(k));
    memset(&v,0,sizeof(v));
    while (cursor->c_get(cursor, &k, &v, DB_FIRST)==0) {
	assert(k.size==4);
	assert(v.size==4);
	int ki=*(int*)k.data;
	int vi=*(int*)v.data;
	//printf(" put %u %u\n", ki, vi);
	r=committed->put(committed, txn, dbt_init(&k, &ki, sizeof(ki)), dbt_init(&v, &vi, sizeof(vi)), 0);
	assert(r==0);
	r=pending_i->del(pending_i, txn, &k, 0);
	assert(r==0);
    }
    r=cursor->c_close(cursor);
    assert(r==0);

    r = pending_d->cursor(pending_d, txn, &cursor, 0); assert(r==0);
    memset(&k,0,sizeof(k));
    memset(&v,0,sizeof(v));
    while (cursor->c_get(cursor, &k, &v, DB_FIRST)==0) {
	assert(k.size==4);
	assert(v.size==4);
	int ki=*(int*)k.data;
	int vi=*(int*)v.data;
	assert(ki==vi);
	//printf(" del %u\n", ki);
	committed->del(committed, txn, dbt_init(&k, &ki, sizeof(ki)), DB_AUTO_COMMIT);
	// ignore result from that del
	r=pending_d->del(pending_d, txn, &k, 0);
	assert(r==0);
    }
    r=cursor->c_close(cursor);
    assert(r==0);
    r=txn->commit(txn, 0); assert(r==0);
}

static void abort_items (DB_ENV *env) {
    DB_TXN *txn;
    int r=env->txn_begin(env, 0, &txn, 0); assert(r==0);
    //printf("abort_items\n");
    DBC  *cursor;
    r = pending_i->cursor(pending_i, txn, &cursor, 0); assert(r==0);
    DBT k,v;
    memset(&k,0,sizeof(k));
    memset(&v,0,sizeof(v));
    while (cursor->c_get(cursor, &k, &v, DB_FIRST)==0) {
	assert(k.size==4);
	assert(v.size==4);
	int ki=*(int*)k.data;
	//printf("Deleting %u\n", ki);
	r=pending_i->del(pending_i, txn, dbt_init(&k, &ki, sizeof(ki)), 0);
	assert(r==0);
    }
    r=cursor->c_close(cursor);
    assert(r==0);

    r = pending_d->cursor(pending_d, txn, &cursor, 0); assert(r==0);
    memset(&k,0,sizeof(k));
    memset(&v,0,sizeof(v));
    while (cursor->c_get(cursor, &k, &v, DB_FIRST)==0) {
	assert(k.size==4);
	assert(v.size==4);
	int ki=*(int*)k.data;
	r=pending_d->del(pending_d, txn, dbt_init(&k, &ki, sizeof(ki)), 0);
	assert(r==0);
    }
    r=cursor->c_close(cursor);
    assert(r==0);
    r=txn->commit(txn, 0); assert(r==0);
}

static void verify_items (DB_ENV *env, DB *db) {
    DB_TXN *txn;
    int r=env->txn_begin(env, 0, &txn, 0); assert(r==0);
    DBC *cursor;
    DBT k,v;
    memset(&k,0,sizeof(k));
    memset(&v,0,sizeof(v));

#if 0
    r=db->cursor(db, txn, &cursor, 0);
    assert(r==0);
    while (cursor->c_get(cursor, &k, &v, DB_NEXT)==0) {
    }
    r=cursor->c_close(cursor);
    assert(r==0);
#endif

    r = committed->cursor(committed, txn, &cursor, 0);
    assert(r==0);
    while (cursor->c_get(cursor, &k, &v, DB_NEXT)==0) {
	int kv=*(int*)k.data;
	int dv=*(int*)v.data;
	DBT k2,v2;
	memset(&k2, 0, sizeof(k2));
	memset(&v2, 0, sizeof(v2));
	char hello[30], there[30];
	snprintf(hello, sizeof(hello), "hello%d.%d", kv, dv);
	snprintf(there, sizeof(hello), "there%d", dv);
	k2.data  = hello; k2.size=strlen(hello)+1;
	printf("kv=%d dv=%d\n", kv, dv);
	r=db->get(db, txn,  &k2, &v2, 0);
	assert(r==0);
	assert(strcmp(v2.data, there)==0);
    }
    r=cursor->c_close(cursor);
    assert(r==0);

    r=txn->commit(txn, 0); assert(r==0);
}

static void make_db (void) {
    DB_ENV *env;
    DB *db;
    DB_TXN *tid, *bookx;
    int r;
    int i;

    system("rm -rf " ENVDIR);
    r=mkdir(ENVDIR, 0777);       assert(r==0);
    r=db_env_create(&env, 0); assert(r==0);
    env->set_errfile(env, stderr);
    r=env->open(env, ENVDIR, DB_INIT_LOCK|DB_INIT_LOG|DB_INIT_MPOOL|DB_INIT_TXN|DB_CREATE|DB_PRIVATE, 0777); CKERR(r);
    r=db_create(&db, env, 0); CKERR(r);
    r=db_create(&pending_i, env, 0); CKERR(r);
    r=db_create(&pending_d, env, 0); CKERR(r);
    r=db_create(&committed, env, 0); CKERR(r);
    r=env->txn_begin(env, 0, &tid, 0); assert(r==0);
    r=db->open(db, tid, "foo.db", 0, DB_BTREE, DB_CREATE, 0777); CKERR(r);
    r=pending_i->open(pending_i, tid, "pending_i.db", 0, DB_BTREE, DB_CREATE, 0777); CKERR(r);
    r=pending_d->open(pending_d, tid, "pending_d.db", 0, DB_BTREE, DB_CREATE, 0777); CKERR(r);
    r=committed->open(committed, tid, "committed.db", 0, DB_BTREE, DB_CREATE, 0777); CKERR(r);
    r=tid->commit(tid, 0);    assert(r==0);
    r=env->txn_begin(env, 0, &tid, 0); assert(r==0);
    r=env->txn_begin(env, 0, &bookx, 0); assert(r==0);

    for (i=0; i<N; i++) {
	int rand = myrandom();
	if (i%10000==0) printf(".");
	if (rand%100==0) {
	    r=tid->abort(tid); assert(r==0);
	    r=bookx->commit(bookx, 0); assert(r==0);
	    r=env->txn_begin(env, 0, &bookx, 0); assert(r==0);
	    abort_items(env);
	    r=env->txn_begin(env, 0, &tid, 0); assert(r==0);
	} else if (rand%1000==1) {
	    r=tid->commit(tid, 0); assert(r==0);
	    r=bookx->commit(bookx, 0); assert(r==0);
	    r=env->txn_begin(env, 0, &bookx, 0); assert(r==0);
	    commit_items(env, i);
	    r=env->txn_begin(env, 0, &tid, 0); assert(r==0);
	} else if (rand%3==0) {
	    delete_a_random_item(db, tid, bookx);
	} else {
	    put_a_random_item(db, tid, i, bookx);
	}
    }
    r=tid->commit(tid, 0); assert(r==0);
    r=bookx->commit(bookx, 0); assert(r==0);
    commit_items(env, i);
    verify_items(env, db);

    r=pending_i->close(pending_i, 0); assert(r==0);
    r=pending_d->close(pending_d, 0); assert(r==0);
    r=committed->close(committed, 0); assert(r==0);
    r=db->close(db, 0);       assert(r==0);
    r=env->close(env, 0);     assert(r==0);
}

int main (int argc __attribute__((__unused__)), char *argv[] __attribute__((__unused__))) {
    make_db();
    return 0;
}
