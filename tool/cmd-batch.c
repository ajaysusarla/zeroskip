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

static void usage_and_die(const char *progname)
{
         fprintf(stderr, "Usage: %s %s\n", progname, cmd_batch_usage);


        exit(EXIT_FAILURE);
}

int cmd_batch(int argc, char **argv, const char *progname)
{
        static struct option long_options[] = {
                {"config", required_argument, NULL, 'c'},
                {"help", no_argument, NULL, 'h'},
                {NULL, 0, NULL, 0}
        };
        int option;
        int option_index;
        const char *config_file = NULL;

        while((option = getopt_long(argc, argv, "c:h?", long_options, &option_index)) != -1) {
                switch (option) {
                case 'c':
                        config_file = optarg;
                        break;
                case 'h':
                case '?':
                default:
                        usage_and_die(progname);
                };
        }

        cmd_parse_config(config_file);

        exit(EXIT_SUCCESS);
}
