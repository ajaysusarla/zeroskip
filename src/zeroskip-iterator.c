/*
 * zeroskip-iterator.c
 *
 * This file is part of zeroskip.
 *
 * zeroskip is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 */

#include <libzeroskip/log.h>
#include <libzeroskip/util.h>
#include <libzeroskip/zeroskip.h>

#include "zeroskip-priv.h"

/**
 * Private functions
 */

/* hash table handling */
static struct iter_htable_entry *alloc_iter_htable_entry(unsigned char *key,
                                                         uint64_t keylen,
                                                         void *value)
{
        struct iter_htable_entry *e;

        e = xmalloc(sizeof(struct iter_htable_entry));
        e->key = xmalloc(sizeof(unsigned char) * keylen);
        memcpy(e->key, key, keylen);
        e->keylen = keylen;
        e->value = value;

        return e;
}

static void free_iter_htable_entry(struct iter_htable_entry **entry)
{
        if (entry && *entry) {
                xfree((*entry)->key);
                (*entry)->keylen = 0;
                (*entry)->value = NULL;
                xfree(*entry);
        }
}

static int iter_htable_entry_cmpfn(const void *unused1 _unused_,
                                   const void *entry1,
                                   const void *entry2,
                                   const void *unused2 _unused_)
{
        const struct iter_htable_entry *e1 = entry1;
        const struct iter_htable_entry *e2 = entry2;

        return memcmp_raw(e1->key, e1->keylen, e2->key, e2->keylen);
}

static void iter_htable_init(struct iter_htable *ht, const void *cmpfndata)
{
        htable_init(&ht->table, iter_htable_entry_cmpfn, cmpfndata, 0);
}

static void *iter_htable_get(struct iter_htable *ht, unsigned char *key,
                             uint64_t keylen)
{
        struct iter_htable_entry k;
        struct iter_htable_entry *e;

        if (!ht->table.size)
                iter_htable_init(ht, ht->table.cmpfndata);

        htable_entry_init(&k, bufhash(key, keylen));
        k.key = (unsigned char *)key;
        k.keylen = keylen;
        e = htable_get(&ht->table, &k, NULL);

        return e ? e : NULL;
}

static void iter_htable_put(struct iter_htable *ht, unsigned char *key,
                            uint64_t keylen, void *value)
{
        struct iter_htable_entry *e;

        if (!ht->table.size)
                iter_htable_init(ht, ht->table.cmpfndata);

        e = alloc_iter_htable_entry(key, keylen, value);
        htable_entry_init(e, bufhash(key, keylen));

        htable_put(&ht->table, e);
}

static void *iter_htable_remove(struct iter_htable *ht,
                                const unsigned char *key,
                                uint64_t keylen)
{
        struct iter_htable_entry e = ITER_HTABLE_ENTRY_INIT;

        htable_entry_init(&e, bufhash(key, keylen));

        if (!ht->table.size)
                iter_htable_init(ht, ht->table.cmpfndata);

        return htable_remove(&ht->table, &e, key);
}

static void iter_htable_free(struct iter_htable *ht)
{
        struct htable_iter iter;
        struct iter_htable_entry *e;

        htable_iter_init(&ht->table, &iter);
        while ((e = htable_iter_next(&iter))) {
                free_iter_htable_entry(&e);
        }

        htable_free(&ht->table, 0);
}

/* iter data handling */
static struct iter_key_data *alloc_iter_key_data(unsigned char *key,
                                                 uint64_t keylen)
{
        struct iter_key_data *data = NULL;

        data = xmalloc(sizeof(struct iter_key_data));

        data->key = xmalloc(sizeof(unsigned char) * keylen);
        memcpy(data->key, key, keylen);
        data->len = keylen;

        return data;
}

static void free_iter_key_data(struct iter_key_data **data)
{
        if (data && *data) {
                xfree((*data)->key);
                (*data)->len = 0;
                xfree(*data);
        }
}

static int iter_pq_cmp(const void *d1, const void *d2, void *cbdata)
{
        const struct iter_key_data *e1, *e2;
        struct zsdb_priv *priv = cbdata;
        int ret = -1;

        e1 = (const struct iter_key_data *)d1;
        e2 = (const struct iter_key_data *)d2;

        if (priv && priv->dbcompare)
                ret = priv->dbcompare(e1->key, e1->len, e2->key, e2->len);
        else
                ret = memcmp_raw(e1->key, e1->len, e2->key, e2->len);

        return ret;
}

/* struct zsdb_iter_data handling */
static struct zsdb_iter_data *zsdb_iter_data_alloc(zsdb_be_t type, int prio,
                                                   void *data,
                                                   memtree_iter_t *iter)
{
        struct zsdb_iter_data *d;

        d = xcalloc(1, sizeof(struct zsdb_iter_data));

        d->type = type;
        d->priority = prio;
        d->done = 0;
        d->deleted = 0;
        if (type == ZSDB_BE_PACKED) {
                /* data is in files */
                d->data.f = data;
        } else {
                /* data is an in-memory memtree for finalised and active */
                struct memtree *tree = data;
                if (!iter) {
                        memtree_begin(tree, d->data.iter);
                } else {
                        d->data.iter->tree = (*iter)->tree;
                        d->data.iter->node = (*iter)->node;
                        d->data.iter->pos  = (*iter)->pos;
                        d->data.iter->record = (*iter)->record;
                }
                memtree_next(d->data.iter);
        }

        return d;
}

static void zsdb_iter_data_free(struct zsdb_iter_data **iterd)
{
        if (iterd && *iterd) {
                struct zsdb_iter_data *data = *iterd;
                *iterd = NULL;

                xfree(data);
        }
}

static void zsdb_iter_datav_add_iter(struct zsdb_iter *iter,
                                     struct zsdb_iter_data *iterd)
{
        ALLOC_GROW(iter->datav, iter->iter_data_count + 1, iter->iter_data_alloc);
        iter->datav[iter->iter_data_count++] = iterd;
        iter->datav[iter->iter_data_count] = NULL;
}

static void zsdb_iter_datav_clear_iter(struct zsdb_iter *iter)
{
        if (iter->datav != NULL) {
                int i;
                for (i = 0; i < iter->iter_data_count; i++) {
                        struct zsdb_iter_data *d = iter->datav[i];
                        zsdb_iter_data_free(&d);
                }
                xfree(iter->datav);
        }
}

static void zsdb_iter_data_process(struct zsdb_iter *iter,
                                   unsigned char *key, uint64_t keylen,
                                   struct zsdb_iter_data *new_iterd);
/* Get next entry in zsdb_iter_data */
static int zsdb_iter_data_next(struct zsdb_iter *iter,
                               struct zsdb_iter_data *iterdata)
{
        unsigned char *key = NULL;
        uint64_t keylen = 0;
        int ret = 0;

        if (!iterdata)
                return ret;

        if (iterdata->done)
                return 0;

        switch (iterdata->type) {
        case ZSDB_BE_ACTIVE:
        case ZSDB_BE_FINALISED:
                if (memtree_next(iterdata->data.iter)) {
                        key = iterdata->data.iter->record->key;
                        keylen = iterdata->data.iter->record->keylen;
                }
                break;
        case ZSDB_BE_PACKED:
        {
                struct zsdb_file *f = iterdata->data.f;
                f->indexpos++;
                if (f->indexpos < f->index->count)
                        zs_packed_file_get_key_from_offset(f, &key,
                                                           &keylen, NULL);
                break;
        }
        default:
                abort();        /* should never reach here */
        }

        if (!key) {
                iterdata->done = 1;
                ret = 0;
        } else {
                zsdb_iter_data_process(iter, key, keylen, iterdata);
                ret = 1;
        }

        return ret;
}

/* Set `deleted` from the record the iterator data currently points at.
 */
static void zsdb_iter_data_update_deleted(struct zsdb_iter_data *iterd)
{
        switch (iterd->type) {
        case ZSDB_BE_ACTIVE:
        case ZSDB_BE_FINALISED:
                iterd->deleted = iterd->data.iter->record ?
                        iterd->data.iter->record->deleted : 0;
                break;
        case ZSDB_BE_PACKED:
        {
                struct zsdb_file *f = iterd->data.f;
                struct zs_key k;

                iterd->deleted = 0;
                if (f->indexpos < f->index->count &&
                    zs_record_read_key_from_file_offset(f,
                                                        f->index->data[f->indexpos],
                                                        &k) == ZS_OK)
                        iterd->deleted = !!(k.base.type & REC_TYPE_DELETED);
                break;
        }
        default:
                abort();        /* should never reach here */
        }
}

/* Process struct zsdb_iter_data */
static void zsdb_iter_data_process(struct zsdb_iter *iter,
                                   unsigned char *key, uint64_t keylen,
                                   struct zsdb_iter_data *new_iterd)
{
        struct iter_htable_entry *e;

        /* new_iterd has just moved to `key` */
        zsdb_iter_data_update_deleted(new_iterd);

        /* check if the key exists in the hash table */
        e = iter_htable_get(&iter->ht, key, keylen);
        if (e) {                /* key exists */
                /* If the key exists, we check if the priority of the existing
                 * entry is less than that of the new entry
                 */
                struct zsdb_iter_data *old_iterd = e->value;
                if (new_iterd->priority > old_iterd->priority) {
                        /* Replace it with new iter_data */
                        e = iter_htable_remove(&iter->ht, key, keylen);
                        free_iter_htable_entry(&e);
                        iter_htable_put(&iter->ht, key, keylen, new_iterd);
                        /* Process the old iterd */
                        zsdb_iter_data_next(iter, old_iterd);
                } else {
                        /* If the new iterd's priority is lesser than the older
                         * iterator, we reprocess the new iterator.
                         */
                        zsdb_iter_data_next(iter, new_iterd);
                }
        } else {                /* new key */
                struct iter_key_data *pqd;
                pqd = alloc_iter_key_data(key, keylen);

                /* Add to the hashtable */
                iter_htable_put(&iter->ht, key, keylen, new_iterd);

                /* Add to the PQueue */
                pqueue_put(&iter->pq, pqd);
        }
}

/**
 * Public functions
 */
int zs_iterator_new(struct zsdb *db, struct zsdb_iter **iter)
{
        struct zsdb_iter *t = NULL;
        struct zsdb_priv *priv;

        assert(db);
        assert(db->priv);

        priv = db->priv;
        if (!priv) return ZS_INTERNAL;

        if (!priv->open) {
                zslog(LOGWARNING, "DB `%s` not open!\n", priv->dbdir.buf);
                return ZS_NOT_OPEN;
        }

        t = xcalloc(1, sizeof(struct zsdb_iter));

        t->db = db;
        t->pq.cmp = iter_pq_cmp;
        t->pq.cmpfndata = priv;
        iter_htable_init(&t->ht, priv);
        t->datav = NULL;
        t->iter_data_count = 0;
        t->iter_data_alloc = 0;
        t->forone_iter = 0;

        *iter = t;

        return ZS_OK;
}

/* zs_iterator_begin():
 * A function to begin an iterator, on a DB. This is the function that
 * needs to be used to iterate over all records in the DB!
 */
int zs_iterator_begin(struct zsdb_iter **iter)
{
        struct zsdb *db = NULL;
        struct zsdb_priv *priv;
        struct list_head *pos;
        int prio = 0;
        struct zsdb_iter_data *fiterd, *aiterd;

        if (!iter || !*iter) {
                zslog(LOGWARNING, "Invalid iterator!\n");
                return ZS_INTERNAL;
        }

        db = (*iter)->db;

        assert(db);
        assert(db->priv);

        priv = db->priv;
        if (!priv) return ZS_INTERNAL;

        if (!priv->open) {
                zslog(LOGWARNING, "DB `%s` not open!\n", priv->dbdir.buf);
                return ZS_NOT_OPEN;
        }


        /* Add packed files to the iterator */
        list_for_each_forward(pos, &priv->dbfiles.pflist) {
                struct zsdb_iter_data *piterd;
                struct zsdb_file *f;
                unsigned char *key;
                uint64_t keylen;

                f = list_entry(pos, struct zsdb_file, list);
                piterd = zsdb_iter_data_alloc(ZSDB_BE_PACKED, f->priority,
                                              f, NULL);

                zs_packed_file_get_key_from_offset(f, &key, &keylen, NULL);

                zsdb_iter_datav_add_iter(*iter, piterd);

                zsdb_iter_data_process(*iter, key, keylen, piterd);
        }

        /* Finalised and active records are newer than any packed file */
        prio = priv->dbfiles.pfcount;

        /* Add finalised files to the iterator*/
        if (priv->dbfiles.ffcount) {
                prio++;
                fiterd = zsdb_iter_data_alloc(ZSDB_BE_FINALISED, prio,
                                              priv->fmemtree, NULL);
                zsdb_iter_datav_add_iter(*iter, fiterd);
                zsdb_iter_data_process(*iter, fiterd->data.iter->record->key,
                                       fiterd->data.iter->record->keylen,
                                       fiterd);
        }

        /* Add active file to the iterator */
        if (priv->memtree->count) {
                prio++;
                aiterd = zsdb_iter_data_alloc(ZSDB_BE_ACTIVE, prio,
                                              priv->memtree, NULL);
                zsdb_iter_datav_add_iter(*iter, aiterd);
                zsdb_iter_data_process(*iter, aiterd->data.iter->record->key,
                                       aiterd->data.iter->record->keylen,
                                       aiterd);
        }

        return ZS_OK;
}

/* zs_iterator_begin_at_key():
 *  sets up the iterator to start from the 'key'.
 *  If 'key' is not found, then the transaction is points to the 'next' closest
 *  key in that iterator for that back-end.
 */
int zs_iterator_begin_at_key(struct zsdb_iter **iter,
                             const unsigned char *key,
                             uint64_t keylen,
                             int *found)
{
        struct zsdb_priv *priv;
        struct list_head *pos;
        int prio = 0;
        memtree_iter_t aiter, fiter;
        struct zsdb_iter_data *fiterd, *aiterd;

        if (!iter || !*iter) {
                zslog(LOGWARNING, "Invalid iterator!\n");
                return ZS_INTERNAL;
        }

        priv = (*iter)->db->priv;
        if (!priv) return ZS_INTERNAL;

        if (!priv->open) {
                zslog(LOGWARNING, "DB `%s` not open!\n", priv->dbdir.buf);
                return ZS_NOT_OPEN;
        }

        /* Look for the key in packed records and add to iterator */
        list_for_each_forward(pos, &priv->dbfiles.pflist) {
                struct zsdb_iter_data *piterd;
                struct zsdb_file *f;
                uint64_t location = 0;
                unsigned char *nextkey = NULL;
                uint64_t nextkeylen = 0;

                f = list_entry(pos, struct zsdb_file, list);

                zslog(LOGDEBUG, "Looking in packed file %s\n",
                      f->fname.buf);
                zslog(LOGDEBUG, "\tTotal records: %d\n",
                      f->index->count);

                if (zs_packed_file_bsearch_index(key, keylen, f,
                                                 &location, NULL, 0,
                                                 priv->dbcompare)) {
                        zslog(LOGDEBUG, "Record found at location %ld\n",
                              location);
                        *found = 1;
                }

                f->indexpos = location;

                piterd = zsdb_iter_data_alloc(ZSDB_BE_PACKED, f->priority,
                                              f, NULL);
                zsdb_iter_datav_add_iter(*iter, piterd);

                zs_packed_file_get_key_from_offset(f, &nextkey,
                                                   &nextkeylen,
                                                   NULL);
                zsdb_iter_data_process(*iter, nextkey, nextkeylen, piterd);
        }

        /* Finalised and active records are newer than any packed file */
        prio = priv->dbfiles.pfcount;

        /* Look for the key in the finalised records and add the iterator */
        prio++;
        if (memtree_find(priv->fmemtree, key, keylen, fiter)) {
                /* We found the key in finalised records */
                *found = 1;
        }

        if (priv->fmemtree->count && fiter->record) {
                fiterd = zsdb_iter_data_alloc(ZSDB_BE_FINALISED, prio,
                                              priv->fmemtree, &fiter);
                zsdb_iter_datav_add_iter(*iter, fiterd);
                zsdb_iter_data_process(*iter, fiterd->data.iter->record->key,
                                       fiterd->data.iter->record->keylen, fiterd);
        }

        /* Look for the key in the active in-memory memtree and add the iterator */
        prio++;
        if (memtree_find(priv->memtree, key, keylen, aiter)) {
                /* We found the key in active records */
                *found = 1;
        }

        if (priv->memtree->count && aiter->record) {
                aiterd = zsdb_iter_data_alloc(ZSDB_BE_ACTIVE, prio,
                                              priv->memtree, &aiter);
                zsdb_iter_datav_add_iter(*iter, aiterd);
                zsdb_iter_data_process(*iter, aiterd->data.iter->record->key,
                                       aiterd->data.iter->record->keylen, aiterd);
        }

        return ZS_OK;
}

/* zs_iterator_begin_for_packed_flist():
 * A function to begin an iterator on a set of packed files listed in
 * `pflist`, in a DB and to iterate over it.
 */
int zs_iterator_begin_for_packed_files(struct zsdb_iter **iter,
                                       struct list_head *pflist)
{
        struct zsdb *db = NULL;
        struct zsdb_priv *priv;
        struct list_head *pos;

        if (!iter || !*iter) {
                zslog(LOGWARNING, "Invalid iterator!\n");
                return ZS_INTERNAL;
        }

        db = (*iter)->db;

        assert(db);
        assert(db->priv);

        priv = db->priv;
        if (!priv) return ZS_INTERNAL;

        if (!priv->open) {
                zslog(LOGWARNING, "DB `%s` not open!\n", priv->dbdir.buf);
                return ZS_NOT_OPEN;
        }

        /* Add packed files to the iterator */
        list_for_each_forward(pos, pflist) {
                struct zsdb_iter_data *piterd;
                struct zsdb_file *f;
                unsigned char *key;
                uint64_t keylen;

                f = list_entry(pos, struct zsdb_file, list);
                piterd = zsdb_iter_data_alloc(ZSDB_BE_PACKED, f->priority,
                                              f, NULL);
                zsdb_iter_datav_add_iter(*iter, piterd);

                zs_packed_file_get_key_from_offset(f, &key, &keylen, NULL);

                zsdb_iter_data_process(*iter, key, keylen, piterd);
        }

        return ZS_OK;
}

/* zs_iterator_get():
 * Given a valid iterator pointer, get the current iterator data
 */
struct zsdb_iter_data *zs_iterator_get(struct zsdb_iter *iter)
{
        struct iter_key_data *ikdata = NULL;
        struct zsdb_iter_data *iterdata = NULL;
        struct iter_htable_entry *entry = NULL;

        if (!iter)
                return NULL;

        assert(iter->db);
        assert(iter->db->priv);

        ikdata = pqueue_get(&iter->pq);
        if (!ikdata)
                return NULL;

        entry = iter_htable_get(&iter->ht, ikdata->key, ikdata->len);
        if (entry) {
                iterdata = entry->value;
                entry = iter_htable_remove(&iter->ht, ikdata->key, ikdata->len);
                free_iter_htable_entry(&entry);
        }

        free_iter_key_data(&ikdata);

        return iterdata;
}

/* zs_iterator_next():
 * Given a valid iterator pointer, move the next iterator data in the DB
 */
int zs_iterator_next(struct zsdb_iter *iter,
                     struct zsdb_iter_data *data)
{
        if (!iter)
                return 0;

        assert(iter->db);
        assert(iter->db->priv);

        zsdb_iter_data_next(iter, data);

        return iter->pq.count ? 1 : 0;
}

/* zs_iterator_end():
 * End an iterator and free the resources.
 */
void zs_iterator_end(struct zsdb_iter **iter)
{
        struct zsdb_iter *titer;

        if (iter && *iter) {
                struct iter_key_data *idata;
                struct zsdb_priv *priv;
                struct list_head *pos;

                titer = *iter;
                *iter = NULL;
                if (titer->db && titer->db->priv)
                        priv = titer->db->priv;
                else
                        goto done;
                /* Reset the index positions of all the packed files */
                list_for_each_forward(pos, &priv->dbfiles.pflist) {
                        struct zsdb_file *f;
                        f = list_entry(pos, struct zsdb_file, list);

                        f->indexpos = 0;
                }
                titer->db = NULL;
                priv = NULL;

        done:
                while ((idata = pqueue_get(&titer->pq)))
                        free_iter_key_data(&idata);

                pqueue_free(&titer->pq);
                iter_htable_free(&titer->ht);
                zsdb_iter_datav_clear_iter(titer);

                xfree(titer);
        }
}
