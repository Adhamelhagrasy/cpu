#include "../include/os.h"
#include <stdio.h>

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    OS os;
    os_init(&os);
    os_run(&os);
    return 0;
}
