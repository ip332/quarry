#define _GNU_SOURCE
#include <errno.h>
#include <inttypes.h>
#include <linux/perf_event.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#define BUCKETS 65536U
static uint64_t ips[BUCKETS], counts[BUCKETS];
static uint64_t samples, lost;
static void add(uint64_t ip) {
    size_t slot = (size_t)((ip >> 2U) * UINT64_C(11400714819323198485)) & (BUCKETS - 1U);
    while (counts[slot] != 0U && ips[slot] != ip) slot = (slot + 1U) & (BUCKETS - 1U);
    ips[slot] = ip; ++counts[slot]; ++samples;
}
static void drain(struct perf_event_mmap_page* page, size_t page_size) {
    const uint64_t head = __atomic_load_n(&page->data_head, __ATOMIC_ACQUIRE);
    uint64_t tail = page->data_tail;
    const size_t size = page->data_size;
    const uint8_t* data = (const uint8_t*)page + page_size;
    while (tail < head) {
        struct perf_event_header header;
        uint8_t bytes[32];
        for (size_t i = 0; i < sizeof(header); ++i) bytes[i] = data[(tail + i) & (size - 1U)];
        memcpy(&header, bytes, sizeof(header));
        if (header.size < sizeof(header) || tail + header.size > head) break;
        size_t n = header.size < sizeof(bytes) ? header.size : sizeof(bytes);
        for (size_t i = sizeof(header); i < n; ++i) bytes[i] = data[(tail + i) & (size - 1U)];
        if (header.type == PERF_RECORD_SAMPLE && n >= 16U) {
            uint64_t ip; memcpy(&ip, bytes + sizeof(header), sizeof(ip)); add(ip);
        } else if (header.type == PERF_RECORD_LOST && n >= 24U) {
            uint64_t count; memcpy(&count, bytes + 16U, sizeof(count)); lost += count;
        }
        tail += header.size;
    }
    __atomic_store_n(&page->data_tail, tail, __ATOMIC_RELEASE);
}
int main(int argc, char** argv) {
    if (argc < 4 || strcmp(argv[1], "-o") != 0) {
        fprintf(stderr, "usage: sample -o PROFILE.tsv PROGRAM [ARGS...]\n"); return 2;
    }
    int gate[2];
    if (pipe(gate) != 0) { perror("pipe"); return 1; }
    pid_t child = fork();
    if (child < 0) { perror("fork"); return 1; }
    if (child == 0) {
        char byte;
        close(gate[1]);
        if (read(gate[0], &byte, 1U) != 1) _exit(1);
        close(gate[0]);
        execvp(argv[3], &argv[3]); perror("execvp"); _exit(127);
    }
    close(gate[0]);
    struct perf_event_attr attr = {0};
    attr.size = PERF_ATTR_SIZE_VER5;
    attr.type = PERF_TYPE_HARDWARE; attr.config = PERF_COUNT_HW_CPU_CYCLES;
    attr.sample_type = PERF_SAMPLE_IP; attr.freq = 1U; attr.sample_freq = 997U;
    attr.disabled = 1U; attr.enable_on_exec = 1U; attr.inherit = 0U;
    attr.exclude_kernel = 1U; attr.exclude_hv = 1U; attr.wakeup_events = 1U;
    const char* event = "cycles";
    int fd = (int)syscall(__NR_perf_event_open, &attr, child, -1, -1, 0);
    if (fd < 0) {
        fprintf(stderr, "cycles unavailable: %s; trying cpu-clock\n", strerror(errno));
        event = "cpu-clock"; attr.type = PERF_TYPE_SOFTWARE; attr.config = PERF_COUNT_SW_CPU_CLOCK;
        fd = (int)syscall(__NR_perf_event_open, &attr, child, -1, -1, 0);
    }
    if (fd < 0) { perror("perf_event_open"); kill(child, SIGKILL); waitpid(child, NULL, 0); return 1; }
    const size_t page_size = (size_t)sysconf(_SC_PAGESIZE);
    const size_t map_size = page_size * 65U;
    struct perf_event_mmap_page* page = mmap(NULL, map_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (page == MAP_FAILED) { perror("mmap"); kill(child, SIGKILL); waitpid(child, NULL, 0); return 1; }
    if (write(gate[1], "x", 1U) != 1) { perror("gate"); return 1; }
    close(gate[1]);
    int status = 0;
    struct pollfd poll_fd = {fd, POLLIN, 0};
    while (waitpid(child, &status, WNOHANG) == 0) {
        (void)poll(&poll_fd, 1U, 100); drain(page, page_size);
    }
    (void)ioctl(fd, PERF_EVENT_IOC_DISABLE, 0); drain(page, page_size);
    FILE* out = fopen(argv[2], "w");
    if (out == NULL) { perror("profile output"); return 1; }
    fprintf(out, "# event=%s frequency=997 samples=%" PRIu64 " lost=%" PRIu64 "\n", event, samples, lost);
    for (size_t i = 0; i < BUCKETS; ++i)
        if (counts[i] != 0U) fprintf(out, "0x%" PRIx64 "\t%" PRIu64 "\n", ips[i], counts[i]);
    fclose(out); munmap(page, map_size); close(fd);
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}
