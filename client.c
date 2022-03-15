#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#define FIB_DEV "/dev/fibonacci"

int main()
{
    long long sz;

    char write_buf[] = "testing writing";
    int offset = 100; /* TODO: try test something bigger than the limit */

    int fd = open(FIB_DEV, O_RDWR);
    if (fd < 0) {
        perror("Failed to open character device");
        exit(1);
    }

    for (int i = 0; i <= offset; i++) {
        sz = write(fd, write_buf, strlen(write_buf));
        printf("Writing to " FIB_DEV ", returned the sequence %lld\n", sz);
    }

    size_t length_pred = offset / 4 + 1;  // fib(n) length ~ 0.2090n
    char *result = calloc(length_pred, 1);

    for (int i = 0; i <= offset; i++) {
        lseek(fd, i, SEEK_SET);
        sz = read(fd, result, length_pred);
        printf("Reading from %s at offset %d, returned the sequence %s.\n",
               FIB_DEV, i, result);
    }

    for (int i = offset; i >= 0; i--) {
        lseek(fd, i, SEEK_SET);
        sz = read(fd, result, length_pred);
        printf("Reading from %s at offset %d, returned the sequence %s.\n",
               FIB_DEV, i, result);
    }

    free(result);
    close(fd);
    return 0;
}
