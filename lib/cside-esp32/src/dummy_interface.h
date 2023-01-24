#pragma once

#include <stdio.h>
#include <stdlib.h>

typedef struct _cnode_t 
{
    int the_entire_internal_structure_of_a_c_node;
    int tboard;
} cnode_t;

typedef struct _strange_thing 
{ 
    char *sval;
    int ival;
} strange_thing;

typedef struct __arg_t 
{
    strange_thing val;
} arg_t;

typedef struct _context_t
{
    
} context_t;

arg_t* remote_sync_call(int, char*, char*, char*);
void local_async_call(int, char*, int, char*);

void tboard_register_func(int, int);

void task_yield();
cnode_t* cnode_init(int argc, char *argv[]);
void cnode_stop(cnode_t*);
void cnode_destroy(cnode_t*);

void* task_get_args();

#define TBOARD_FUNC(x,y,z,w,l) 5