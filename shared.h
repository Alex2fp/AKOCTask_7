#ifndef SHARED_H
#define SHARED_H

#include <signal.h>

#define SHM_OBJECT_NAME "/akoctask7_shm"

typedef struct SharedState {
    volatile sig_atomic_t terminate;    // 0 - running, 1 - terminate requested
    volatile sig_atomic_t client_ready; // 1 when client is alive
    volatile sig_atomic_t server_ready; // 1 when server is alive
    volatile sig_atomic_t has_value;    // 1 when a fresh random value is available
    int value;                          // random value produced by the client
} SharedState;

#endif // SHARED_H
