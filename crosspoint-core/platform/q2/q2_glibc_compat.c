// SPDX-License-Identifier: MIT
// Compatibility exports for Shanling Q2 glibc 2.28. Host cross headers use
// C23 symbol redirects not provided by device libc; route to legacy APIs.
// Never weaken the Expat hash seed: /dev/urandom failure is fatal.
#define _GNU_SOURCE 1
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

unsigned long __isoc23_strtoul(const char *s, char **end, int base) {
    return strtoul(s, end, base);
}
int __isoc23_fscanf(FILE *stream, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int rc = vfscanf(stream, format, ap);
    va_end(ap);
    return rc;
}
void arc4random_buf(void *buffer, size_t length) {
    unsigned char *out = (unsigned char *)buffer;
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) _exit(127);
    while (length != 0) {
        ssize_t n = read(fd, out, length);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) _exit(127);
        out += (size_t)n;
        length -= (size_t)n;
    }
    close(fd);
}