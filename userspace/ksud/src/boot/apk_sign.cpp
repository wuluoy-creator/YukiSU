#include "apk_sign.hpp"
#include <mbedtls/sha256.h>
#include "../log.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

namespace ksud {

namespace {

constexpr const char* hex_chars = "0123456789abcdef";

std::string sha256_digest(const uint8_t* data, size_t len) {
    unsigned char digest[32];
    mbedtls_sha256(data, len, digest, 0);
    std::string out;
    out.reserve(64);
    for (const unsigned char i : digest) {
        out.push_back(hex_chars[(i >> 4) & 0xf]);
        out.push_back(hex_chars[i & 0xf]);
    }
    return out;
}

struct ZipCentralDirectoryHeader {
    uint32_t signature;
    uint16_t version_made_by;
    uint16_t version_needed;
    uint16_t flags;
    uint16_t compression;
    uint16_t mod_time;
    uint16_t mod_date;
    uint32_t crc32;
    uint32_t compressed_size;
    uint32_t uncompressed_size;
    uint16_t file_name_length;
    uint16_t extra_field_length;
    uint16_t file_comment_length;
    uint16_t disk_number_start;
    uint16_t internal_file_attributes;
    uint32_t external_file_attributes;
    uint32_t relative_offset;
} __attribute__((packed));

class FileReader {
public:
    explicit FileReader(const char* path) : fd_(open(path, O_RDONLY | O_CLOEXEC)) {
        struct stat status{};
        if (fd_ >= 0 && fstat(fd_, &status) == 0 && status.st_size >= 0)
            size_ = static_cast<uint64_t>(status.st_size);
    }
    ~FileReader() {
        if (fd_ >= 0)
            close(fd_);
    }
    FileReader(const FileReader&) = delete;
    FileReader& operator=(const FileReader&) = delete;
    FileReader(FileReader&&) = delete;
    FileReader& operator=(FileReader&&) = delete;

    [[nodiscard]] bool valid() const { return fd_ >= 0; }
    [[nodiscard]] uint64_t size() const { return size_; }
    bool read_at(uint64_t offset, void* data, size_t size) const {
        if (offset > size_ || size > size_ - offset)
            return false;
        auto* cursor = static_cast<uint8_t*>(data);
        size_t done = 0;
        while (done < size) {
            const ssize_t count =
                pread(fd_, cursor + done, size - done, static_cast<off_t>(offset + done));
            if (count > 0) {
                done += static_cast<size_t>(count);
                continue;
            }
            if (count < 0 && errno == EINTR)
                continue;
            return false;
        }
        return true;
    }

private:
    int fd_ = -1;
    uint64_t size_ = 0;
};

bool has_v1_signature_file(const FileReader& file, uint32_t cd_offset, uint32_t cd_size) {
    constexpr uint32_t kCentralDirectoryHeaderSignature = 0x02014b50;
    constexpr std::string_view kManifest = "META-INF/MANIFEST.MF";
    const uint64_t cd_end = static_cast<uint64_t>(cd_offset) + cd_size;
    if (cd_end > file.size())
        return false;

    uint64_t offset = cd_offset;
    ZipCentralDirectoryHeader header{};
    while (offset + sizeof(header) <= cd_end && file.read_at(offset, &header, sizeof(header))) {
        if (header.signature != kCentralDirectoryHeaderSignature)
            break;
        offset += sizeof(header);
        if (header.file_name_length > cd_end - offset)
            break;
        std::string file_name(header.file_name_length, '\0');
        if (!file.read_at(offset, file_name.data(), file_name.size()))
            break;
        if (file_name == kManifest)
            return true;
        const uint64_t advance = static_cast<uint64_t>(header.file_name_length) +
                                 header.extra_field_length + header.file_comment_length;
        if (advance > cd_end - offset)
            break;
        offset += advance;
    }
    return false;
}

}  // namespace

ApkSignatureInfo get_apk_signature(const std::string& apk_path) {
    ApkSignatureInfo info{};

    const FileReader file(apk_path.c_str());
    if (!file.valid()) {
        LOGE("Failed to open APK: %s", apk_path.c_str());
        return info;
    }

    const uint64_t file_size = file.size();
    if (file_size < 22) {
        LOGE("Not a valid ZIP file");
        return info;
    }

    // Find EOCD (End of Central Directory). Its final u16 is the comment length,
    // so scan backward until that value points exactly to the physical EOF.
    uint64_t eocd_offset = 0;
    bool found_eocd = false;
    for (uint64_t comment_size = 0; comment_size <= UINT16_MAX && comment_size + 22 <= file_size;
         ++comment_size) {
        uint16_t stored_comment_size = 0;
        if (!file.read_at(file_size - comment_size - 2, &stored_comment_size,
                          sizeof(stored_comment_size)))
            break;
        if (stored_comment_size != comment_size)
            continue;
        const uint64_t candidate = file_size - comment_size - 22;
        uint32_t magic = 0;
        if (!file.read_at(candidate, &magic, sizeof(magic)))
            break;
        if ((magic ^ 0xcafebabe) == 0xccfbf1ee) {
            eocd_offset = candidate;
            found_eocd = true;
            break;
        }
    }
    if (!found_eocd) {
        LOGE("EOCD not found");
        return info;
    }
    info.valid = true;

    uint32_t cd_size = 0;
    uint32_t cd_offset = 0;
    if (!file.read_at(eocd_offset + 12, &cd_size, sizeof(cd_size)) ||
        !file.read_at(eocd_offset + 16, &cd_offset, sizeof(cd_offset)))
        return info;
    info.v1 = has_v1_signature_file(file, cd_offset, cd_size);

    if (cd_offset < 0x18) {
        return info;
    }

    const uint64_t footer_offset = cd_offset - 0x18;
    uint64_t block_size = 0;
    std::array<char, 16> magic{};
    if (!file.read_at(footer_offset, &block_size, sizeof(block_size)) ||
        !file.read_at(footer_offset + sizeof(block_size), magic.data(), magic.size()))
        return info;

    if (memcmp(magic.data(), "APK Sig Block 42", 16) != 0)
        return info;

    // block_size comes straight out of the file, so guard the addition before
    // trusting it. Everything downstream is bounds-checked too, but wrapping here
    // would make that reasoning depend on three later checks instead of one.
    if (block_size > UINT64_MAX - 8 || block_size + 8 > cd_offset)
        return info;
    const uint64_t block_start = cd_offset - (block_size + 8);
    uint64_t block_size_check = 0;
    if (!file.read_at(block_start, &block_size_check, sizeof(block_size_check)) ||
        block_size != block_size_check) {
        LOGE("APK Signing Block size mismatch");
        return info;
    }

    // Pairs occupy the bytes between the leading size and the footer. Each pair
    // length includes its four-byte ID but excludes the length word itself.
    uint64_t offset = block_start + sizeof(block_size_check);
    while (offset + sizeof(uint64_t) <= footer_offset) {
        uint64_t pair_len = 0;
        if (!file.read_at(offset, &pair_len, sizeof(pair_len)) || pair_len < sizeof(uint32_t))
            break;
        offset += sizeof(pair_len);
        if (pair_len > footer_offset - offset)
            break;
        const uint64_t pair_end = offset + pair_len;

        uint32_t pair_id = 0;
        if (!file.read_at(offset, &pair_id, sizeof(pair_id)))
            break;
        uint64_t cursor = offset + sizeof(pair_id);

        if (pair_id == 0x7109871a) {
            // V2: descend through signer/signed-data/digest length prefixes to
            // the first certificate, while keeping every read inside this pair.
            const auto take_u32 = [&](uint32_t* value) {
                if (cursor + sizeof(*value) > pair_end ||
                    !file.read_at(cursor, value, sizeof(*value)))
                    return false;
                cursor += sizeof(*value);
                return true;
            };
            uint32_t signer_seq_len = 0;
            uint32_t signer_len = 0;
            uint32_t signed_data_len = 0;
            uint32_t digests_len = 0;
            uint32_t certs_len = 0;
            uint32_t cert_len = 0;
            if (take_u32(&signer_seq_len) && take_u32(&signer_len) && take_u32(&signed_data_len) &&
                take_u32(&digests_len) && digests_len <= pair_end - cursor) {
                cursor += digests_len;
                if (take_u32(&certs_len) && take_u32(&cert_len) && cert_len <= pair_end - cursor) {
                    std::vector<uint8_t> cert_data(cert_len);
                    if (file.read_at(cursor, cert_data.data(), cert_data.size())) {
                        info.v2 = {true, cert_len,
                                   sha256_digest(cert_data.data(), cert_data.size())};
                    }
                }
            }
            (void)signer_seq_len;
            (void)signer_len;
            (void)signed_data_len;
            (void)certs_len;
        } else if (pair_id == 0xf05368c0) {
            info.v3 = true;
        } else if (pair_id == 0x1b93ad61) {
            info.v31 = true;
        }

        offset = pair_end;
    }

    return info;
}

}  // namespace ksud
