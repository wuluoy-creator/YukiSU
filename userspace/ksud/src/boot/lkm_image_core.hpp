#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace ksud::boot::lkm_image {

enum class ErrorCode {
    kInvalidArgument,
    kOutOfRange,
    kOverflow,
    kUnsupported,
    kMalformedBootImage,
    kMalformedArm64Image,
    kKallsymsNotFound,
    kAmbiguous,
};

struct Error {
    ErrorCode code = ErrorCode::kInvalidArgument;
    std::string message;
};

template <typename T>
class Result {
public:
    static Result success(T value) { return Result(std::move(value)); }

    static Result failure(ErrorCode code, std::string message) {
        return Result(Error{code, std::move(message)});
    }

    explicit operator bool() const { return std::holds_alternative<T>(storage_); }
    bool has_value() const { return static_cast<bool>(*this); }

    T& value() { return std::get<T>(storage_); }
    const T& value() const { return std::get<T>(storage_); }
    T&& take_value() { return std::get<T>(std::move(storage_)); }

    Error& error() { return std::get<Error>(storage_); }
    const Error& error() const { return std::get<Error>(storage_); }

private:
    explicit Result(T value) : storage_(std::move(value)) {}
    explicit Result(Error error) : storage_(std::move(error)) {}

    std::variant<T, Error> storage_;
};

template <>
class Result<void> {
public:
    static Result success() { return Result(); }

    static Result failure(ErrorCode code, std::string message) {
        return Result(Error{code, std::move(message)});
    }

    explicit operator bool() const { return !error_.has_value(); }
    bool has_value() const { return static_cast<bool>(*this); }
    const Error& error() const { return *error_; }

private:
    Result() = default;
    explicit Result(Error error) : error_(std::move(error)) {}

    std::optional<Error> error_;
};

struct ByteRange {
    std::size_t offset = 0;
    std::size_t size = 0;

    bool empty() const { return size == 0; }
};

enum class BootHeaderKind {
    kLegacy,
    kV3,
    kV4,
};

struct BootImageInfo {
    BootHeaderKind kind = BootHeaderKind::kLegacy;
    std::uint32_t header_version = 0;
    std::uint32_t page_size = 0;
    std::uint32_t header_size = 0;
    ByteRange header;
    ByteRange kernel;
    ByteRange ramdisk;
    ByteRange second;
    ByteRange extra;
    ByteRange recovery_dtbo;
    std::uint64_t recovery_dtbo_offset = 0;
    ByteRange dtb;
    ByteRange signature;
    std::size_t payload_end = 0;
};

struct RawImageInfo {
    std::uint64_t text_offset = 0;
    std::size_t image_size = 0;
    std::uint64_t flags = 0;
};

Result<std::size_t> checked_add(std::size_t left, std::size_t right);
Result<std::size_t> checked_mul(std::size_t left, std::size_t right);
Result<std::size_t> checked_align_up(std::size_t value, std::size_t alignment);
Result<ByteRange> checked_range(std::size_t buffer_size, std::size_t offset, std::size_t length);
Result<std::uint16_t> read_u16_le(const std::uint8_t* data, std::size_t size, std::size_t offset);
Result<std::uint32_t> read_u32_le(const std::uint8_t* data, std::size_t size, std::size_t offset);
Result<std::int32_t> read_i32_le(const std::uint8_t* data, std::size_t size, std::size_t offset);
Result<std::uint64_t> read_u64_le(const std::uint8_t* data, std::size_t size, std::size_t offset);
Result<std::int64_t> read_i64_le(const std::uint8_t* data, std::size_t size, std::size_t offset);
Result<void> write_u32_le(std::uint8_t* data, std::size_t size, std::size_t offset,
                          std::uint32_t value);
Result<void> write_u64_le(std::uint8_t* data, std::size_t size, std::size_t offset,
                          std::uint64_t value);

Result<BootImageInfo> parse_boot_image(const std::uint8_t* data, std::size_t size);
Result<std::vector<std::uint8_t>> extract_boot_kernel(const std::uint8_t* data, std::size_t size);
Result<std::vector<std::uint8_t>> replace_boot_kernel(const std::uint8_t* data, std::size_t size,
                                                      const std::uint8_t* kernel,
                                                      std::size_t kernel_size);

inline Result<BootImageInfo> parse_boot_image(const std::vector<std::uint8_t>& data) {
    return parse_boot_image(data.data(), data.size());
}

inline Result<std::vector<std::uint8_t>> extract_boot_kernel(
    const std::vector<std::uint8_t>& data) {
    return extract_boot_kernel(data.data(), data.size());
}

inline Result<std::vector<std::uint8_t>> replace_boot_kernel(
    const std::vector<std::uint8_t>& data, const std::vector<std::uint8_t>& kernel) {
    return replace_boot_kernel(data.data(), data.size(), kernel.data(), kernel.size());
}

Result<RawImageInfo> parse_arm64_image(const std::uint8_t* data, std::size_t size);

inline Result<RawImageInfo> parse_arm64_image(const std::vector<std::uint8_t>& data) {
    return parse_arm64_image(data.data(), data.size());
}

struct MapSymbol {
    std::uint64_t address = 0;
    std::string name;
};

class SymbolMap {
public:
    static Result<SymbolMap> create(std::vector<MapSymbol> entries);

    Result<MapSymbol> resolve(const std::string& requested_name) const;
    std::optional<MapSymbol> resolve_module_symbol(const std::string& requested_name) const;
    std::vector<MapSymbol> variants(const std::string& requested_name) const;
    std::optional<std::uint64_t> next_address(std::uint64_t address) const;
    const std::vector<MapSymbol>& entries() const { return entries_; }

private:
    explicit SymbolMap(std::vector<MapSymbol> entries);

    std::vector<MapSymbol> entries_;
    std::unordered_map<std::string, std::vector<std::size_t>> by_name_;
    std::unordered_map<std::string, std::vector<std::size_t>> by_normalized_name_;
};

struct RecoveredKallsyms {
    SymbolMap symbols;
    std::string layout;
    std::size_t count = 0;
    std::size_t token_table_offset = 0;
    std::size_t token_index_offset = 0;
    std::size_t names_offset = 0;
    std::size_t markers_offset = 0;
    std::size_t offsets_offset = 0;
    std::size_t relative_base_offset = 0;
};

std::vector<RecoveredKallsyms> recover_arm64_kallsyms_candidates(const std::uint8_t* data,
                                                                 std::size_t size);
Result<RecoveredKallsyms> recover_arm64_kallsyms(const std::uint8_t* data, std::size_t size);

}  // namespace ksud::boot::lkm_image
