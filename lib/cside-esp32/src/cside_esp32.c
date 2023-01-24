#include <stdio.h>

void _component_func();

void app_main(void)
{
    printf("Testing everything\n");

    _component_func();

    while(1) {}
}
