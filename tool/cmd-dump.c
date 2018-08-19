/*
 * zeroskip is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 *
 * Copyright (c) 2018 Partha Susarla <mail@spartha.org>
 *
 */

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>

#include "cmds.h"
#include <libzeroskip/zeroskip.h>

static void usage_and_die(const char *progname)
{
         fprintf(stderr, "Usage: %s %s\n", progname, cmd_dump_usage);


        exit(EXIT_FAILURE);
}
int cmd_dump(int argc, char **argv, const char *progname)
{
        static struct option long_options[] = {
                {"config", required_argument, NULL, 'c'},
                {"recs", required_argument, NULL, 'r'},
                {"help", no_argument, NULL, 'h'},
                {NULL, 0, NULL, 0}
        };
        int option;
        int option_index;
        const char *config_file = NULL;
        struct zsdb *db = NULL;
        const char *dbname;
        int ret;
        DBDumpLevel level = DB_DUMP_ACTIVE;

        while((option = getopt_long(argc, argv, "r:c:h?", long_options,
                                    &option_index)) != -1) {
                switch (option) {
                case 'r':       /* level of detail */
                        level = parse_dump_level_string(optarg);
                        break;
                case 'c':       /* config file */
                        config_file = optarg;
                        break;
                case 'h':
                case '?':
                default:
                        usage_and_die(progname);
                };
        }

        if (argc - optind != 1) {
                usage_and_die(progname);
        }

        dbname = argv[optind];

        cmd_parse_config(config_file);

        if (zsdb_init(&db, NULL, NULL) != ZS_OK) {
                fprintf(stderr, "ERROR: Failed initialising DB.\n");
                ret = EXIT_FAILURE;
                goto done;
        }

        if (zsdb_open(db, dbname, MODE_RDWR) != ZS_OK) {
                fprintf(stderr, "ERROR: Could not open DB %s.\n", dbname);
                ret = EXIT_FAILURE;
                goto done;
        }

        if (zsdb_dump(db, level) != ZS_OK) {
                fprintf(stderr, "ERROR: Failed dumping records in %s.\n",
                      dbname);
                ret = EXIT_FAILURE;
                goto done;
        }

        ret = EXIT_SUCCESS;
        fprintf(stderr, "OK\n");
done:
        if (zsdb_close(db) != ZS_OK) {
                fprintf(stderr, "ERROR: Could not close DB.\n");
                ret = EXIT_FAILURE;
        }

        zsdb_final(&db);

        exit(ret);
}
