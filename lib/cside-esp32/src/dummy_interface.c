#include <dummy_interface.h>

arg_t* remote_sync_call(int a1, char* a2, char* a3, char* a4)
{
    arg_t* thing = malloc(sizeof(arg_t));
    thing->val = malloc(sizeof(strange_thing));
    thing->val.sval = a2;
    return thing;
}