/*
 * zeroskip-active.c
 *
 * This file is part of zeroskip.
 *
 * zeroskip is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 */

#include <libzeroskip/log.h>
#include <libzeroskip/mfile.h>
#include <libzeroskip/util.h>
#include "zeroskip-priv.h"

#include <libzeroskip/zeroskip.h>
#include <zlib.h>

/*
 * Private functions
 */

/*
 * Public functions
 */
/* FIXME: Make `int create`, the 3rd argument to this function
   (active_file_open an enum or something more meaningful
*/
int zs_active_file_open(struct zsdb_priv *priv, uint32_t idx, int create)
{
        int ret = ZS_OK;
        size_t mf_size = 0;
        int mfile_flags = MFILE_RW;

        zs_filename_generate_active(priv, &priv->dbfiles.factive.fname);

        /* Initialise the header fields */
        priv->dbfiles.factive.header.signature = ZS_SIGNATURE;
        priv->dbfiles.factive.header.version = ZS_VERSION;
        priv->dbfiles.factive.header.startidx = idx;
        priv->dbfiles.factive.header.endidx = idx;
        priv->dbfiles.factive.header.crc32 = 0;
        memcpy(priv->dbfiles.factive.header.uuid, priv->uuid, sizeof(uuid_t));

        if (create)
                mfile_flags |= MFILE_CREATE;

        /* Open the active filename for use */
        ret = mfile_open(priv->dbfiles.factive.fname.buf,
                              mfile_flags, &priv->dbfiles.factive.mf);
        if (ret) {
                ret = ZS_IOERROR;
                goto done;
        }

        priv->dbfiles.factive.is_open = 1;

        mf_size = priv->dbfiles.factive.mf->size;
        /* The filesize is zero, it is a new file. */
        if (mf_size == 0) {
                ret = zs_header_write(&priv->dbfiles.factive);
                if (ret) {
                        zslog(LOGDEBUG, "Could not write zeroskip header.\n");
                        mfile_close(&priv->dbfiles.factive.mf);
                        goto done;
                }
        }

        if (zs_header_validate(&priv->dbfiles.factive)) {
                ret = ZS_INVALID_DB;
                mfile_close(&priv->dbfiles.factive.mf);
                goto done;
        }

        /* Seek to location after header */
        mf_size = ZS_HDR_SIZE;
        mfile_seek(&priv->dbfiles.factive.mf, mf_size, NULL);
done:
        return ret;
}

int zs_active_file_close(struct zsdb_priv *priv)
{
        int ret = ZS_OK;

        if (!priv->dbfiles.factive.is_open)
                return ZS_ERROR;

        if (priv->dbfiles.factive.dirty) {
                /* XXX: If not committed, just ignore. */
                ret = zs_active_file_write_commit_record(priv);
                if (ret == ZS_OK)
                        priv->dbfiles.factive.dirty = 0;
        }

        mfile_close(&priv->dbfiles.factive.mf);

        cstring_release(&priv->dbfiles.factive.fname);

        priv->dbfiles.factive.is_open = 0;
        priv->dbfiles.afcount = 0;

        return ret;
}

int zs_active_file_finalise(struct zsdb_priv *priv, cstring *fname)
{
        int ret = ZS_OK;
        cstring newfname = CSTRING_INIT;
        char index[11];
        size_t siz;

        siz = sizeof(index);

        memset(index, 0, siz);

        if (!file_lock_is_locked(&priv->wlk)) {
                zslog(LOGDEBUG, "Need a write lock to finalise.\n");
                ret = ZS_ERROR;
                goto done;
        }

        /* Commit anything uncommitted. With nothing pending, a commit
         * record would carry a stale CRC and fail to verify. */
        if (priv->dbfiles.factive.dirty ||
            priv->dbfiles.factive.mf->crc32_data_len) {
                ret = zs_active_file_write_commit_record(priv);
                if (ret != ZS_OK)
                        goto done;
                priv->dbfiles.factive.dirty = 0;
        }

        mfile_flush(&priv->dbfiles.factive.mf);
        mfile_close(&priv->dbfiles.factive.mf);


        /* Rename the current active db file */
        cstring_dup(&priv->dbfiles.factive.fname, &newfname);
        cstring_addch(&newfname, '-');
        snprintf(index, siz, "%d", priv->dotzsdb.curidx);
        cstring_addstr(&newfname, index);
        if (rename(priv->dbfiles.factive.fname.buf, newfname.buf) < 0) {
                ret = ZS_INTERNAL;
                perror("Rename");
                goto done;
        }
        if (fname)
                cstring_dup(&newfname, fname);
        cstring_release(&newfname);

        priv->dbfiles.factive.is_open = 0;
done:
        return ret;
}

int zs_active_file_new(struct zsdb_priv *priv, uint32_t idx)
{
        int ret = ZS_OK;
        size_t mfsize = 0;
        int mfile_flags = MFILE_RW | MFILE_CREATE;

        /* Update the index and offset in .zsdb */
        zs_dotzsdb_update_index_and_offset(priv, idx, ZS_HDR_SIZE);

        zs_filename_generate_active(priv, &priv->dbfiles.factive.fname);

        /* Initialise the header fields of factive */
        priv->dbfiles.factive.header.signature = ZS_SIGNATURE;
        priv->dbfiles.factive.header.version = ZS_VERSION;
        priv->dbfiles.factive.header.startidx = idx;
        priv->dbfiles.factive.header.endidx = idx;
        priv->dbfiles.factive.header.crc32 = 0;

       /* Open the active filename for use */
        ret = mfile_open(priv->dbfiles.factive.fname.buf,
                              mfile_flags, &priv->dbfiles.factive.mf);
        if (ret) {
                ret = ZS_IOERROR;
                goto done;
        }

        priv->dbfiles.factive.is_open = 1;

        mfsize = priv->dbfiles.factive.mf->size;
        /* The filesize is zero, it is a new file. */
        if (mfsize == 0) {
                ret = zs_header_write(&priv->dbfiles.factive);
                if (ret) {
                        zslog(LOGDEBUG, "Could not write zeroskip header.\n");
                        mfile_close(&priv->dbfiles.factive.mf);
                        goto done;
                }
        }

        priv->dbfiles.factive.is_open = 1;

        if (zs_header_validate(&priv->dbfiles.factive)) {
                ret = ZS_INVALID_DB;
                mfile_close(&priv->dbfiles.factive.mf);
                goto done;
        }

        /* Seek to location after header */
        mfsize = ZS_HDR_SIZE;
        mfile_seek(&priv->dbfiles.factive.mf, mfsize, NULL);
done:
        return ret;
}

int zs_active_file_write_keyval_record(struct zsdb_priv *priv,
                                       const unsigned char *key, uint64_t keylen,
                                       const unsigned char *val, uint64_t vallen)
{
        return zs_file_write_keyval_record(&priv->dbfiles.factive,
                                           key, keylen,
                                           val, vallen);
}

int zs_active_file_write_commit_record(struct zsdb_priv *priv)
{
        return zs_file_write_commit_record(&priv->dbfiles.factive, 0);
}

int zs_active_file_write_delete_record(struct zsdb_priv *priv,
                                       const unsigned char *key,
                                       uint64_t keylen)
{
        return zs_file_write_delete_record(&priv->dbfiles.factive,
                                           key, keylen);
}

int zs_active_file_record_foreach(struct zsdb_priv *priv,
                                  zsdb_foreach_cb *cb, zsdb_foreach_cb *deleted_cb,
                                  void *cbdata)
{
        int ret = ZS_OK;
        size_t dbsize = 0, offset = ZS_HDR_SIZE;

        mfile_size(&priv->dbfiles.factive.mf, &dbsize);
        if (dbsize == 0 || dbsize < ZS_HDR_SIZE) {
                zslog(LOGDEBUG, "Not a valid active file.\n");
                return ZS_INVALID_DB;
        } else if (dbsize == ZS_HDR_SIZE) {
                zslog(LOGDEBUG, "No records in active file.\n");
                return ret;
        }

        while (offset < dbsize) {
                ret = zs_record_read_from_file(&priv->dbfiles.factive, &offset,
                                               cb, deleted_cb, cbdata);
                if (ret != ZS_OK) {
                        zslog(LOGWARNING, "Cannot read records from active file!\n");
                        break;
                }
        }

        return ret;
}

