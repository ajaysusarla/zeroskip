/*
 * zeroskip is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 *
 * Copyright (c) 2018 Partha Susarla <mail@spartha.org>
 *
 */

#include <getopt.h>
#include <sys/param.h>          /* For MAXPATHLEN */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "cmds.h"
#include <libzeroskip/zeroskip.h>

static void usage_and_die(const char *progname)
{
         fprintf(stderr, "Usage: %s %s\n", progname, cmd_new_usage);


        exit(EXIT_FAILURE);
}

int cmd_new(int argc, char **argv, const char *progname)
{
        static struct option long_options[] = {
                {"config", required_argument, NULL, 'c'},
                {"help", no_argument, NULL, 'h'},
                {NULL, 0, NULL, 0}
        };
        int option;
        int option_index;
        const char *config_file = NULL;
        struct zsdb *db = NULL;
        char *fname;
        int ret;

        while((option = getopt_long(argc, argv, "c:h?", long_options,
                                    &option_index)) != -1) {
                switch (option) {
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

        fname = argv[optind];

        cmd_parse_config(config_file);

        if (zsdb_init(&db, NULL, NULL) != ZS_OK) {
                fprintf(stderr, "ERROR: Failed initialising DB.\n");
                ret = EXIT_FAILURE;
                goto done;
        }

        if (zsdb_open(db, fname, MODE_CREATE) != ZS_OK) {
                fprintf(stderr, "ERROR: Could not create DB.\n");
                ret = EXIT_FAILURE;
                goto done;
        }

        if (zsdb_close(db) != ZS_OK) {
                fprintf(stderr, "ERROR: Could not close DB.\n");
                ret = EXIT_FAILURE;
                goto done;
        }

        ret = EXIT_SUCCESS;
        fprintf(stderr, "OK\n");
done:
        zsdb_final(&db);

        exit(ret);
}
