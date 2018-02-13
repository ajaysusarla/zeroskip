/*
 * zeroskip-active.c
 *
 * This file is part of zeroskip.
 *
 * zeroskip is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 */

#include "log.h"
#include "util.h"
#include "zeroskip.h"
#include "zeroskip-priv.h"

#include <zlib.h>

/*
 * Private functions
 */
/* Caller should free buf
 */
static int zs_prepare_key_buf(unsigned char *key, size_t keylen,
                              unsigned char **buf, size_t *buflen)
{
        int ret = ZS_OK;
        unsigned char *kbuf;
        size_t kbuflen, finalkeylen, pos = 0;
        enum record_t type;

        kbuflen = ZS_KEY_BASE_REC_SIZE;
        type = REC_TYPE_KEY;

        if (keylen > MAX_SHORT_KEY_LEN)
                type |= REC_TYPE_LONG;

        /* Minimum buf size */
        finalkeylen = roundup64bits(keylen);
        kbuflen += finalkeylen;

        kbuf = xcalloc(1, kbuflen);

        if (type == REC_TYPE_KEY) {
                /* If it is a short key, the first 3 fields make up 64 bits */
                uint64_t val;
                val = ((uint64_t)kbuflen & ((1ULL << 40) - 1)); /* Val offset */
                val |= ((uint64_t)keylen << 40);     /* Key length */
                val |= ((uint64_t)type << 56);       /* Type */
                write_be64(kbuf + pos, val);
                pos += sizeof(uint64_t);
                write_be64(kbuf + pos, 0ULL);     /* Extended length */
                pos += sizeof(uint64_t);
                write_be64(kbuf + pos, 0ULL);     /* Extended Value offset */
                pos += sizeof(uint64_t);

        } else {
                /* A long key has the type followed by 56 bits of nothing */
                uint64_t val;
                val = ((uint64_t)type & ((1ULL << 56) - 1));
                write_be64(kbuf + pos, val);
                pos += sizeof(uint64_t);
                write_be64(kbuf + pos, keylen);     /* Extended length */
                pos += sizeof(uint64_t);
                write_be64(kbuf + pos, kbuflen);    /* Extended Value offset */
                pos += sizeof(uint64_t);
        }

        /* the key */
        memcpy(kbuf + pos, key, keylen);
        pos += keylen;

        *buflen = kbuflen;
        *buf = kbuf;

        return ret;
}

/* Caller should free buf
 */
static int zs_prepare_val_buf(unsigned char *val, size_t vallen,
                              unsigned char **buf, size_t *buflen)
{
        int ret = ZS_OK;
        unsigned char *vbuf;
        size_t vbuflen, finalvallen, pos = 0;
        enum record_t type;

        vbuflen = ZS_VAL_BASE_REC_SIZE;
        type = REC_TYPE_VALUE;

        if (vallen > MAX_SHORT_VAL_LEN)
                type |= REC_TYPE_LONG;

        /* Minimum buf size */
        finalvallen = roundup64bits(vallen);

        vbuflen += finalvallen;

        vbuf = xcalloc(1, vbuflen);

        if (type == REC_TYPE_VALUE) {
                /* The first 3 fields in a short key make up 64 bits */
                uint64_t val = 0;
                val = ((uint64_t)vallen & ((1UL << 32) - 1));  /* Val length */
                val |= ((uint64_t)type << 56);                 /* Type */
                write_be64(vbuf + pos, val);
                pos += sizeof(uint64_t);
                write_be64(vbuf + pos, 0ULL);     /* Extended length */
                pos += sizeof(uint64_t);
        } else {
                /* A long val has the type followed by 56 bits of nothing */
                uint64_t val;
                val = ((uint64_t)type & ((1UL << 56) - 1));
                write_be64(vbuf + pos, val);
                pos += sizeof(uint64_t);
                write_be64(vbuf + pos, vallen);
                pos += sizeof(uint64_t);
        }

        /* the value */
        memcpy(vbuf + pos, val, vallen);

        *buflen = vbuflen;
        *buf = vbuf;

        return ret;
}

/* Caller should free buf
 */
static int zs_prepare_delete_key_buf(unsigned char *key, size_t keylen,
                                     unsigned char **buf, size_t *buflen)
{
        int ret = ZS_OK;
        unsigned char *kbuf;
        size_t kbuflen, finalkeylen, pos = 0;
        uint64_t val;
        enum record_t type = REC_TYPE_DELETED;

        kbuflen = ZS_KEY_BASE_REC_SIZE;

        /* Minimum buf size */
        finalkeylen = roundup64bits(keylen);
        kbuflen += finalkeylen;

        kbuf = xcalloc(1, kbuflen);

        if (keylen <= MAX_SHORT_KEY_LEN) {
                val = ((uint64_t)0ULL & ((1ULL << 40) - 1));
                val |= ((uint64_t)keylen << 40);     /* Key length */
                val |= ((uint64_t)type << 56);       /* Type */
                write_be64(kbuf + pos, val);
                pos += sizeof(uint64_t);
                write_be64(kbuf + pos, 0ULL);     /* Extended length */
                pos += sizeof(uint64_t);
                write_be64(kbuf + pos, 0ULL);     /* Extended Value offset */
                pos += sizeof(uint64_t);
        } else {
                type = REC_TYPE_LONG_DELETED;
                val = ((uint64_t)type & ((1ULL << 56) - 1));
                pos += sizeof(uint64_t);
                write_be64(kbuf + pos, keylen);     /* Extended length */
                pos += sizeof(uint64_t);
                write_be64(kbuf + pos, 0ULL);       /* Extended Value offset */
                pos += sizeof(uint64_t);
        }

        /* the key */
        memcpy(kbuf + pos, key, keylen);
        pos += keylen;

        *buflen = kbuflen;
        *buf = kbuf;

        return ret;
}

/*
 * Public functions
 */
/* FIXME: Make `int create`, the 3rd argument to this function
   (active_file_open an enum or something more meaninful
*/
int zs_active_file_open(struct zsdb_priv *priv, uint32_t idx, int create)
{
        int ret = ZS_OK;
        size_t mf_size;
        int mappedfile_flags = MAPPEDFILE_RW;

        zs_filename_generate_active(priv, &priv->factive.fname);

        /* Initialise the header fields */
        priv->factive.header.signature = ZS_SIGNATURE;
        priv->factive.header.version = ZS_VERSION;
        priv->factive.header.startidx = idx;
        priv->factive.header.endidx = idx;
        priv->factive.header.crc32 = 0;

        if (create)
                mappedfile_flags |= MAPPEDFILE_CREATE;

        /* Open the active filename for use */
        ret = mappedfile_open(priv->factive.fname.buf,
                              mappedfile_flags, &priv->factive.mf);
        if (ret) {
                ret = ZS_IOERROR;
                goto done;
        }

        priv->factive.is_open = 1;

        mappedfile_size(&priv->factive.mf, &mf_size);
        /* The filesize is zero, it is a new file. */
        if (mf_size == 0) {
                ret = zs_header_write(&priv->factive);
                if (ret) {
                        zslog(LOGDEBUG, "Could not write zeroskip header.\n");
                        mappedfile_close(&priv->factive.mf);
                        goto done;
                }
        }

        if (zs_header_validate(&priv->factive)) {
                ret = ZS_INVALID_DB;
                mappedfile_close(&priv->factive.mf);
                goto done;
        }

        /* Seek to location after header */
        mf_size = ZS_HDR_SIZE;
        mappedfile_seek(&priv->factive.mf, mf_size, NULL);
done:
        return ret;
}

int zs_active_file_close(struct zsdb_priv *priv)
{
        int ret = ZS_OK;

        if (!priv->factive.is_open)
                return ZS_ERROR;

        if (priv->factive.dirty) {
                ret = zs_active_file_write_commit_record(priv);
                if (ret == ZS_OK)
                        priv->factive.dirty = 0;
        }

        mappedfile_flush(&priv->factive.mf);
        mappedfile_close(&priv->factive.mf);

        cstring_release(&priv->factive.fname);

        priv->factive.is_open = 0;

        return ret;
}

int zs_active_file_finalise(struct zsdb_priv *priv _unused_)
{
        int ret = ZS_OK;
        cstring newfname = CSTRING_INIT;
        char index[11] = { 0 };

        if (!file_lock_is_locked(&priv->wlk)) {
                zslog(LOGDEBUG, "Need a write lock to finalise.\n");
                ret = ZS_ERROR;
                goto done;
        }

        ret = zs_active_file_write_commit_record(priv);
        if (ret != ZS_OK)
                goto done;

        mappedfile_flush(&priv->factive.mf);
        mappedfile_close(&priv->factive.mf);


        /* Rename the current active db file */
        cstring_dup(&priv->factive.fname, &newfname);
        cstring_addch(&newfname, '-');
        snprintf(index, 10, "%d", priv->dotzsdb.curidx);
        cstring_addstr(&newfname, index);
        if (rename(priv->factive.fname.buf, newfname.buf) < 0) {
                ret = ZS_INTERNAL;
                perror("Rename");
                goto done;
        }
        cstring_release(&newfname);

        priv->factive.is_open = 0;
done:
        return ret;
}

int zs_active_file_new(struct zsdb_priv *priv, uint32_t idx)
{
        int ret = ZS_OK;
        size_t mfsize;
        int mappedfile_flags = MAPPEDFILE_RW | MAPPEDFILE_CREATE;

        /* Update the index */
        zs_dotzsdb_update_index(priv, idx);
        zs_filename_generate_active(priv, &priv->factive.fname);

        /* Initialise the header fields of factive */
        priv->factive.header.signature = ZS_SIGNATURE;
        priv->factive.header.version = ZS_VERSION;
        priv->factive.header.startidx = idx;
        priv->factive.header.endidx = idx;
        priv->factive.header.crc32 = 0;

       /* Open the active filename for use */
        ret = mappedfile_open(priv->factive.fname.buf,
                              mappedfile_flags, &priv->factive.mf);
        if (ret) {
                ret = ZS_IOERROR;
                goto done;
        }

        priv->factive.is_open = 1;

        mappedfile_size(&priv->factive.mf, &mfsize);
        /* The filesize is zero, it is a new file. */
        if (mfsize == 0) {
                ret = zs_header_write(&priv->factive);
                if (ret) {
                        zslog(LOGDEBUG, "Could not write zeroskip header.\n");
                        mappedfile_close(&priv->factive.mf);
                        goto done;
                }
        }

        priv->factive.is_open = 1;

        if (zs_header_validate(&priv->factive)) {
                ret = ZS_INVALID_DB;
                mappedfile_close(&priv->factive.mf);
                goto done;
        }

        /* Seek to location after header */
        mfsize = ZS_HDR_SIZE;
        mappedfile_seek(&priv->factive.mf, mfsize, NULL);
done:
        return ret;
}

int zs_active_file_write_keyval_record(struct zsdb_priv *priv,
                                       unsigned char *key, size_t keylen,
                                       unsigned char *val, size_t vallen)
{
        int ret = ZS_OK;
        size_t keybuflen, valbuflen;
        unsigned char *keybuf, *valbuf;
        size_t mfsize, nbytes;

        if (!priv->factive.is_open)
                return ZS_NOT_OPEN;

        ret = zs_prepare_key_buf(key, keylen, &keybuf, &keybuflen);
        if (ret != ZS_OK) {
                return ZS_IOERROR;
        }

        ret = zs_prepare_val_buf(val, vallen, &valbuf, &valbuflen);
        if (ret != ZS_OK) {
                return ZS_IOERROR;
        }

        /* Get the current mappedfile size */
        ret = mappedfile_size(&priv->factive.mf, &mfsize);
        if (ret) {
                zslog(LOGDEBUG, "Could not get mappedfile size\n");
                goto done;
        }

        /* write key buffer */
        ret = mappedfile_write(&priv->factive.mf, (void *)keybuf, keybuflen, &nbytes);
        if (ret) {
                zslog(LOGDEBUG, "Error writing key\n");
                ret = ZS_IOERROR;
                goto done;
        }

        /* assert(nbytes == keybuflen); */

        /* write value buffer */
        ret = mappedfile_write(&priv->factive.mf, (void *)valbuf, valbuflen, &nbytes);
        if (ret) {
                zslog(LOGDEBUG, "Error writing key\n");
                ret = ZS_IOERROR;
                goto done;
        }

        /* assert(nbytes == valbuflen); */

        /* If we failed writing the value buffer, then restore the db file to
         * the original size we had before updating */
        if (ret != ZS_OK) {
                mappedfile_truncate(&priv->factive.mf, mfsize);
        }

        /* Flush the change to disk */
        ret = mappedfile_flush(&priv->factive.mf);
        if (ret) {
                zslog(LOGDEBUG, "Error flushing data to disk.\n");
                ret = ZS_IOERROR;
                goto done;
        }

done:
        xfree(keybuf);
        xfree(valbuf);

        return ret;
}

int zs_active_file_write_commit_record(struct zsdb_priv *priv)
{
        int ret = ZS_OK;
        size_t buflen, nbytes, pos = 0;
        unsigned char buf[24], *ptr;
        uint32_t crc;

        if (!priv->factive.is_open)
                return ZS_INTERNAL;

        memset(&buf, 0, sizeof(buf));
        ptr = buf;

        if (priv->factive.mf->crc32_data_len > MAX_SHORT_VAL_LEN) {
                /* Long commit */
                uint32_t lccrc;
                uint64_t val = 0;
                struct zs_long_commit lc;

                lc.type1 = REC_TYPE_LONG_COMMIT;
                lc.length = priv->factive.mf->crc32_data_len;
                lc.type2 = REC_TYPE_LONG_COMMIT;

                /* Compute CRC32 */
                lccrc = crc32(0L, Z_NULL, 0);
                lccrc = crc32(lccrc, (void *)&lc,
                              sizeof(struct zs_long_commit) - sizeof(uint32_t));
                crc = crc32_end(&priv->factive.mf);
                lc.crc32 = crc32_combine(crc, lccrc, sizeof(uint32_t));

                /* Create long commit */
                val = ((uint64_t)lc.type1 & ((1ULL << 56) - 1));
                write_be64(ptr + pos, val);
                pos += sizeof(uint64_t);

                write_be64(ptr + pos, lc.length);
                pos += sizeof(uint64_t);

                val = 0;
                val = ((uint64_t)lc.crc32 & ((1UL << 32) - 1)); /* crc */
                val |= ((uint64_t)lc.type2 << 56);               /* type */
                write_be64(ptr + pos, val);
                pos += sizeof(uint64_t);

                buflen = ZS_LONG_COMMIT_REC_SIZE;
        } else {
                /* Short commit */
                uint64_t val = 0;
                uint32_t sccrc;
                struct zs_short_commit sc;

                sc.type = REC_TYPE_COMMIT;
                sc.length = priv->factive.mf->crc32_data_len;

                /* Compute CRC32 */
                sccrc = crc32(0L, Z_NULL, 0);
                sccrc = crc32(sccrc, (void *)&sc,
                              sizeof(struct zs_short_commit) - sizeof(uint32_t));
                crc = crc32_end(&priv->factive.mf);
                sc.crc32 = crc32_combine(crc, sccrc, sizeof(uint32_t));

                val = ((uint64_t)sc.crc32 & ((1UL << 32) - 1)); /* crc */
                val |= ((uint64_t)sc.length << 32);             /* length */
                val |= ((uint64_t)sc.type << 56);               /* type */
                write_be64(ptr + pos, val);
                pos += sizeof(uint64_t);

                buflen = ZS_SHORT_COMMIT_REC_SIZE;
        }

        ret = mappedfile_write(&priv->factive.mf, (void *)buf, buflen, &nbytes);
        if (ret) {
                zslog(LOGDEBUG, "Error writing commit record.\n");
                ret = ZS_IOERROR;
                goto done;
        }

        /* assert(nbytes == buflen); */

        /* Flush the change to disk */
        ret = mappedfile_flush(&priv->factive.mf);
        if (ret) {
                zslog(LOGDEBUG, "Error flushing commit record to disk.\n");
                ret = ZS_IOERROR;
                goto done;
        }

done:
        return ret;
}

int zs_active_file_write_delete_record(struct zsdb_priv *priv,
                                       unsigned char *key,
                                       size_t keylen)
{
        int ret = ZS_OK;
        unsigned char *dbuf;
        size_t dbuflen, mfsize, nbytes;

        ret = zs_prepare_delete_key_buf(key, keylen, &dbuf, &dbuflen);
        if (ret != ZS_OK) {
                return ZS_IOERROR;
        }

        /* Get the current mappedfile size */
        ret = mappedfile_size(&priv->factive.mf, &mfsize);
        if (ret) {
                zslog(LOGDEBUG, "delete: Could not get mappedfile size\n");
                goto done;
        }

        /* write delete buffer */
        ret = mappedfile_write(&priv->factive.mf, (void *)dbuf, dbuflen, &nbytes);
        if (ret) {
                zslog(LOGDEBUG, "Error writing delete key\n");
                ret = ZS_IOERROR;
                goto done;
        }

        /* assert(nbytes == keybuflen); */

        /* If we failed writing the delete buffer, then restore the db file to
         * the original size we had before updating */
        if (ret != ZS_OK) {
                mappedfile_truncate(&priv->factive.mf, mfsize);
        }

        /* Flush the change to disk */
        ret = mappedfile_flush(&priv->factive.mf);
        if (ret) {
                zslog(LOGDEBUG, "delete: Error flushing data to disk.\n");
                ret = ZS_IOERROR;
                goto done;
        }

done:
        xfree(dbuf);
        return ret;
}


int zs_active_file_record_foreach(struct zsdb_priv *priv,
                                  foreach_cb *cb, void *cbdata)
{
        int ret = ZS_OK;
        size_t dbsize = 0, offset = ZS_HDR_SIZE;

        mappedfile_size(&priv->factive.mf, &dbsize);
        if (dbsize == 0 || dbsize < ZS_HDR_SIZE) {
                zslog(LOGDEBUG, "Not a valid active file.\n");
                return ZS_INVALID_DB;
        } else if (dbsize == ZS_HDR_SIZE) {
                zslog(LOGDEBUG, "No records in active file.\n");
                return ret;
        }

        while (offset < dbsize) {
                ret = zs_record_read_from_file(&priv->factive, &offset,
                                               cb, cbdata);
        }

        return ret;
}

