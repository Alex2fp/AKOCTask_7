#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "shared.h"

static volatile sig_atomic_t stop_requested = 0;

static void handle_signal(int signum) {
    (void)signum;
    stop_requested = 1;
}

static void install_signal_handlers(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
}

static void sleep_for_millis(long ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;

    while (nanosleep(&ts, &ts) == -1 && errno == EINTR) {
        // retry if interrupted
    }
}

int main(void) {
    install_signal_handlers();

    bool created = false;
    int shm_fd = shm_open(SHM_OBJECT_NAME, O_CREAT | O_EXCL | O_RDWR, 0660);
    if (shm_fd == -1) {
        if (errno != EEXIST) {
            perror("shm_open");
            return EXIT_FAILURE;
        }

        shm_fd = shm_open(SHM_OBJECT_NAME, O_RDWR, 0);
        if (shm_fd == -1) {
            perror("shm_open");
            return EXIT_FAILURE;
        }
    } else {
        created = true;
        if (ftruncate(shm_fd, sizeof(SharedState)) == -1) {
            perror("ftruncate");
            close(shm_fd);
            return EXIT_FAILURE;
        }
    }

    SharedState *state = mmap(NULL, sizeof(SharedState), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (state == MAP_FAILED) {
        perror("mmap");
        close(shm_fd);
        return EXIT_FAILURE;
    }

    if (created) {
        memset((void *)state, 0, sizeof(SharedState));
    }

    state->client_ready = 1;

    srand((unsigned)time(NULL) ^ (unsigned)getpid());

    printf("Client started. Generating random numbers...\n");
    fflush(stdout);

    while (!stop_requested) {
        if (state->terminate) {
            break;
        }

        if (!state->has_value) {
            int value = rand() % 100; // numbers in range 0-99
            state->value = value;
            state->has_value = 1;
        }

        sleep_for_millis(200);
    }

    state->terminate = 1;
    state->client_ready = 0;

    if (munmap(state, sizeof(SharedState)) == -1) {
        perror("munmap");
    }

    close(shm_fd);

    if (shm_unlink(SHM_OBJECT_NAME) == -1 && errno != ENOENT) {
        perror("shm_unlink");
    }

    printf("Client exited.\n");
    return EXIT_SUCCESS;
}
