#include "cnode.h"
#include "args.h"

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include "utilities.h"

bool args_appid_valid(char *appid) {
    return (appid != NULL);
}
bool args_port_valid(int port) {
    return ((PORT_MIN <= port)  && (port <= PORT_MAX));
}
bool args_serialnum_valid(int serialnum) {
    return (serialnum > 0);
}
bool args_numexecutors_valid(int numexecutors) {
    return (numexecutors >= 0);
}

cnode_args_t *process_args(int argc, char **argv) {
    cnode_args_t *args = (cnode_args_t *)malloc(sizeof(cnode_args_t));

    // set default values (e.g., port number)
    args->port = DEFAULTS_PORT;
    args->nexecs = DEFAULTS_NUMEXECUTORS;
    args->snumber = DEFAULTS_SERIALNUM;
    args->appid = NULL;
    args->tags = NULL;
    opterr = 0;

    int c;

    // parse the arguments..
    while ((c = getopt (argc, argv, "p:a:n:g:t:x:")) != -1)
    switch (c)
    {
        case 'a':
            args->appid = optarg;
        break;
        case 'n':
            args->snumber = atoi(optarg);
        break;
        case 'g':
            args->groupid = atoi(optarg);
        break;
        case 't':
            args->tags = optarg;
        break;
        case 'p':
            args->port = atoi(optarg);
        break;
        case 'x':
            args->nexecs = atoi(optarg);
        break;
        default:
            terminate_error(true, "Unknown input option\nUsage: program -a app_id [-t tag] [-g groupid] [-n num] [-p port] [-x executors]\n");
    }

    // check validity
    if ( !args_appid_valid(args->appid) ) {
        destroy_args(args);
        terminate_error(false, "Appid is not specified ");
        return NULL;
    }
    if ( !args_port_valid(args->port) ) {
        destroy_args(args);
        terminate_error(false, "Invalid port given %d",args->port);
        return NULL;
    }
    if ( !args_serialnum_valid(args->snumber) ) {
        destroy_args(args);
        terminate_error(false, "Invalid serial number given %d",args->snumber);
        return NULL;
    }
    if ( !args_numexecutors_valid(args->nexecs) ){
        destroy_args(args);
        terminate_error(false, "Invalid number of executors given %d",args->nexecs);
        return NULL;
    }

    return args;
}

void destroy_args(cnode_args_t *args) {
    // free arguments object
    free(args);
}
