/*
 * zeroskip-priv.h
 *
 * This file is part of zeroskip.
 *
 * zeroskip is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 *
 */

#ifndef _ZEROSKIP_PRIV_H_
#define _ZEROSKIP_PRIV_H_

#include "file-lock.h"
#include "htable.h"
#include "list.h"
#include "pqueue.h"

#include <libzeroskip/memtree.h>
#include <libzeroskip/cstring.h>
#include <libzeroskip/macros.h>
#include <libzeroskip/mfile.h>
#include <libzeroskip/util.h>
#include <libzeroskip/vecu64.h>
#include <libzeroskip/zeroskip.h>

#include <sys/stat.h>
#include <uuid/uuid.h>

CPP_GUARD_START

/* Sizes */
#define MB       (1 << 20)
#define TWOMB    (2 << 20)
#define THREEMB  (3 << 20)
#define FOURMB   (4 << 20)

/*
 * Zeroskip db files have the following file naming scheme:
 *   zeroskip-$(UUID)-$(index)                     - for an unpacked file
 *   zeroskip-$(UUID)-$(startindex)-$(endindex)    - for a packed filed
 *
 * The UUID, startindex and endindex values are in the header of each file.
 * The index starts with a 0, for a completely new Zeroskip DB. And is
 * incremented every time a file is finalsed(packed).
 */
#define ZS_FNAME_PREFIX       "zeroskip-"
#define ZS_FNAME_PREFIX_LEN   9
#define ZS_SIGNATURE          0x5a45524f534b4950 /* "ZEROSKIP" */
#define ZS_VERSION            1

/* This is the size of the unparssed uuid string */
#define UUID_STRLEN  37

/**
 * zeroskip header.
 */
/* Header offsets */
enum {
        ZS_HDR      = 0,
        ZS_VER      = 8,
        ZS_UUID     = 12,
        ZS_SIDX     = 28,
        ZS_EIDX     = 32,
        ZS_CRC      = 36,
};

struct zs_header {
        uint64_t signature;         /* Signature */
        uint32_t version;           /* Version Number */
        uuid_t   uuid;              /* UUID of DB - 128 bits: unsigned char uuid_t[16];*/
        uint32_t startidx;          /* Start Index of DB range */
        uint32_t endidx;            /* End Index of DB range */
        uint32_t crc32;             /* CRC32 of rest of header */
};

#define ZS_HDR_SIZE      sizeof(struct zs_header)


/**
 * Zeroskip .zsdb
 */
struct _packed_ dotzsdb{
        uint64_t signature;
        uint64_t offset;
        char uuidstr[37];
        uint32_t curidx;
        uint32_t crc;
};
#define DOTZSDB_FNAME ".zsdb"
#define DOTZSDB_SIZE  sizeof(struct dotzsdb)



/* Types of files in the DB */
enum db_ftype_t {
        DB_FTYPE_ACTIVE,
        DB_FTYPE_FINALISED,
        DB_FTYPE_PACKED,
        DB_FTYPE_UNKNOWN,
};

/**
 * Zeroskip record[key|value|commit]
 */
enum record_t {
        REC_TYPE_UNUSED              =  0,
        REC_TYPE_KEY                 =  1,
        REC_TYPE_VALUE               =  2,
        REC_TYPE_COMMIT              =  4,
        REC_TYPE_2ND_HALF_COMMIT     =  8,
        REC_TYPE_FINAL               = 16,
        REC_TYPE_LONG                = 32,
        REC_TYPE_DELETED             = 64,
        REC_TYPE_LONG_KEY            = REC_TYPE_KEY | REC_TYPE_LONG,
        REC_TYPE_LONG_VALUE          = REC_TYPE_VALUE | REC_TYPE_LONG,
        REC_TYPE_LONG_COMMIT         = REC_TYPE_COMMIT | REC_TYPE_LONG,
        REC_TYPE_LONG_FINAL          = REC_TYPE_FINAL | REC_TYPE_LONG,
        REC_TYPE_LONG_DELETED        = REC_TYPE_DELETED | REC_TYPE_LONG,
};

struct zs_key_base {
        uint8_t  type;
        uint16_t slen;
        uint64_t sval_offset : 40;
        uint64_t llen;
        uint64_t lval_offset;
};
/* Since the structure isn't packed not doing a sizeof */
#define ZS_KEY_BASE_REC_SIZE 24

struct zs_key {
        struct zs_key_base base;
        unsigned char *data;
};

struct zs_val_base {
        uint8_t  type;
        uint32_t slen : 24;
        uint32_t nullpad;
        uint64_t llen;
};
/* Since the structure isn't packed not doing a sizeof */
#define ZS_VAL_BASE_REC_SIZE 16

struct zs_val {
        struct zs_val_base base;
        unsigned char *data;
};

struct zs_short_commit {
        uint8_t type;
        uint32_t length : 24;
        uint32_t crc32;
};
/* Since the structure isn't packed not doing a sizeof */
#define ZS_SHORT_COMMIT_REC_SIZE 8

struct zs_long_commit {
        uint8_t  type1;
        uint64_t padding1 : 56;
        uint64_t length;
        uint8_t  type2;
        uint32_t padding2 : 24;
        uint32_t crc32;
};
/* Since the structure isn't packed not doing a sizeof */
#define ZS_LONG_COMMIT_REC_SIZE 24

#define MAX_SHORT_KEY_LEN 65535
#define MAX_SHORT_VAL_LEN 16777215


/* masks for file stat changes */
#define ZSDB_FILE_INO_CHANGED    0x0001
#define ZSDB_FILE_MODE_CHANGED   0x0002
#define ZSDB_FILE_UID_CHANGED    0x0004
#define ZSDB_FILE_GID_CHANGED    0x0008
#define ZSDB_FILE_SIZE_CHANGED   0x0010
#define ZSDB_FILE_MTIM_CHANGED   0x0020
#define ZSDB_FILE_CTIM_CHANGED   0x0040

/** File Data **/
struct zsdb_file {
        struct list_head list;
        enum db_ftype_t type;
        struct zs_header header;
        cstring fname;
        struct mfile *mf;
        struct vecu64 *index;
        struct stat st;
        int is_open;
        uint64_t indexpos;      /* Position in the index vec */
        uint64_t priority;      /* Higher the number, higher the priority */
        int dirty;
};

struct zsdb_files {
        struct zsdb_file factive; /* The active file */
        struct list_head pflist;  /* The list of packed files */
        struct list_head fflist;  /* The list of finalised files */
        unsigned int afcount;     /* Number of active files - should be 1 */
        unsigned int pfcount;     /* Number of packed files */
        unsigned int ffcount;     /* Number of finalised files */
};

/** Storage Backend **/
typedef enum _zsdb_be_t {
        ZSDB_BE_ACTIVE,
        ZSDB_BE_FINALISED,
        ZSDB_BE_PACKED,
} zsdb_be_t;

/** Iterator **/
struct iter_key_data {
        unsigned char *key;
        uint64_t len;
};

struct iter_htable {
        struct htable table;
};

struct iter_htable_entry {
        struct htable_entry entry;
        unsigned char *key;
        uint64_t keylen;
        void *value;
};
#define ITER_HTABLE_ENTRY_INIT  { .entry = HTABLE_ENTRY_INIT, .key = NULL, .keylen = 0, .value = NULL };

struct zsdb_iter_data {
        zsdb_be_t type;
        int priority;
        int done;
        int deleted;
        union {
                memtree_iter_t iter;
                struct zsdb_file *f;
        } data;
};

struct zsdb_iter {
        struct zsdb *db;
        struct pqueue pq;
        struct iter_htable ht;

        struct zsdb_iter_data **datav;
        int iter_data_count;
        int iter_data_alloc;

        int forone_iter;
        int foreach_iter;
};

/** Transactions **/
enum TxnType {
        TXN_ALL,
        TXN_PACKED_ONLY,
};

struct zsdb_txn {
        struct zsdb *db;
        struct zsdb_iter *iter;
        unsigned char *curkey;
        uint64_t curkeylen;
        int alloced;
};


/** Private data structure **/
struct zsdb_priv {
        uuid_t uuid;                /* The UUID for the DB */
        cstring dotzsdbfname;       /* The filename, with path of .zsdb */
        struct dotzsdb dotzsdb;     /* .zsdb contents */
        struct stat dotzsdb_st;     /* The stat() info of the file .zsdb file
                                     * when it was opened.*/

        cstring dbdir;              /* The directory path */

        struct zsdb_files dbfiles;

        /* Locks */
        struct file_lock wlk;       /* Lock when writing */
        struct file_lock plk;       /* Lock when packing */

        struct memtree *memtree;      /* in-memory B-Tree */
        struct memtree *fmemtree;     /* in-memory B-Tree of finalised records */

        zsdb_cmp_fn dbcompare;       /* The db comparator */
        memtree_search_cb_t btcompare; /* Th memtree comparator */

        int open;                    /* is the db open */
        int flags;                   /* The flags passed during call to open */
        int dbdirty;                 /* Marked dirty when there are changes
                                      * (add/remove/pack) to the db */
};


extern int zsdb_break(int err);

/* zeroskip-active.c */
extern int zs_active_file_open(struct zsdb_priv *priv, uint32_t idx, int mode);
extern int zs_active_file_close(struct zsdb_priv *priv);
extern int zs_active_file_finalise(struct zsdb_priv *priv);
extern int zs_active_file_write_keyval_record(struct zsdb_priv *priv,
                                              const unsigned char *key,
                                              uint64_t keylen,
                                              const unsigned char *val,
                                              uint64_t vallen);
extern int zs_active_file_write_commit_record(struct zsdb_priv *priv);
extern int zs_active_file_write_delete_record(struct zsdb_priv *priv,
                                              const unsigned char *key,
                                              uint64_t keylen);
extern int zs_active_file_record_foreach(struct zsdb_priv *priv,
                                         zsdb_foreach_cb *cb,
                                         zsdb_foreach_cb *deleted_cb,
                                         void *cbdata);
extern int zs_active_file_new(struct zsdb_priv *priv, uint32_t idx);

/* zeroskip-dotzsdb.c */
extern int zs_dotzsdb_create(struct zsdb_priv *priv);
extern int zs_dotzsdb_validate(struct zsdb_priv *priv);
extern int zs_dotzsdb_update_index_and_offset(struct zsdb_priv *priv,
                                              uint32_t idx,
                                              uint64_t offset);
extern ino_t zs_dotzsdb_get_ino(struct zsdb_priv *priv);
extern int zs_dotzsdb_update_stat(struct zsdb_priv *priv);
extern int zs_dotzsdb_check_stat(struct zsdb_priv *priv);
extern int zs_dotzsdb_update_begin(struct zsdb_priv *priv);
extern int zs_dotzsdb_update_end(struct zsdb_priv *priv);

/* zeroskip-file.c */
extern int zs_file_write_keyval_record(struct zsdb_file *f,
                                       const unsigned char *key, uint64_t keylen,
                                       const unsigned char *val, uint64_t vallen);
extern int zs_file_write_commit_record(struct zsdb_file *f, int final);
extern int zs_file_write_delete_record(struct zsdb_file *f,
                                       const unsigned char *key, uint64_t keylen);
extern int zs_file_update_stat(struct zsdb_file *f);
extern int zs_file_check_stat(struct zsdb_file *f);

/* zeroskip-filename.c */
extern void zs_filename_generate_active(struct zsdb_priv *priv, cstring *fname);
extern void zs_filename_generate_packed(struct zsdb_priv *priv, cstring *fname,
                                        uint32_t startidx, uint32_t endidx);

/* zeroskip-finalised.c */
extern int zs_finalised_file_open(const char *path, struct zsdb_file **fptr);
extern int zs_finalised_file_close(struct zsdb_file **fptr);
extern int zs_finalised_file_record_foreach(struct zsdb_file *f,
                                            zsdb_foreach_cb *cb, zsdb_foreach_cb *deleted_cb,
                                            void *cbdata);

/* zeroskip-header.c */
extern int zs_header_write(struct zsdb_file *f);
extern int zs_header_validate(struct zsdb_file *f);

/* zeroskip-iterator.c */
extern int zs_iterator_new(struct zsdb *db, struct zsdb_iter **iter);
extern int zs_iterator_begin(struct zsdb_iter **iter);
extern int zs_iterator_begin_at_key(struct zsdb_iter **iter,
                                    const unsigned char *key,
                                    uint64_t keylen,
                                    int *found);
extern int zs_iterator_begin_for_packed_files(struct zsdb_iter **iter,
                                              struct list_head *pflist);
extern struct zsdb_iter_data *zs_iterator_get(struct zsdb_iter *iter);
extern int zs_iterator_next(struct zsdb_iter *iter,
                            struct zsdb_iter_data *data);
extern void zs_iterator_end(struct zsdb_iter **iter);

/* zeroskip-packed.c */
extern int zs_packed_file_open(const char *path, struct zsdb_file **fptr);
extern int zs_packed_file_close(struct zsdb_file **fptr);
extern int zs_packed_file_new_from_memtree(const char *path,
                                           uint32_t startidx,
                                           uint32_t endidx,
                                           struct zsdb_priv *priv,
                                           struct zsdb_file **fptr);
extern int zs_packed_file_new_from_packed_files(const char *path,
                                                uint32_t startidx,
                                                uint32_t endidx,
                                                struct zsdb_priv *priv,
                                                struct list_head *flist,
                                                struct zsdb_iter **iter,
                                                struct zsdb_file **fptr);
extern int zs_packed_file_write_memtree_record(struct record *record, void *data);
extern int zs_packed_file_write_record(void *data,
                                       const unsigned char *key, uint64_t keylen,
                                       const unsigned char *value, uint64_t vallen);
extern int zs_packed_file_write_delete_record(void *data,
                                              const unsigned char *key,
                                              uint64_t keylen,
                                              const unsigned char *value _unused_,
                                              uint64_t vallen _unused_);
extern int zs_packed_file_write_commit_record(struct zsdb_file *f);
extern int zs_packed_file_write_final_commit_record(struct zsdb_file *f);
extern int zs_pq_cmp_key_frm_offset(const void *d1, const void *d2,
                                    zsdb_cmp_fn cmpfn, void *cbdata);
extern int zs_packed_file_get_key_from_offset(struct zsdb_file *f,
                                              unsigned char **key,
                                              uint64_t *len,
                                              enum record_t *type);
extern int zs_packed_file_bsearch_index(const unsigned char *key,
                                        const uint64_t keylen,
                                        struct zsdb_file *f,
                                        uint64_t *location,
                                        const unsigned char **value,
                                        uint64_t *vallen,
                                        zsdb_cmp_fn cmpfn);

/* zeroskip-record.c */
extern int zs_record_read_from_file(struct zsdb_file *f, uint64_t *offset,
                                    zsdb_foreach_cb *cb, zsdb_foreach_cb *deleted_cb,
                                    void *cbdata);
extern int zs_record_read_key_from_file_offset(const struct zsdb_file *f,
                                               uint64_t offset,
                                               struct zs_key *key);
extern int zs_read_key_val_record_from_file_offset(struct zsdb_file *f,
                                                   uint64_t *offset,
                                                   struct zs_key *key,
                                                   struct zs_val *val);

extern int zs_record_read_key_val_from_offset(struct zsdb_file *f,
                                              uint64_t *offset,
                                              const unsigned char **key,
                                              uint64_t *keylen,
                                              const unsigned char **val,
                                              uint64_t *vallen);

/* zeroskip-transaction.c */
extern int zs_transaction_begin(struct zsdb *db, struct zsdb_txn **txn);
extern void zs_transaction_end(struct zsdb_txn **txn);

CPP_GUARD_END
#endif  /* _ZEROSKIP_PRIV_H_ */
