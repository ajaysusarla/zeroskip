/*
 * zeroskip-packed.c
 *
 * This file is part of zeroskip.
 *
 * zeroskip is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 */


#include "btree.h"
#include "log.h"
#include "macros.h"
#include "pqueue.h"
#include "vecu64.h"
#include "util.h"
#include "zeroskip.h"
#include "zeroskip-priv.h"

/**
 * Private functions
 */
static int zs_packed_file_write_index_count(struct zsdb_file *f,
                                            uint64_t count)
{
        unsigned char buf[8];
        size_t nbytes;
        int ret;

        memset(buf, 0, sizeof(buf));

        write_be64(buf, count);
        ret = mappedfile_write(&f->mf, (void *)buf, sizeof(uint64_t), &nbytes);
        if (ret) {
                zslog(LOGDEBUG, "Error writing index count\n");
                ret = ZS_IOERROR;
        }

        return ret;
}

static int zs_packed_file_write_index(void *data, uint64_t offset)
{
        unsigned char buf[8];
        struct zsdb_file *f = (struct zsdb_file *)data;
        size_t nbytes;
        int ret;

        write_be64(buf, offset);

        ret = mappedfile_write(&f->mf, (void *)buf, sizeof(uint64_t), &nbytes);
        if (ret) {
                zslog(LOGDEBUG, "Error writing index\n");
                ret = ZS_IOERROR;
        }

        return ret;
}

/* get_offset_to_pointers():
 * Given a struct zsdb_file pointer and an offset to the final commit
 * record, this function returns the offset to the beginning of the pointers
 * section.
 * Returns error otherwise.
 */
static int get_offset_to_pointers(struct zsdb_file *f, size_t *offset)
{
        /* TODO: Verify CRC32 */
        unsigned char *bptr, *fptr;
        uint64_t data;
        enum record_t rectype;

        if (!f->is_open)
                return ZS_IOERROR;

        bptr = f->mf->ptr;
        fptr = bptr + *offset;

        data = read_be64(fptr);
        rectype = data >> 56;

        if (rectype == REC_TYPE_FINAL) {
                /* TODO: just read length not the whole record */
                struct zs_short_commit zshort;
                zshort.type = rectype;
                zshort.length = (data >> 32) & 0xFFFFFF;
                zshort.crc32 = data & ((1UL >> 32) - 1);

                *offset = *offset - zshort.length;
                return ZS_OK;
        } else if (rectype == REC_TYPE_2ND_HALF_COMMIT) {
                /* This is a long commit */
                *offset = *offset - (ZS_LONG_COMMIT_REC_SIZE -
                                     ZS_SHORT_COMMIT_REC_SIZE);
                return get_offset_to_pointers(f, offset);
        } else if (rectype == REC_TYPE_LONG_FINAL) {
                uint64_t len;
                fptr = fptr + sizeof(uint64_t);
                len = read_be64(fptr);

                *offset = *offset - len;
                return ZS_OK;
        } else {
                zslog(LOGDEBUG, "Not a valid final commit record\n");
                return ZS_ERROR;
        }
}

static int read_pointers(struct zsdb_file *f, size_t offset)
{
        uint64_t count, i;
        unsigned char *bptr, *fptr;

        if (!f->is_open)
                return ZS_IOERROR;

        bptr = f->mf->ptr;
        fptr = bptr + offset;

        /* Get the number of records */
        count = read_be64(fptr);
        fptr += sizeof(uint64_t);

        for (i = 0; i < count; i++) {
                uint64_t n;
                n = read_be64(fptr);
                vecu64_append(f->index, n);

                fptr += sizeof(uint64_t);
        }

        return ZS_OK;
}

/**
 * Public functions
 */

int zs_packed_file_write_btree_record(struct record *record, void *data)
{
        struct zsdb_file *f = (struct zsdb_file *)data;
        int ret = ZS_OK;

        vecu64_append(f->index, f->mf->offset);
        ret = zs_file_write_keyval_record(f, record->key, record->keylen,
                                          record->val, record->vallen);
        if (ret == ZS_OK) return 1;
        else return 0;
}

int zs_packed_file_write_record(void *data,
                                unsigned char *key, size_t keylen,
                                unsigned char *value, size_t vallen)
{
        struct zsdb_file *f = (struct zsdb_file *)data;
        int ret = ZS_OK;

        vecu64_append(f->index, f->mf->offset);
        ret = zs_file_write_keyval_record(f, key, keylen, value, vallen);
        if (ret == ZS_OK) return 1;
        else return 0;
}

int zs_packed_file_write_commit_record(struct zsdb_file *f)
{
        return zs_file_write_commit_record(f, 0);
}

int zs_packed_file_write_final_commit_record(struct zsdb_file *f)
{
        return zs_file_write_commit_record(f, 1);
}

/* zs_packed_file_open():
 * Open an existing packed file in read-only mode.
 */
int zs_packed_file_open(const char *path,
                        struct zsdb_file **fptr)
{
        int ret = ZS_OK;
        struct zsdb_file *f;
        size_t mf_size = 0;
        size_t offset;
        int mappedfile_flags = MAPPEDFILE_RD;

        f = xcalloc(sizeof(struct zsdb_file), 1);
        f->type = DB_FTYPE_PACKED;
        cstring_init(&f->fname, 0);
        cstring_addstr(&f->fname, path);

        /* Open the filename for use */
        ret = mappedfile_open(f->fname.buf,
                              mappedfile_flags, &f->mf);
        if (ret) {
                zslog(LOGDEBUG, "Could not open %s in read-only mode.\n",
                        f->fname.buf);
                ret = ZS_IOERROR;
                goto done;
        }

        f->is_open = 1;

        mappedfile_size(&f->mf, &mf_size);
        if (mf_size <= ZS_HDR_SIZE) {
                ret = ZS_INVALID_FILE;
                zslog(LOGDEBUG, "%s is not a valid packed file.\n",
                        f->fname.buf);
                goto fail;
        }

        if (zs_header_validate(f)) {
                zslog(LOGDEBUG, "Could not valid header in %s.\n",
                        f->fname.buf);
                ret = ZS_INVALID_DB;
                goto fail;
        }

        /* TODO:
         *  Initialise f->index;
         *  Seek to the end of file
         *   - go back 8 bytes, and check if there is a commit record,
         *     if there is one, then it is a short commit
         *   - if not a short commit go back 24 bytes from the end of file
         *     and check if there is a commit record, if there is one,
         *     then it is a long commit
         *   - else fail
         *
         *   If there is a valid commit (short or long):
         *          + verify commit
         *          + read commit record and get length of commit
         *          + go back to 'length' bytes to get the beginning of index
         *          + Read index into f->index
         */
        f->index = vecu64_new();

        /* Read the commit record and get to the pointers */
        offset = mf_size - ZS_SHORT_COMMIT_REC_SIZE;
        ret = get_offset_to_pointers(f, &offset);
        if (ret != ZS_OK) {
                zslog(LOGDEBUG, "Could not get pointer block.\n");
                goto fail;
        }

        /* Read the pointers section and get all the offsets */
        ret = read_pointers(f, offset);
        if (ret != ZS_OK) {
                zslog(LOGDEBUG, "Could not get pointers from pointer block.\n");
                goto fail;
        }

        f->indexpos = 0;
        f->priority = -1;

        *fptr = f;

        goto done;

fail:                           /* Jump here on failure */
        zs_packed_file_close(&f);
done:
        return ret;
}

int zs_packed_file_close(struct zsdb_file **fptr)
{
        int ret = ZS_OK;
        struct zsdb_file *f;

        if (fptr == NULL || *fptr == NULL)
                return ret;

        f = *fptr;
        fptr = NULL;

        mappedfile_close(&f->mf);
        cstring_release(&f->fname);
        vecu64_free(&f->index);
        xfree(f);

        return ret;
}

/* zs_packed_file_new():
 * Open a new file to be used as a packed file for writing.
 */
int zs_packed_file_new(const char *path _unused_,
                       uint32_t startidx _unused_, uint32_t endidx _unused_,
                       struct zsdb_file **fptr _unused_)
{
        int ret = ZS_OK;
        struct zsdb_file *f _unused_;
        size_t mf_size _unused_;
        int mappedfile_flags _unused_ = MAPPEDFILE_RW | MAPPEDFILE_CREATE;

        return ret;
}

/* zs_packed_file_new_from_memtree():
 * Create a new pack file (with sorted records and an index at the end
 * from all the finalised files(which are in memory).
 */
int zs_packed_file_new_from_memtree(const char *path,
                                    uint32_t startidx,
                                    uint32_t endidx,
                                    struct zsdb_priv *priv,
                                    struct zsdb_file **fptr)
{
        int ret = ZS_OK;
        struct zsdb_file *f;

        f = xcalloc(sizeof(struct zsdb_file), 1);
        f->type = DB_FTYPE_PACKED;
        cstring_init(&f->fname, 0);
        cstring_addstr(&f->fname, path);
        f->index = vecu64_new();

        /* Initialise header fields */
        f->header.signature = ZS_SIGNATURE;
        f->header.version = ZS_VERSION;
        memcpy(f->header.uuid, priv->uuid, sizeof(uuid_t));
        f->header.startidx = startidx;
        f->header.endidx = endidx;
        f->header.crc32 = 0;

        ret = mappedfile_open(f->fname.buf, MAPPEDFILE_RW_CR, &f->mf);
        if (ret) {
                ret = ZS_IOERROR;
                goto fail;
        }

        f->is_open = 1;

        /* Create the header */
        ret = zs_header_write(f);
        if (ret) {
                zslog(LOGDEBUG, "Could not write zeroskip header.\n");
                goto fail;
        }

        /* Start computing the crc32. Will end when the transaction is
           committed */
        crc32_begin(&f->mf);

        /* Seek to location after header */
        mappedfile_seek(&f->mf, ZS_HDR_SIZE, NULL);

        /* Write records into packed files */
        btree_walk_forward(priv->fmemtree,
                           zs_packed_file_write_btree_record,
                           (void *)f);

        ret = mappedfile_flush(&f->mf);
        if (ret) {
                zslog(LOGDEBUG, "Error flushing data to disk.\n");
                ret = ZS_IOERROR;
                goto fail;
        }

        /* The commit record marking the end of records */
        if (zs_packed_file_write_commit_record(f) != ZS_OK) {
                zslog(LOGDEBUG, "Could not commit.\n");
                ret = EXIT_FAILURE;
                goto fail;
        }

        /* Write the pointer/index section */
        crc32_begin(&f->mf);    /* The crc32 for index of the file */

        zs_packed_file_write_index_count(f, f->index->count); /* count */

        vecu64_foreach(f->index, zs_packed_file_write_index, f);

        /* The commit record for pointer section */
        if (zs_packed_file_write_final_commit_record(f) != ZS_OK) {
                zslog(LOGDEBUG, "Could not commit.\n");
                ret = EXIT_FAILURE;
                goto fail;
        }

        *fptr = f;

        goto done;
fail:
        xunlink(f->fname.buf);
        mappedfile_close(&f->mf);
        cstring_release(&f->fname);
        xfree(f);

done:
        return ret;
}

int zs_packed_file_get_key_from_offset(struct zsdb_file *f,
                                       unsigned char **key, uint64_t *len)
{
        uint64_t off;
        int ret;
        struct zs_key k;

        assert(f->indexpos <= f->index->count);

        off = f->index->data[f->indexpos];

        ret = zs_record_read_key_from_file_offset(f, off, &k);
        assert(ret == ZS_OK);   /* This should not be anything otherwise */

        if (k.base.type == REC_TYPE_KEY ||
            k.base.type == REC_TYPE_DELETED)
                *len = k.base.slen;
        else if (k.base.type == REC_TYPE_LONG_KEY ||
                 k.base.type == REC_TYPE_DELETED)
                *len = k.base.llen;

        *key = k.data;

        return ZS_OK;
}

int zs_pq_cmp_key_frm_offset(const void *d1, const void *d2, void *cbdata _unused_)
{
        struct zsdb_file *f1, *f2;
        uint64_t off1, off2;
        struct zs_key key1, key2;
        int ret;
        uint64_t len1, len2;

        f1 = (struct zsdb_file *)d1;
        f2 = (struct zsdb_file *)d2;

        assert(f1->indexpos <= f1->index->count);
        assert(f2->indexpos <= f2->index->count);

        off1 = f1->index->data[f1->indexpos];
        off2 = f2->index->data[f2->indexpos];

        /* asserts() should be ok here, since we should *not* have anything
         * apart from:
         * REC_TYPE_KEY|REC_TYPE_LONG_KEY|REC_TYPE_DELETED|REC_TYPE_LONG_DELE
         *
         * If the assertion fails, the DB is corrupted, and we have bigger
         * problems.
         */
        ret = zs_record_read_key_from_file_offset(f1, off1, &key1);
        assert(ret == ZS_OK);
        ret = zs_record_read_key_from_file_offset(f2, off2, &key2);
        assert(ret == ZS_OK);

        if (key1.base.type == REC_TYPE_KEY ||
            key1.base.type == REC_TYPE_DELETED)
                len1 = key1.base.slen;
        else if (key1.base.type == REC_TYPE_LONG_KEY ||
                 key1.base.type == REC_TYPE_DELETED)
                len1 = key1.base.llen;

        if (key2.base.type == REC_TYPE_KEY ||
            key2.base.type == REC_TYPE_DELETED)
                len2 = key2.base.slen;
        else if (key2.base.type == REC_TYPE_LONG_KEY ||
                 key2.base.type == REC_TYPE_DELETED)
                len2 = key2.base.llen;

        return memcmp_raw(key1.data, len1, key2.data, len2);
}

int zs_packed_file_bsearch_index(const unsigned char *key, const size_t keylen,
                                 struct zsdb_file *f, uint64_t *location,
                                 unsigned char **value, size_t *vallen)
{
        uint64_t hi, lo;

        lo = 0;
        hi = f->index->count;

        while (lo < hi) {
                uint64_t mi;
                struct zs_key k;
                struct zs_val v;
                size_t klen = 0;
                int res;
                size_t offset = 0;

                /* compute the mid */
                mi = lo + (hi - lo) / 2;

                /* Get key from file */
                offset = f->index->data[mi];

                res = zs_read_key_val_record_from_file_offset(f,
                                                   &offset,
                                                   &k, &v);
                assert(res == ZS_OK);

                /* Compare */
                if (k.base.type == REC_TYPE_KEY ||
                    k.base.type == REC_TYPE_DELETED) {
                        klen = k.base.slen;
                } else if (k.base.type == REC_TYPE_LONG_KEY ||
                    k.base.type == REC_TYPE_LONG_DELETED) {
                        klen = k.base.llen;
                }

                res = memcmp_raw(key, keylen, k.data, klen);
                if (!res) {
                        /* If found, `location` will contain the
                           offset at which the element can be found.
                         */
                        if (location)
                                *location = mi;
                        if (value) {
                                *vallen = (v.base.type == REC_TYPE_VALUE) ?
                                        v.base.slen : v.base.llen;
                                *value = xmalloc(*vallen);
                                memcpy(*value, v.data, *vallen);
                        }
                        return 1; /* FOUND */
                }

                if (res > 0)
                        lo = mi + 1;
                else
                        hi = mi;
        }

        /* If the element isn't found, then we return the least
         * entry that is closest(greater) than the `key` we are
         * given
         */
        if (location)
                *location = lo;

        return 0;               /* NOT FOUND */
}

int zs_packed_file_new_from_packed_files(const char *path,
                                         uint32_t startidx,
                                         uint32_t endidx,
                                         struct zsdb_priv *priv,
                                         struct list_head *flist,
                                         struct txn **txn,
                                         struct zsdb_file **fptr)
{
        int ret = ZS_OK;
        struct zsdb_file *f;
        struct txn_data *data;
        int count = 0;

        f = xcalloc(sizeof(struct zsdb_file), 1);
        f->type = DB_FTYPE_PACKED;
        cstring_init(&f->fname, 0);
        cstring_addstr(&f->fname, path);
        f->index = vecu64_new();

        /* Initialise header fields */
        f->header.signature = ZS_SIGNATURE;
        f->header.version = ZS_VERSION;
        memcpy(f->header.uuid, priv->uuid, sizeof(uuid_t));
        f->header.startidx = startidx;
        f->header.endidx = endidx;
        f->header.crc32 = 0;

        ret = mappedfile_open(f->fname.buf, MAPPEDFILE_RW_CR, &f->mf);
        if (ret) {
                ret = ZS_IOERROR;
                goto fail;
        }

        f->is_open = 1;

        /* Create the header */
        ret = zs_header_write(f);
        if (ret) {
                zslog(LOGDEBUG, "Could not write zeroskip header.\n");
                goto fail;
        }

        /* Start computing the crc32. Will end when the transaction is
           committed */
        crc32_begin(&f->mf);

        /* Seek to location after header */
        mappedfile_seek(&f->mf, ZS_HDR_SIZE, NULL);

        ret = zs_transaction_begin_for_packed_flist(txn, flist);
        if (ret != ZS_OK) {
                zslog(LOGWARNING, "Failed to begin transaction!\n");
                goto fail;
        }

        do {
                data = zs_transaction_get(*txn);
                switch(data->type) {
                case ZSDB_BE_PACKED:
                {
                        struct zsdb_file *tempf = data->data.f;
                        size_t offset = tempf->index->data[tempf->indexpos];
                        zs_record_read_from_file(tempf, &offset,
                                                 zs_packed_file_write_record,
                                                 (void *)f);
                        break;
                }
                case ZSDB_BE_ACTIVE:
                case ZSDB_BE_FINALISED:
                default:
                        abort();  /* Should never reach here */
                        break;
                }

                count++;
        } while (zs_transaction_next(*txn, data));

        ret = mappedfile_flush(&f->mf);
        if (ret) {
                zslog(LOGDEBUG, "Error flushing data to disk.\n");
                ret = ZS_IOERROR;
                goto fail;
        }

        /* The commit record marking the end of records */
        if (zs_packed_file_write_commit_record(f) != ZS_OK) {
                zslog(LOGDEBUG, "Could not commit.\n");
                ret = EXIT_FAILURE;
                goto fail;
        }

        /* Write the pointer/index section */
        crc32_begin(&f->mf);    /* The crc32 for index of the file */

        zs_packed_file_write_index_count(f, f->index->count); /* count */

        vecu64_foreach(f->index, zs_packed_file_write_index, f);

        /* The commit record for pointer section */
        if (zs_packed_file_write_final_commit_record(f) != ZS_OK) {
                zslog(LOGDEBUG, "Could not commit.\n");
                ret = EXIT_FAILURE;
                goto fail;
        }

        *fptr = f;

        goto done;
fail:
        xunlink(f->fname.buf);
        mappedfile_close(&f->mf);
        cstring_release(&f->fname);
        xfree(f);

done:
        return ret;
}
