#include <stdio.h>

#include "../include/stickhook.h"

int (*orig_is_valid1)(char *serial);
int my_is_valid(char *serial) {
    printf("====== verifying serial: %s\n", serial);
    int res = orig_is_valid1(serial);
    printf("====== result: %d\n", res);
    printf("====== faking valid...\n");
    return 1;
}

void (*orig_ask_serial)();
void my_ask_serial() {
    printf("====== ask_serial hooked!\n");
    orig_ask_serial();
    printf("====== ask_serial returned!\n");
}

__attribute__((constructor(0))) void load() {
    stick_init();
    stick_hook("main", 0x100000528, my_is_valid, &orig_is_valid1);
    // stick_hook("abcd", 0x100000528, my_is_valid, &orig_is_valid1);
    stick_hook("main", 0x100000608, my_ask_serial, &orig_ask_serial);
    return;
}
