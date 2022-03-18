#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define FIB_DEV "/dev/fibonacci"

int main(int argc, char const *argv[])
{
    if (argc < 3) {
        puts("Usage : ./program target mode\n");
        return 1;
    }

    const char *mode = argv[2];
    u_int64_t target = atoll(argv[1]);

    // try to open target device
    int fd = open(FIB_DEV, O_RDWR);
    if (fd == -1) {
        printf("Failed to open target device (%s).\n", FIB_DEV);
        return 1;
    }

    // set mode
    ssize_t ret = write(fd, mode, strlen(mode));
    if (ret < 0) {
        printf("Failed to set mode (%s).\n", mode);
        close(fd);
        return 1;
    }

    // set target
    lseek(fd, target, SEEK_SET);

    size_t digits = target >> 2;

    // calculate fib(n) time
    struct timespec us, ue;
    clock_gettime(CLOCK_MONOTONIC, &us);
    time_t k = read(fd, NULL, digits);
    clock_gettime(CLOCK_MONOTONIC, &ue);
    time_t u = (ue.tv_sec * 1e9 + ue.tv_nsec) - (us.tv_sec * 1e9 + us.tv_nsec);
    // output result
    printf("%8c[%ld,%ld,%ld]", ' ', u, k, u - k);

    close(fd);
    return 0;
}
