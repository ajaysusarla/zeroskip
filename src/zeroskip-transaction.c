/*
 * zeroskip-transactions.c
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

extern void assert_zsdb(struct zsdb *db);

int zs_transaction_begin(struct zsdb *db, struct txn **txn)
{
        struct txn *t = NULL;
        struct zsdb_priv *priv;

        assert_zsdb(db);

        priv = db->priv;

        if (!priv) return ZS_INTERNAL;
        if (!priv->open) {
                zslog(LOGWARNING, "DB `%s` not open!\n", priv->dbdir.buf);
                return ZS_NOT_OPEN;
        }

        t = xcalloc(1, sizeof(struct txn));

        t->db = db;
        t->iter = NULL;
        t->alloced = 1;

        *txn = t;

        return ZS_OK;
}

void zs_transaction_end(struct txn **txn)
{
        struct txn *t;

        if (txn && *txn && (*txn)->alloced) {
                t = *txn;
                *txn = NULL;
                t->db = NULL;
                t->iter = NULL;
                t->alloced = 0;

                xfree(t);
        }
}
