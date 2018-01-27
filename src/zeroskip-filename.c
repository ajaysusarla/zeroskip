/*
 * zeroskip-filename.c
 *
 * This file is part of zeroskip.
 *
 * zeroskip is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 *
 */

#include "log.h"
#include "zeroskip.h"
#include "zeroskip-priv.h"

/* zs_filename_generate_active():
 * Generates a new filename for the active file
 */
void zs_filename_generate_active(struct zsdb_priv *priv, cstring *fname)
{
        char index[11] = { 0 };

        snprintf(index, 20, "%d", priv->dotzsdb.curidx);

        cstring_release(fname);

        cstring_dup(&priv->dbdir, fname);
        cstring_addch(fname, '/');
        cstring_addstr(fname, ZS_FNAME_PREFIX);
        cstring_add(fname, priv->dotzsdb.uuidstr, UUID_STRLEN - 1);
        cstring_addch(fname, '-');
        cstring_addstr(fname, index);
}