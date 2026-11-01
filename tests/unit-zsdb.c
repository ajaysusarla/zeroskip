/*
 * zeroskip
 *
 * zeroskip is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 *
 * Copyright (c) 2018 Partha Susarla <mail@spartha.org>
 */

#include <libzeroskip/zeroskip.h>
#include <libzeroskip/macros.h>
#include <libzeroskip/log.h>
#include <libzeroskip/util.h>

#include <assert.h>
#include <error.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <check.h>

#if CHECK_MINOR_VERSION < 11
#include <assert.h>
#define ck_assert_mem_eq(a,b,c) assert(0 == memcmp(a,b,c))
#endif

Suite *zsdb_suite(void);

struct kvrecs {
        const unsigned char *k;
        size_t klen;
        const unsigned char *v;
        size_t vlen;
};

static struct kvrecs kvrecsgen[] = {
        { (const unsigned char *)"123", 3, (const unsigned char *)"456", 3 },
        { (const unsigned char *)"foo", 3, (const unsigned char *)"bar", 3 },
        { (const unsigned char *)"abc", 3, (const unsigned char *)"def", 3 },
        { (const unsigned char *)"abc.name", 8, (const unsigned char *)"foo", 3 },
        { (const unsigned char *)"1233", 4, (const unsigned char *)"456", 3 },
        { (const unsigned char *)"abc.place", 9, (const unsigned char *)"foo", 3 },
        { (const unsigned char *)"1232", 4, (const unsigned char *)"456", 3 },
        { (const unsigned char *)"abc.animal", 10, (const unsigned char *)"foo", 3 },
        { (const unsigned char *)"Apple", 5, (const unsigned char *)"iPhone7s", 8 },
        { (const unsigned char *)"abc.thing", 9, (const unsigned char *)"foo", 3 },
        { (const unsigned char *)"12311", 5, (const unsigned char *)"456", 3 },
        { (const unsigned char *)"blackberry", 10, (const unsigned char *)"BB10", 3 },
        { (const unsigned char *)"1231", 4, (const unsigned char *)"456", 3 },
        { (const unsigned char *)"nokia", 5, (const unsigned char *)"meego", 5 },
};

static struct kvrecs kvrecs2[] = {
        { (const unsigned char *)"Australia.Sydney", 4, (const unsigned char *)"2000", 3 },
        { (const unsigned char *)"Australia.Melbourne", 5, (const unsigned char *)"3000", 5 },
};

static struct kvrecs kvrecsdel[] = {
        { (const unsigned char *)"buzzes", 6, (const unsigned char *)"afro timur funky cents hewitt", 29 },
        { (const unsigned char *)"galas", 5, (const unsigned char *)"assad goering flemish brynner heshvan", 37 },
        { (const unsigned char *)"bathes", 6, (const unsigned char *)"flax corm naipaul enable herrera fating", 39 },
};

static struct kvrecs kvmultiopen[] = {
        { (const unsigned char *)"mustache", 8, (const unsigned char *)"blog lomo", 9 },
        { (const unsigned char *)"cred", 4, (const unsigned char *)"beard ethical", 13 },
        { (const unsigned char *)"leggings", 8, (const unsigned char *)"tumblr salvia", 13 },
};

#define K0 "affect"
#define K0U "bother"
#define K1 "carib"
#define K2 "cubist"
#define K3 "eulogy"
#define K4 "kidding"
#define K4A "llama"
#define K5 "monkey"
#define K6 "notice"
#define K7 "octopus"
#define K7D "opossum"
#define K7A "possum"
#define K7B "quine"
#define K8 "rooster"

static struct kvrecs kvforeachchanges[] = {
        { (const unsigned char *)K1, 5, (const unsigned char *)"delays maj bullish packard ronald", 33 },
        { (const unsigned char *)K2, 6, (const unsigned char *)"bobby tswana cu albumin created", 31 },
        { (const unsigned char *)K3, 6, (const unsigned char *)"aleut stoic muscovy adonis moe docent", 37 },
        { (const unsigned char *)K4, 7, (const unsigned char *)"curry deterrent drove raising hiring", 36 },
        { (const unsigned char *)K5, 6, (const unsigned char *)"joining keeper angle burden buffer", 34 },
        { (const unsigned char *)K6, 6, (const unsigned char *)"annoying push security plenty ending", 36 },
};

struct zsdb *db = NULL;
static char *basedir = NULL;

static char *get_basedir(void)
{
        const char *tmpdir;
        char path[PATH_MAX];

        tmpdir = getenv("TMPDIR");
        if (!tmpdir)
                tmpdir = "/tmp";

        snprintf(path, sizeof(path), "%s/zsdb-test.%d", tmpdir, getpid());

        #if 0
        r = xmkdir(path, 0700);
        if (r && errno != EEXIST) {
                perror(path);
                return NULL;
        }
        #endif

        return xstrdup(path);
}

static void setup(void)
{
        int ret;

        basedir = get_basedir();

        ret = zsdb_init(&db, NULL, NULL);
        ck_assert_int_eq(ret, ZS_OK);

        ret = zsdb_open(db, basedir, MODE_CREATE);
        ck_assert_int_eq(ret, ZS_OK);
}

static void teardown(void)
{
        int ret;
        ret = zsdb_close(db);
        ck_assert_int_eq(ret, ZS_OK);
        zsdb_final(&db);
        recursive_rm(basedir);
        free(basedir);
}

static int record_count = 0;

static int count_fe_p(void *data _unused_,
                      const unsigned char *key _unused_, size_t keylen _unused_,
                      const unsigned char *val _unused_, size_t vallen _unused_)
{
        record_count++;

        return 0;
}

START_TEST(test_abort_transaction)
{
        struct zsdb_txn *txn;
        size_t i;
        int ret;

        txn = NULL;
        record_count = 0;

        /* Begin transaction */
        ret = zsdb_transaction_begin(db, &txn);
        ck_assert_int_eq(ret, ZS_OK);

        /* Acquire write lock */
        zsdb_write_lock_acquire(db, 0);

        for (i = 0; i < ARRAY_SIZE(kvrecsgen); i++) {
                ret = zsdb_add(db, kvrecsgen[i].k, kvrecsgen[i].klen,
                               kvrecsgen[i].v, kvrecsgen[i].vlen, &txn);
                ck_assert_int_eq(ret, ZS_OK);
        }

        /* Commit the add records transaction */
        zsdb_commit(db, &txn);
        ck_assert_int_eq(ret, ZS_OK);

        /* Release write lock */
        zsdb_write_lock_release(db);

        zsdb_transaction_end(&txn);

        /* Count records */
        ret = zsdb_foreach(db, NULL, 0, count_fe_p, NULL, NULL, &txn);
        ck_assert_int_eq(ret, ZS_OK);
        ck_assert_int_eq(record_count, ARRAY_SIZE(kvrecsgen));

        /* Add more records, but don't commit  */
        /* Begin transaction */
        ret = zsdb_transaction_begin(db, &txn);
        ck_assert_int_eq(ret, ZS_OK);

        /* Acquire write lock */
        zsdb_write_lock_acquire(db, 0);

        for (i = 0; i < ARRAY_SIZE(kvrecs2); i++) {
                ret = zsdb_add(db, kvrecs2[i].k, kvrecs2[i].klen,
                               kvrecs2[i].v, kvrecs2[i].vlen, &txn);
                ck_assert_int_eq(ret, ZS_OK);
        }

        /* Release write lock */
        zsdb_write_lock_release(db);

        /* End transaction */
        zsdb_transaction_end(&txn);

        /* Count records, again */
        record_count = 0;
        ret = zsdb_foreach(db, NULL, 0, count_fe_p, NULL, NULL, &txn);
        ck_assert_int_eq(ret, ZS_OK);
        ck_assert_int_eq(record_count, (ARRAY_SIZE(kvrecsgen) + ARRAY_SIZE(kvrecs2)));

        /* Abort transaction */
        ret = zsdb_abort(db, &txn);
        ck_assert_int_eq(ret, ZS_OK);

        /* Now close the DB and open again, and the db should contain only
         * records from `kvrecsgen`.
         */
        ret = zsdb_close(db);
        ck_assert_int_eq(ret, ZS_OK);
        zsdb_final(&db);

        ret = zsdb_init(&db, NULL, NULL);
        ck_assert_int_eq(ret, ZS_OK);

        ret = zsdb_open(db, basedir, MODE_RDWR);
        ck_assert_int_eq(ret, ZS_OK);

        /* Count records */
        record_count = 0;
        ret = zsdb_foreach(db, NULL, 0, count_fe_p, NULL, NULL, &txn);
        ck_assert_int_eq(ret, ZS_OK);
        ck_assert_int_eq(record_count, ARRAY_SIZE(kvrecsgen));
}
END_TEST

START_TEST(test_delete)
{
        struct zsdb_txn *txn;
        size_t i;
        int ret;
        const unsigned char *value = NULL;
        size_t vallen = 0;

        txn = NULL;

        /** ADD RECORDS **/
        /* Begin transaction */
        ret = zsdb_transaction_begin(db, &txn);
        ck_assert_int_eq(ret, ZS_OK);

        /* Acquire write lock */
        zsdb_write_lock_acquire(db, 0);

        for (i = 0; i < ARRAY_SIZE(kvrecsdel); i++) {
                ret = zsdb_add(db, kvrecsdel[i].k, kvrecsdel[i].klen,
                               kvrecsdel[i].v, kvrecsdel[i].vlen, &txn);
                ck_assert_int_eq(ret, ZS_OK);
        }

        /* Commit the add records transaction */
        zsdb_commit(db, &txn);
        ck_assert_int_eq(ret, ZS_OK);

        /* Release write lock */
        zsdb_write_lock_release(db);


        /** DELETE RECORDS **/
        /* Acquire write lock */
        zsdb_write_lock_acquire(db, 0);

        /* Delete a record */
        ret = zsdb_remove(db, kvrecsdel[1].k, kvrecsdel[1].klen, &txn);
        ck_assert_int_eq(ret, ZS_OK);

        /* Release write lock */
        zsdb_write_lock_release(db);

        /* Fetch key2  - should not be found*/
        ret = zsdb_fetch(db, kvrecsdel[1].k, kvrecsdel[1].klen,
                         &value, &vallen, &txn);
        ck_assert_int_eq(ret, ZS_NOTFOUND);


        /* Fetch key1 and key3, they should be found */
        ret = zsdb_fetch(db, kvrecsdel[0].k, kvrecsdel[0].klen,
                         &value, &vallen, &txn);
        ck_assert_int_eq(ret, ZS_OK);

        ret = zsdb_fetch(db, kvrecsdel[2].k, kvrecsdel[2].klen,
                         &value, &vallen, &txn);
        ck_assert_int_eq(ret, ZS_OK);

        /* Commit */
        zsdb_commit(db, &txn);
        ck_assert_int_eq(ret, ZS_OK);


        /* After committing, key2  - should not be found
         * key and key3 should be found.
         */
        ret = zsdb_fetch(db, kvrecsdel[1].k, kvrecsdel[1].klen,
                         &value, &vallen, &txn);
        ck_assert_int_eq(ret, ZS_NOTFOUND);

        ret = zsdb_fetch(db, kvrecsdel[0].k, kvrecsdel[0].klen,
                         &value, &vallen, &txn);
        ck_assert_int_eq(ret, ZS_OK);

        ret = zsdb_fetch(db, kvrecsdel[2].k, kvrecsdel[2].klen,
                         &value, &vallen, &txn);
        ck_assert_int_eq(ret, ZS_OK);


        /* Close and reopen DB */
        ret = zsdb_close(db);
        ck_assert_int_eq(ret, ZS_OK);
        zsdb_final(&db);

        ret = zsdb_init(&db, NULL, NULL);
        ck_assert_int_eq(ret, ZS_OK);

        ret = zsdb_open(db, basedir, MODE_RDWR);
        ck_assert_int_eq(ret, ZS_OK);


        /* After reopening, key2  - should not be found
         * key and key3 should be found.
         */
        ret = zsdb_fetch(db, kvrecsdel[1].k, kvrecsdel[1].klen,
                         &value, &vallen, &txn);
        ck_assert_int_eq(ret, ZS_NOTFOUND);

        ret = zsdb_fetch(db, kvrecsdel[0].k, kvrecsdel[0].klen,
                         &value, &vallen, &txn);
        ck_assert_int_eq(ret, ZS_OK);

        ret = zsdb_fetch(db, kvrecsdel[2].k, kvrecsdel[2].klen,
                         &value, &vallen, &txn);
        ck_assert_int_eq(ret, ZS_OK);



}
END_TEST

START_TEST(test_multiopen)
{
        struct zsdb_txn *txn = NULL;
        struct zsdb *db2 = NULL;
        int ret;

        /** ADD RECORD in tranaction 1 **/
        /* Acquire write lock */
        zsdb_write_lock_acquire(db, 0);

        ret = zsdb_add(db, kvmultiopen[0].k, kvmultiopen[0].klen,
                       kvmultiopen[0].v, kvmultiopen[0].vlen, NULL);
        ck_assert_int_eq(ret, ZS_OK);

        /* Commit the add record transaction */
        zsdb_commit(db, NULL);
        ck_assert_int_eq(ret, ZS_OK);

        /* Release write lock */
        zsdb_write_lock_release(db);

        /** Open the DB again, this is the second instance of the DB **/
        ret = zsdb_init(&db2, NULL, NULL);
        ck_assert_int_eq(ret, ZS_OK);

        ret = zsdb_open(db2, basedir, MODE_RDWR);
        ck_assert_int_eq(ret, ZS_OK);

        /** ADD RECORD - second open instance **/

        /* Acquire write lock */
        zsdb_write_lock_acquire(db2, 0);

        ret = zsdb_add(db2, kvmultiopen[1].k, kvmultiopen[1].klen,
                       kvmultiopen[1].v, kvmultiopen[1].vlen, NULL);
        ck_assert_int_eq(ret, ZS_OK);

        /* Commit the add record transaction */
        zsdb_commit(db2, NULL);
        ck_assert_int_eq(ret, ZS_OK);

        /* Release write lock */
        zsdb_write_lock_release(db2);

        /** CLOSE Second instance of the DB **/
        ret = zsdb_close(db2);
        ck_assert_int_eq(ret, ZS_OK);
        zsdb_final(&db2);

        /** ADD a record to first open instance **/
        /* Acquire write lock */
        zsdb_write_lock_acquire(db, 0);

        ret = zsdb_add(db, kvmultiopen[2].k, kvmultiopen[2].klen,
                       kvmultiopen[2].v, kvmultiopen[2].vlen, NULL);
        ck_assert_int_eq(ret, ZS_OK);

        /* Commit the add record transaction */
        zsdb_commit(db, NULL);
        ck_assert_int_eq(ret, ZS_OK);

        /* Release write lock */
        zsdb_write_lock_release(db);

        /** CLOSE first instance of the DB **/
        ret = zsdb_close(db);
        ck_assert_int_eq(ret, ZS_OK);
        zsdb_final(&db);

        /** REOPEN DB **/
        ret = zsdb_init(&db, NULL, NULL);
        ck_assert_int_eq(ret, ZS_OK);

        ret = zsdb_open(db, basedir, MODE_RDWR);
        ck_assert_int_eq(ret, ZS_OK);

        /** COUNT RECORDS **/
        record_count = 0;
        ret = zsdb_foreach(db, NULL, 0, count_fe_p, NULL, NULL, &txn);
        ck_assert_int_eq(ret, ZS_OK);
        ck_assert_int_eq(record_count, ARRAY_SIZE(kvmultiopen));
}
END_TEST

START_TEST(test_many_records)
{
        struct zsdb_txn *txn;
        size_t i, NUM_RECS;
        int ret;

        NUM_RECS = 4096;

        txn = NULL;

        /* Begin transaction */
        ret = zsdb_transaction_begin(db, &txn);
        ck_assert_int_eq(ret, ZS_OK);

        /* Acquire write lock */
        zsdb_write_lock_acquire(db, 0);

        /* Add 4096 records */
        for (i = 0; i < NUM_RECS; i++) {
                unsigned char key[24], val[24];

                snprintf((char *)key, 8, "key%zu", i);
                snprintf((char *)val, 8, "val%zu", i);

                ret = zsdb_add(db, key, strlen((char *)key), val,
                               strlen((char *)val), &txn);
                ck_assert_int_eq(ret, ZS_OK);
        }

        /* Commit the add records transaction */
        zsdb_commit(db, &txn);
        ck_assert_int_eq(ret, ZS_OK);

        /* Release write lock */
        zsdb_write_lock_release(db);

        zsdb_transaction_end(&txn);

        /* Count records */
        record_count = 0;
        txn = NULL;
        ret = zsdb_foreach(db, NULL, 0, count_fe_p, NULL, NULL, &txn);
        ck_assert_int_eq(ret, ZS_OK);
        ck_assert_int_eq(record_count, NUM_RECS);
}
END_TEST

struct ffrock {
        struct zsdb *db;
        struct zsdb_txn **tid;
        int state;
};

static int fe_cb_foreach_changes(void *data,
                                 const unsigned char *key, size_t keylen,
                                 const unsigned char *val _unused_,
                                 size_t vallen _unused_)
{
        struct ffrock *fr = (struct ffrock *)data;
        int r;

        switch (fr->state) {
        case 0:
                ck_assert_mem_eq(key, K1, keylen);
                fr->state = 1;
                break;
        case 1:
                ck_assert_mem_eq(key, K2, keylen);
                zsdb_write_lock_acquire(fr->db, 0);
                r = zsdb_add(fr->db, (const unsigned char *)K0, strlen(K0),
                             (const unsigned char *)"", 0, fr->tid);
                ck_assert_int_eq(r, ZS_OK);
                zsdb_write_lock_release(fr->db);
                fr->state = 2;
                break;
        case 2:
                ck_assert_mem_eq(key, K3, keylen);
                r = zsdb_fetch(fr->db, (const unsigned char *)K0U, strlen(K0U),
                               NULL, NULL, fr->tid);
                ck_assert_int_eq(r, ZS_NOTFOUND);
                fr->state = 3;
                break;
        case 3:
                ck_assert_mem_eq(key, K4, keylen);
                zsdb_write_lock_acquire(fr->db, 0);
                r = zsdb_add(fr->db, (const unsigned char *)K4A, strlen(K4A),
                             (const unsigned char *)"", 0, fr->tid);
                ck_assert_int_eq(r, ZS_OK);
                zsdb_write_lock_release(fr->db);
                fr->state = 4;
                break;
        case 4:
                ck_assert_mem_eq(key, K4A, keylen);
                zsdb_write_lock_acquire(fr->db, 0);
                r = zsdb_add(fr->db, (const unsigned char *)K4A, strlen(K4A),
                             (const unsigned char *)"another", 7, fr->tid);
                ck_assert_int_eq(r, ZS_OK);
                zsdb_write_lock_release(fr->db);
                fr->state = 5;
                break;
        case 5:
                ck_assert_mem_eq(key, K5, keylen);
                zsdb_write_lock_acquire(fr->db, 0);
                r = zsdb_remove(fr->db, (const unsigned char *)K5,
                                strlen(K5), fr->tid);
                ck_assert_int_eq(r, ZS_OK);
                zsdb_write_lock_release(fr->db);
                fr->state = 6;
                break;
        case 6:
                ck_assert_mem_eq(key, K6, keylen);
                fr->state = 7;
                break;
        case 7:
                ck_assert_mem_eq(key, K7, keylen);
                zsdb_write_lock_acquire(fr->db, 0);
                r = zsdb_add(fr->db, (const unsigned char *)K7, strlen(K7),
                             (const unsigned char *)"newval", 6, fr->tid);
                ck_assert_int_eq(r, ZS_OK);

                r = zsdb_add(fr->db, (const unsigned char *)K7D, strlen(K7D),
                             (const unsigned char *)"val", 3, fr->tid);
                ck_assert_int_eq(r, ZS_OK);

                r = zsdb_add(fr->db, (const unsigned char *)K7B, strlen(K7B),
                             (const unsigned char *)"bval", 4, fr->tid);
                ck_assert_int_eq(r, ZS_OK);

                r = zsdb_add(fr->db, (const unsigned char *)K7A, strlen(K7A),
                             (const unsigned char *)"aval", 4, fr->tid);
                ck_assert_int_eq(r, ZS_OK);


                r = zsdb_remove(fr->db, (const unsigned char *)K7D,
                                strlen(K7D), fr->tid);
                ck_assert_int_eq(r, ZS_OK);

                zsdb_write_lock_release(fr->db);
                fr->state = 8;
                break;
        case 8:
                ck_assert_mem_eq(key, K7A, keylen);
                fr->state = 9;
                break;
        case 9:
                ck_assert_mem_eq(key, K7B, keylen);
                fr->state = 10;
                break;
        case 10:
                ck_assert_mem_eq(key, K7B, keylen);
                fr->state = 11;
                break;
        default:
                assert(0);
                break;
        }

        return 0;
}

START_TEST(test_foreach_changes)
{
        struct zsdb_txn *txn;
        size_t i;
        int ret;
        struct ffrock rock;

        /** ADD RECORDS **/
        /* Begin transaction */
        ret = zsdb_transaction_begin(db, &txn);
        ck_assert_int_eq(ret, ZS_OK);

        /* Acquire write lock */
        zsdb_write_lock_acquire(db, 0);

        for (i = 0; i < ARRAY_SIZE(kvforeachchanges); i++) {
                ret = zsdb_add(db, kvforeachchanges[i].k, kvforeachchanges[i].klen,
                               kvforeachchanges[i].v, kvforeachchanges[i].vlen, &txn);
                ck_assert_int_eq(ret, ZS_OK);
        }

        /* Commit the add records transaction */
        zsdb_commit(db, &txn);
        ck_assert_int_eq(ret, ZS_OK);

        /* Release write lock */
        zsdb_write_lock_release(db);

        zsdb_transaction_end(&txn);

        txn = NULL;

        /** Process Records **/
        rock.db = db;
        rock.state = 0;
        rock.tid = &txn;

        ret = zsdb_foreach(db, NULL, 0, NULL, fe_cb_foreach_changes,
                           &rock, rock.tid);
        ck_assert_int_eq(ret, ZS_OK);
        ck_assert_int_eq(rock.state, 7);

        /* Commit the transaction */
        zsdb_write_lock_acquire(db, 0);

        zsdb_commit(db, &txn);
        ck_assert_int_eq(ret, ZS_OK);

        zsdb_write_lock_release(db);
        zsdb_transaction_end(&txn);

        txn = NULL;
}
END_TEST

START_TEST(test_foreach_count)
{
        struct zsdb_txn *txn;
        size_t i;
        int ret;

        /** ADD RECORDS **/
        /* Begin transaction */
        ret = zsdb_transaction_begin(db, &txn);
        ck_assert_int_eq(ret, ZS_OK);

        /* Acquire write lock */
        zsdb_write_lock_acquire(db, 0);

        for (i = 0; i < ARRAY_SIZE(kvrecsgen); i++) {
                ret = zsdb_add(db, kvrecsgen[i].k, kvrecsgen[i].klen,
                               kvrecsgen[i].v, kvrecsgen[i].vlen, &txn);
                ck_assert_int_eq(ret, ZS_OK);
        }

        /* Commit the add records transaction */
        zsdb_commit(db, &txn);
        ck_assert_int_eq(ret, ZS_OK);

        /* Release write lock */
        zsdb_write_lock_release(db);

        zsdb_transaction_end(&txn);

        txn = NULL;

        /* DELETE A RECORD */
        /* Acquire write lock */
        zsdb_write_lock_acquire(db, 0);

        /* Delete a record */
        ret = zsdb_remove(db, (const unsigned char *)"foo", 3, &txn);
        ck_assert_int_eq(ret, ZS_OK);

        /* Commit after deleting record */
        zsdb_commit(db, &txn);
        ck_assert_int_eq(ret, ZS_OK);

        zsdb_write_lock_release(db);

        /* Count records */
        record_count = 0;
        ret = zsdb_foreach(db, NULL, 0, count_fe_p, NULL, NULL, &txn);
        ck_assert_int_eq(ret, ZS_OK);
        ck_assert_int_eq(record_count, ARRAY_SIZE(kvrecsgen) - 1);
}
END_TEST

START_TEST(test_foreach_heirarchy)
{
        struct zsdb_txn *txn;
        size_t i;
        int ret;
        const unsigned char *key = (const unsigned char *)"abc.";

        /** ADD RECORDS **/
        /* Begin transaction */
        ret = zsdb_transaction_begin(db, &txn);
        ck_assert_int_eq(ret, ZS_OK);

        /* Acquire write lock */
        zsdb_write_lock_acquire(db, 0);

        for (i = 0; i < ARRAY_SIZE(kvrecsgen); i++) {
                ret = zsdb_add(db, kvrecsgen[i].k, kvrecsgen[i].klen,
                               kvrecsgen[i].v, kvrecsgen[i].vlen, &txn);
                ck_assert_int_eq(ret, ZS_OK);
        }

        /* Commit the add records transaction */
        zsdb_commit(db, &txn);
        ck_assert_int_eq(ret, ZS_OK);

        /* Release write lock */
        zsdb_write_lock_release(db);

        zsdb_transaction_end(&txn);

        txn = NULL;

        /* Count records */
        record_count = 0;
        ret = zsdb_foreach(db, NULL, 0, count_fe_p, NULL, NULL, &txn);
        ck_assert_int_eq(ret, ZS_OK);
        ck_assert_int_eq(record_count, ARRAY_SIZE(kvrecsgen));

        /* Count heirarchical records */
        txn = NULL;
        record_count = 0;

        ret = zsdb_transaction_begin(db, &txn);
        ck_assert_int_eq(ret, ZS_OK);

        ret = zsdb_forone(db, key, 3, count_fe_p, NULL, NULL, &txn);
        ck_assert_int_eq(ret, ZS_OK);

        ret = zsdb_foreach(db, key, 4, count_fe_p, NULL, NULL, &txn);
        ck_assert_int_eq(ret, ZS_OK);

        ck_assert_int_eq(record_count, 5);

        zsdb_transaction_end(&txn);
}
END_TEST

START_TEST(test_fetchnext_simple)
{
        struct zsdb_txn *txn;
        size_t i;
        int ret;
        const unsigned char *found, *value;
        size_t foundlen = 0, vallen = 0;

        /** ADD RECORDS **/
        /* Begin transaction */
        ret = zsdb_transaction_begin(db, &txn);
        ck_assert_int_eq(ret, ZS_OK);

        /* Acquire write lock */
        zsdb_write_lock_acquire(db, 0);

        for (i = 0; i < ARRAY_SIZE(kvrecsgen); i++) {
                ret = zsdb_add(db, kvrecsgen[i].k, kvrecsgen[i].klen,
                               kvrecsgen[i].v, kvrecsgen[i].vlen, &txn);
                ck_assert_int_eq(ret, ZS_OK);
        }

        /* Commit the add records transaction */
        zsdb_commit(db, &txn);
        ck_assert_int_eq(ret, ZS_OK);

        /* Release write lock */
        zsdb_write_lock_release(db);

        zsdb_transaction_end(&txn);

        txn = NULL;

        ret = zsdb_fetchnext(db, (const unsigned char *)"key",
                             strlen("key"), &found, &foundlen,
                             &value, &vallen, &txn);
        ck_assert_int_eq(ret, ZS_OK);

        ck_assert_mem_eq(found, "nokia", foundlen);
        ck_assert_mem_eq(value, "meego", vallen);
}
END_TEST

/*
 * Helpers for tests that move records across the active, finalised and
 * packed tiers.
 */
static void tier_add(const char *key, const char *val)
{
        struct zsdb_txn *txn = NULL;
        int ret;

        ck_assert_int_eq(zsdb_write_lock_acquire(db, 0), ZS_OK);
        ret = zsdb_add(db, (const unsigned char *)key, strlen(key),
                       (const unsigned char *)val, strlen(val), &txn);
        ck_assert_int_eq(ret, ZS_OK);
        ck_assert_int_eq(zsdb_commit(db, &txn), ZS_OK);
        ck_assert_int_eq(zsdb_write_lock_release(db), ZS_OK);
}

static void tier_remove(const char *key)
{
        struct zsdb_txn *txn = NULL;
        int ret;

        ck_assert_int_eq(zsdb_write_lock_acquire(db, 0), ZS_OK);
        ret = zsdb_remove(db, (const unsigned char *)key, strlen(key), &txn);
        ck_assert_int_eq(ret, ZS_OK);
        ck_assert_int_eq(zsdb_commit(db, &txn), ZS_OK);
        ck_assert_int_eq(zsdb_write_lock_release(db), ZS_OK);
}

static void tier_finalise(void)
{
        ck_assert_int_eq(zsdb_write_lock_acquire(db, 0), ZS_OK);
        ck_assert_int_eq(zsdb_finalise(db), ZS_OK);
        ck_assert_int_eq(zsdb_write_lock_release(db), ZS_OK);
}

static void tier_reopen(void);

/* Reopen before packing, like in the real world, a separate process would. An
 * in-process zsdb_finalise() doesn't add the new file to the list of
 * finalised files, so repack wouldn't see it otherwise.
 */
static void tier_repack(void)
{
        tier_reopen();
        ck_assert_int_eq(zsdb_pack_lock_acquire(db, 0), ZS_OK);
        ck_assert_int_eq(zsdb_repack(db), ZS_OK);
        ck_assert_int_eq(zsdb_pack_lock_release(db), ZS_OK);
}

static void tier_reopen(void)
{
        ck_assert_int_eq(zsdb_close(db), ZS_OK);
        zsdb_final(&db);
        ck_assert_int_eq(zsdb_init(&db, NULL, NULL), ZS_OK);
        ck_assert_int_eq(zsdb_open(db, basedir, MODE_RDWR), ZS_OK);
}

static void tier_assert_value(const char *key, const char *expected)
{
        const unsigned char *value = NULL;
        size_t vallen = 0;
        struct zsdb_txn *txn = NULL;
        int ret;

        ret = zsdb_fetch(db, (const unsigned char *)key, strlen(key),
                         &value, &vallen, &txn);
        ck_assert_int_eq(ret, ZS_OK);
        ck_assert_uint_eq(vallen, strlen(expected));
        ck_assert_mem_eq(value, expected, vallen);
}

static void tier_assert_missing(const char *key)
{
        const unsigned char *value = NULL;
        size_t vallen = 0;
        struct zsdb_txn *txn = NULL;
        int ret;

        ret = zsdb_fetch(db, (const unsigned char *)key, strlen(key),
                         &value, &vallen, &txn);
        ck_assert_int_eq(ret, ZS_NOTFOUND);
}

/* Repacking when there is exactly one finalised file used to generate a
 * packed filename identical to that finalised file (`-0-0`), and then
 * unlink it as a "packed input", losing all of its records.
 */
START_TEST(test_repack_single_finalised_keeps_data)
{
        tier_add("alpha", "one");
        tier_add("beta", "two");
        tier_finalise();

        tier_repack();

        tier_assert_value("alpha", "one");
        tier_assert_value("beta", "two");

        tier_reopen();
        tier_assert_value("alpha", "one");
        tier_assert_value("beta", "two");
}
END_TEST

/* Repacking two finalised files must produce a packed file with the
 * records from both.
 */
START_TEST(test_repack_finalised_files_keeps_data)
{
        tier_add("alpha", "one");
        tier_finalise();
        tier_add("beta", "two");
        tier_finalise();

        tier_repack();
        tier_reopen();

        tier_assert_value("alpha", "one");
        tier_assert_value("beta", "two");
}
END_TEST

/* A key deleted in the active file must not be resurrected from a
 * finalised file.
 */
START_TEST(test_delete_shadows_finalised)
{
        tier_add("alpha", "one");
        tier_add("beta", "two");
        tier_finalise();

        tier_remove("alpha");
        tier_assert_missing("alpha");
        tier_assert_value("beta", "two");

        tier_reopen();
        tier_assert_missing("alpha");
        tier_assert_value("beta", "two");

        /* The tombstone itself is now in a finalised file */
        tier_finalise();
        tier_reopen();
        tier_assert_missing("alpha");
        tier_assert_value("beta", "two");
}
END_TEST

/* A key deleted after its value was packed must stay deleted, whether
 * the tombstone lives in the active, a finalised or a newer packed file.
 */
START_TEST(test_delete_shadows_packed)
{
        /* Put alpha and beta into a packed file (0-1) */
        tier_add("alpha", "one");
        tier_finalise();
        tier_add("beta", "two");
        tier_finalise();
        tier_repack();
        tier_reopen();
        tier_assert_value("alpha", "one");

        /* Tombstone in the active file */
        tier_remove("alpha");
        tier_assert_missing("alpha");
        tier_reopen();
        tier_assert_missing("alpha");
        tier_assert_value("beta", "two");

        /* Tombstone in a finalised file */
        tier_finalise();
        tier_reopen();
        tier_assert_missing("alpha");

        /* Tombstone in a newer packed file */
        tier_add("gamma", "three");
        tier_finalise();
        tier_repack();
        tier_reopen();
        tier_assert_missing("alpha");
        tier_assert_value("beta", "two");
        tier_assert_value("gamma", "three");
}
END_TEST

Suite *zsdb_suite(void)
{
        Suite *s;
        TCase *tc_core;
        TCase *tc_tiers;
        TCase *tc_many;
        TCase *tc_foreach;
        TCase *tc_fetch;

        s = suite_create("zeroskip");

        /* core */
        tc_core = tcase_create("core");
        tcase_add_checked_fixture(tc_core, setup, teardown);

        tcase_add_test(tc_core, test_abort_transaction);
        tcase_add_test(tc_core, test_delete);
        tcase_add_test(tc_core, test_multiopen);
        suite_add_tcase(s, tc_core);

        /* records moving across active/finalised/packed files */
        tc_tiers = tcase_create("tiers");
        tcase_add_checked_fixture(tc_tiers, setup, teardown);

        tcase_add_test(tc_tiers, test_repack_single_finalised_keeps_data);
        tcase_add_test(tc_tiers, test_repack_finalised_files_keeps_data);
        tcase_add_test(tc_tiers, test_delete_shadows_finalised);
        tcase_add_test(tc_tiers, test_delete_shadows_packed);
        suite_add_tcase(s, tc_tiers);

        /* foreach */
        tc_foreach = tcase_create("foreach");
        tcase_add_checked_fixture(tc_foreach, setup, teardown);

        tcase_add_test(tc_foreach, test_foreach_changes);
        tcase_add_test(tc_foreach, test_foreach_count);
        tcase_add_test(tc_foreach, test_foreach_heirarchy);
        suite_add_tcase(s, tc_foreach);

        /* fetch */
        tc_fetch = tcase_create("fetch");
        tcase_add_checked_fixture(tc_fetch, setup, teardown);

        tcase_add_test(tc_fetch, test_fetchnext_simple);
        suite_add_tcase(s, tc_fetch);

        /* many records */
        tc_many = tcase_create("many");
        tcase_add_checked_fixture(tc_many, setup, teardown);
        /* The default time out is 4 seconds and while this is perfectly fine on
         * on my machine, the tc_many test fails when running on travis. So
         * bumping up the timeout to 50 seconds.
         */
        tcase_set_timeout(tc_many, 50);

        tcase_add_test(tc_many, test_many_records);
        suite_add_tcase(s, tc_many);

        return s;
}
