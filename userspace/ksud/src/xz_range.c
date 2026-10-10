#include "xz_range.hpp"

#include <errno.h>
#include <fcntl.h>
#include <linux/memfd.h>
#include <pthread.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

int zysu_unpack_xz_stream_to_fd(int source_fd, int destination_fd, uint64_t* decoded_size);

struct range_writer {
    int fd;
    int source_fd;
    uint64_t range_offset;
    uint64_t range_end;
    uint64_t unpacked_size;
    uint64_t offset;
    uint64_t written;
    int failed;
};

static int write_all(int fd, const void* data, size_t size) {
    const uint8_t* cursor = data;

    while (size != 0) {
        ssize_t count = write(fd, cursor, size);
        if (count > 0) {
            cursor += count;
            size -= (size_t)count;
            continue;
        }
        if (count < 0 && errno == EINTR)
            continue;
        return -1;
    }
    return 0;
}

static void* write_selected_range(void* opaque) {
    struct range_writer* writer = opaque;
    uint8_t buffer[65536];

    while (1) {
        ssize_t count = read(writer->source_fd, buffer, sizeof(buffer));
        uint64_t chunk_end;
        if (count < 0) {
            if (errno == EINTR)
                continue;
            writer->failed = 1;
            break;
        }
        if (count == 0)
            break;
        if (writer->offset > UINT64_MAX - (size_t)count) {
            writer->failed = 1;
            continue;
        }
        chunk_end = writer->offset + (size_t)count;
        if (!writer->failed && writer->offset < writer->range_end &&
            chunk_end > writer->range_offset) {
            uint64_t copy_start =
                writer->offset > writer->range_offset ? writer->offset : writer->range_offset;
            uint64_t copy_end = chunk_end < writer->range_end ? chunk_end : writer->range_end;
            size_t source_offset = (size_t)(copy_start - writer->offset);
            size_t copy_size = (size_t)(copy_end - copy_start);

            if (write_all(writer->fd, buffer + source_offset, copy_size) != 0)
                writer->failed = 1;
            else
                writer->written += copy_size;
        }
        writer->offset = chunk_end;
    }
    if (writer->offset != writer->unpacked_size ||
        writer->written != writer->range_end - writer->range_offset)
        writer->failed = 1;
    return NULL;
}

int zysu_unpack_xz_range_to_fd(const uint8_t* packed, size_t packed_size, uint64_t range_offset,
                               size_t range_size, uint64_t unpacked_size, int dst_fd) {
    struct range_writer writer;
    pthread_t writer_thread;
    int input_fd = -1;
    int output_pipe[2] = {-1, -1};
    uint64_t decoded = 0;
    int result = -1;

    if (!packed || packed_size == 0 || dst_fd < 0 || range_offset > UINT64_MAX - range_size ||
        range_offset + range_size > unpacked_size) {
        errno = EINVAL;
        return -1;
    }

    input_fd = (int)syscall(__NR_memfd_create, "zysu-lkm-pack", MFD_CLOEXEC);
    if (input_fd < 0 || write_all(input_fd, packed, packed_size) != 0 ||
        lseek(input_fd, 0, SEEK_SET) != 0 || pipe(output_pipe) != 0)
        goto cleanup;

    /* Drain the fd-based decoder while retaining only the selected module range. */
    memset(&writer, 0, sizeof(writer));
    writer.fd = dst_fd;
    writer.source_fd = output_pipe[0];
    writer.range_offset = range_offset;
    writer.range_end = range_offset + range_size;
    writer.unpacked_size = unpacked_size;
    if (pthread_create(&writer_thread, NULL, write_selected_range, &writer) != 0)
        goto cleanup;

    int decode_status = zysu_unpack_xz_stream_to_fd(input_fd, output_pipe[1], &decoded);
    close(output_pipe[1]);
    output_pipe[1] = -1;
    if (pthread_join(writer_thread, NULL) == 0 && decode_status == 0 && decoded == unpacked_size &&
        !writer.failed)
        result = 0;

cleanup:
    if (output_pipe[0] >= 0)
        close(output_pipe[0]);
    if (output_pipe[1] >= 0)
        close(output_pipe[1]);
    if (input_fd >= 0)
        close(input_fd);
    return result;
}
