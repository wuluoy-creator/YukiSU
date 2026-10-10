#include "libbb.h"

/* bb_archive.h relies on libbb.h's types and visibility macros. */
#include "bb_archive.h"

int zysu_unpack_xz_stream_to_fd(int source_fd, int destination_fd, uint64_t* decoded_size) {
    transformer_state_t state;
    long long result;

    if (!decoded_size)
        return -1;
    init_transformer_state(&state);
    state.src_fd = source_fd;
    state.dst_fd = destination_fd;
    result = unpack_xz_stream(&state);
    if (result < 0)
        return -1;
    *decoded_size = (uint64_t)result;
    return 0;
}
