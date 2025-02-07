// init_shell.c

#include "syscall.h"
#include "string.h"

int main() {
    int result;

    result = _devopen(0, "ser", 1);

    if (result < 0) {
        _msgout("_devopen failed");
        _exit();
    }

    result = _fsopen(1, "shell");

    if (result < 0) {
        _msgout("_fsopen failed");
        _exit();
    }

    _exec(1);
}
