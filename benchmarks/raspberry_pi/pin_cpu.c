#define _GNU_SOURCE
#include <errno.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/* Minimal taskset replacement for targets whose BusyBox omits that applet. */
int main(int argc, char** argv) {
    char* end;
    long cpu;
    cpu_set_t set;
    if (argc < 3) {
        fprintf(stderr, "usage: pin_cpu CPU PROGRAM [ARGS...]\n");
        return 2;
    }
    errno = 0;
    cpu = strtol(argv[1], &end, 10);
    if (errno != 0 || *end != '\0' || end == argv[1] || cpu < 0 || cpu >= CPU_SETSIZE)
        return 2;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);
    if (sched_setaffinity(0, sizeof(set), &set) != 0) {
        perror("sched_setaffinity");
        return 1;
    }
    execvp(argv[2], &argv[2]);
    perror("execvp");
    return 1;
}
