#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ksud::boot::lkm_image {

struct LoadInfoLayout {
    std::uint64_t structure_size;
    std::uint64_t hdr_offset;
    std::uint64_t len_offset;

    [[nodiscard]] bool operator==(const LoadInfoLayout& other) const;
    [[nodiscard]] bool operator<(const LoadInfoLayout& other) const;
};

struct KernelBtf {
    std::size_t file_offset;
    std::size_t size;
    std::size_t type_count;
    std::optional<LoadInfoLayout> load_info;
};

class BtfCandidate {
public:
    [[nodiscard]] std::size_t file_offset() const;
    [[nodiscard]] std::size_t size() const;
    [[nodiscard]] std::size_t type_count() const;

    [[nodiscard]] bool inspect(KernelBtf& result, std::string* error = nullptr) const;

private:
    friend std::vector<BtfCandidate> find_btf_candidates(const std::uint8_t* image,
                                                         std::size_t image_size);

    BtfCandidate(KernelBtf result, std::string error);

    KernelBtf result_;
    std::string error_;
};

[[nodiscard]] std::vector<BtfCandidate> find_btf_candidates(const std::uint8_t* image,
                                                            std::size_t image_size);

[[nodiscard]] inline std::vector<BtfCandidate> find_btf_candidates(
    const std::vector<std::uint8_t>& image) {
    return find_btf_candidates(image.data(), image.size());
}

}  // namespace ksud::boot::lkm_image
