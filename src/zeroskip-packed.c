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
#include "vecu64.h"
#include "util.h"
#include "zeroskip.h"
#include "zeroskip-priv.h"

/**
 * Private functions
 */
static int zs_packed_file_write_index(void *data _unused_, uint64_t offset)
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

/**
 * Public functions
 */

int zs_packed_file_write_record(struct record *record, void *data)
{
        struct zsdb_file *f = (struct zsdb_file *)data;
        int ret = ZS_OK;

        vecu64_append(f->index, f->mf->offset);
        ret = zs_file_write_keyval_record(f, record->key, record->keylen,
                                          record->val, record->vallen);
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
        size_t mf_size;
        int mappedfile_flags = MAPPEDFILE_RD;

        f = xcalloc(sizeof(struct zsdb_file), 1);
        f->type = DB_FTYPE_FINALISED;
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

       /* Seek to location after header */
        mf_size = ZS_HDR_SIZE;
        mappedfile_seek(&f->mf, mf_size, NULL);

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

        *fptr = f;

        goto done;

fail:                           /* Jump here on failure */
        mappedfile_close(&f->mf);
        cstring_release(&f->fname);
        xfree(f);

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
int zs_packed_file_new_from_memtree(const char * path,
                                    uint32_t startidx,
                                    uint32_t endidx,
                                    struct zsdb_priv *priv,
                                    struct zsdb_file **fptr)
{
        int ret = ZS_OK;
        struct zsdb_file *f;

        f = xcalloc(sizeof(struct zsdb_file), 1);
        f->type = DB_FTYPE_FINALISED;
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
        btree_walk_forward(priv->fmemtree, zs_packed_file_write_record,
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

        vecu64_foreach(f->index, zs_packed_file_write_index, f);

        /* The commit record for pointer section */
        if (zs_packed_file_write_commit_record(f) != ZS_OK) {
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
