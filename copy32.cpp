#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <cstring>
#include <algorithm>

namespace {
constexpr size_t BUFFER_SIZE = 32;

void write_error(const char *prefix, const char *detail) {
    char message[512];
    size_t prefix_len = std::strlen(prefix);
    size_t detail_len = detail ? std::strlen(detail) : 0;
    if (prefix_len + detail_len + 2 >= sizeof(message)) {
        prefix_len = std::min(prefix_len, sizeof(message) - 3);
        detail_len = std::min(detail_len, sizeof(message) - prefix_len - 3);
    }
    std::memcpy(message, prefix, prefix_len);
    if (detail_len > 0) {
        message[prefix_len] = ':';
        message[prefix_len + 1] = ' ';
        std::memcpy(message + prefix_len + 2, detail, detail_len);
        prefix_len += detail_len + 2;
    }
    message[prefix_len] = '\n';
    ++prefix_len;
    write(STDERR_FILENO, message, prefix_len);
}

void write_errno_error(const char *prefix) {
    write_error(prefix, std::strerror(errno));
}
}

int main(int argc, char **argv) {
    if (argc != 3) {
        const char usage[] = "Usage: copy32 <source> <destination>\n";
        write(STDERR_FILENO, usage, sizeof(usage) - 1);
        return 1;
    }

    const char *source_path = argv[1];
    const char *dest_path = argv[2];

    int input_fd = open(source_path, O_RDONLY);
    if (input_fd < 0) {
        write_errno_error("Failed to open source file");
        return 1;
    }

    struct stat source_stat;
    if (fstat(input_fd, &source_stat) < 0) {
        write_errno_error("Failed to get source file information");
        close(input_fd);
        return 1;
    }

    mode_t source_mode = source_stat.st_mode & 07777;

    int output_fd = open(dest_path, O_WRONLY | O_CREAT | O_TRUNC, source_mode);
    if (output_fd < 0) {
        write_errno_error("Failed to open destination file");
        close(input_fd);
        return 1;
    }

    char buffer[BUFFER_SIZE];
    size_t head = 0;
    size_t tail = 0;
    size_t stored = 0;
    bool read_completed = false;

    while (!read_completed || stored > 0) {
        if (!read_completed && stored < BUFFER_SIZE) {
            size_t space = BUFFER_SIZE - stored;
            if (tail == BUFFER_SIZE) {
                tail = 0;
            }
            size_t chunk;
            if (head <= tail) {
                chunk = BUFFER_SIZE - tail;
                if (chunk > space) {
                    chunk = space;
                }
            } else {
                chunk = head - tail;
                if (chunk > space) {
                    chunk = space;
                }
            }

            if (chunk > 0) {
                ssize_t bytes_read = read(input_fd, buffer + tail, chunk);
                if (bytes_read < 0) {
                    if (errno == EINTR) {
                        continue;
                    }
                    write_errno_error("Failed to read from source file");
                    close(input_fd);
                    close(output_fd);
                    return 1;
                }
                if (bytes_read == 0) {
                    read_completed = true;
                } else {
                    tail = (tail + static_cast<size_t>(bytes_read)) % BUFFER_SIZE;
                    stored += static_cast<size_t>(bytes_read);
                }
            }
        }

        while (stored > 0) {
            if (head == BUFFER_SIZE) {
                head = 0;
            }
            size_t chunk;
            if (head < tail || (head == tail && stored == BUFFER_SIZE)) {
                chunk = (tail + BUFFER_SIZE - head) % BUFFER_SIZE;
                if (chunk == 0) {
                    chunk = BUFFER_SIZE - head;
                }
            } else {
                chunk = BUFFER_SIZE - head;
            }
            if (chunk > stored) {
                chunk = stored;
            }

            ssize_t bytes_written = write(output_fd, buffer + head, chunk);
            if (bytes_written < 0) {
                if (errno == EINTR) {
                    continue;
                }
                write_errno_error("Failed to write to destination file");
                close(input_fd);
                close(output_fd);
                return 1;
            }

            head = (head + static_cast<size_t>(bytes_written)) % BUFFER_SIZE;
            stored -= static_cast<size_t>(bytes_written);
            if (static_cast<size_t>(bytes_written) < chunk) {
                break;
            }
        }
    }

    if (fchmod(output_fd, source_mode) < 0) {
        write_errno_error("Failed to set destination file permissions");
        close(input_fd);
        close(output_fd);
        return 1;
    }

    if (close(input_fd) < 0) {
        write_errno_error("Failed to close source file");
        close(output_fd);
        return 1;
    }

    if (close(output_fd) < 0) {
        write_errno_error("Failed to close destination file");
        return 1;
    }

    return 0;
}
