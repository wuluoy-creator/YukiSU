#include "boot/boot_image_btf.hpp"

#include <cstdint>
#include <exception>
#include <iostream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace {

using ksud::boot::lkm_image::find_btf_candidates;
using ksud::boot::lkm_image::KernelBtf;
using ksud::boot::lkm_image::LoadInfoLayout;

constexpr std::uint16_t kBtfMagic = 0xeb9f;
constexpr std::uint8_t kBtfKindInt = 1;
constexpr std::uint8_t kBtfKindPtr = 2;
constexpr std::uint8_t kBtfKindStruct = 4;
constexpr std::uint8_t kBtfKindFunc = 12;
constexpr std::uint8_t kBtfKindFuncProto = 13;

int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void push_u16(std::vector<std::uint8_t>& output, std::uint16_t value) {
    output.push_back(static_cast<std::uint8_t>(value));
    output.push_back(static_cast<std::uint8_t>(value >> 8));
}

void push_u32(std::vector<std::uint8_t>& output, std::uint32_t value) {
    output.push_back(static_cast<std::uint8_t>(value));
    output.push_back(static_cast<std::uint8_t>(value >> 8));
    output.push_back(static_cast<std::uint8_t>(value >> 16));
    output.push_back(static_cast<std::uint8_t>(value >> 24));
}

class Strings {
public:
    Strings() : data_{0} {}

    std::uint32_t add(const char* value) {
        const auto offset = static_cast<std::uint32_t>(data_.size());
        while (*value != '\0') {
            data_.push_back(static_cast<std::uint8_t>(*value++));
        }
        data_.push_back(0);
        return offset;
    }

    [[nodiscard]] const std::vector<std::uint8_t>& data() const { return data_; }

private:
    std::vector<std::uint8_t> data_;
};

void push_type(std::vector<std::uint8_t>& output, std::uint32_t name, std::uint8_t kind,
               std::uint16_t vlen, std::uint32_t size_or_type) {
    push_u32(output, name);
    push_u32(output, static_cast<std::uint32_t>(kind) << 24 | vlen);
    push_u32(output, size_or_type);
}

std::vector<std::uint8_t> build_btf(
    const std::vector<std::tuple<std::uint32_t, std::uint32_t, std::uint32_t>>& layouts,
    const std::vector<std::uint32_t>& loading_modules, std::uint16_t load_module_arity) {
    (void)loading_modules;
    Strings strings;
    const auto unsigned_long = strings.add("unsigned long");
    const auto hdr = strings.add("hdr");
    const auto len = strings.add("len");
    const auto load_info = strings.add("load_info");
    const auto load_module = strings.add("load_module");
    const auto parameter = strings.add("parameter");

    std::vector<std::uint8_t> types;
    push_type(types, unsigned_long, kBtfKindInt, 0, 8);
    push_u32(types, 64);
    push_type(types, 0, kBtfKindPtr, 0, 1);
    for (const auto& [size, hdr_offset, len_offset] : layouts) {
        push_type(types, load_info, kBtfKindStruct, 2, size);
        push_u32(types, hdr);
        push_u32(types, 2);
        push_u32(types, hdr_offset * 8);
        push_u32(types, len);
        push_u32(types, 1);
        push_u32(types, len_offset * 8);
    }
    if (!layouts.empty()) {
        constexpr std::uint32_t first_load_info_id = 3;
        const auto pointer_id = static_cast<std::uint32_t>(3 + layouts.size());
        push_type(types, 0, kBtfKindPtr, 0, first_load_info_id);
        const auto prototype_id = pointer_id + 1;
        push_type(types, 0, kBtfKindFuncProto, load_module_arity, 1);
        for (std::uint16_t index = 0; index < load_module_arity; ++index) {
            push_u32(types, parameter);
            push_u32(types, index == 0 ? pointer_id : 1);
        }
        push_type(types, load_module, kBtfKindFunc, 0, prototype_id);
    }

    std::vector<std::uint8_t> output;
    push_u16(output, kBtfMagic);
    output.push_back(1);
    output.push_back(0);
    push_u32(output, 24);
    push_u32(output, 0);
    push_u32(output, static_cast<std::uint32_t>(types.size()));
    push_u32(output, static_cast<std::uint32_t>(types.size()));
    push_u32(output, static_cast<std::uint32_t>(strings.data().size()));
    output.insert(output.end(), types.begin(), types.end());
    output.insert(output.end(), strings.data().begin(), strings.data().end());
    return output;
}

bool recover_single(const std::vector<std::uint8_t>& image, KernelBtf& result, std::string& error) {
    const auto candidates = find_btf_candidates(image);
    expect(candidates.size() == 1, "expected one BTF candidate");
    return candidates.size() == 1 && candidates[0].inspect(result, &error);
}

void test_recovers_load_info() {
    const auto blob = build_btf({{136, 16, 24}}, {2}, 3);
    std::vector<std::uint8_t> image(32, 0xaa);
    image.insert(image.end(), blob.begin(), blob.end());

    KernelBtf btf{};
    std::string error;
    expect(recover_single(image, btf, error), "valid BTF should inspect successfully");
    expect(error.empty(), "valid BTF should not return an error");
    expect(btf.file_offset == 32, "BTF file offset should be recovered");
    expect(btf.size == blob.size(), "BTF blob size should be recovered");
    expect(btf.load_info == LoadInfoLayout{136, 16, 24}, "load_info layout should be recovered");
}

void test_candidate_scanning() {
    const auto blob = build_btf({{136, 16, 24}}, {2}, 3);
    std::vector<std::uint8_t> false_magic = {0x9f, 0xeb, 0x01, 0x00};
    false_magic.insert(false_magic.end(), 20, 0);
    false_magic.insert(false_magic.end(), blob.begin(), blob.end());
    expect(find_btf_candidates(false_magic).size() == 1,
           "false magic should not hide a later BTF blob");

    auto duplicate = blob;
    duplicate.insert(duplicate.end(), blob.begin(), blob.end());
    const auto candidates = find_btf_candidates(duplicate);
    expect(candidates.size() == 2, "two valid BTF blobs should remain distinct candidates");
    if (candidates.size() == 2) {
        expect(candidates[0].file_offset() == 0, "first candidate offset should be zero");
        expect(candidates[1].file_offset() == blob.size(),
               "second candidate offset should follow the first blob");
    }

    expect(find_btf_candidates(false_magic.data(), 4).empty(),
           "truncated BTF magic should not form a candidate");
}

void test_conflicting_load_info_layouts() {
    const auto blob = build_btf({{136, 16, 24}, {152, 24, 32}}, {2}, 3);
    const auto candidates = find_btf_candidates(blob);
    expect(candidates.size() == 1, "semantic ABI conflict should remain a structural candidate");
    if (candidates.empty()) {
        return;
    }
    KernelBtf btf{};
    std::string error;
    expect(!candidates[0].inspect(btf, &error), "conflicting layouts should fail inspection");
    expect(error.find("conflicting struct load_info layouts") != std::string::npos,
           "layout conflict should return a precise error");
}

void test_incompatible_function_arity() {
    const auto blob = build_btf({{136, 16, 24}}, {2}, 2);
    const auto candidates = find_btf_candidates(blob);
    expect(candidates.size() == 1, "incompatible ABI should remain a structural candidate");
    if (candidates.empty()) {
        return;
    }
    KernelBtf btf{};
    std::string error;
    expect(!candidates[0].inspect(btf, &error), "wrong load_module arity should fail inspection");
    expect(error.find("load_module has 2 parameters") != std::string::npos,
           "arity error should name load_module and its parameter count");
}

void test_missing_abi_records() {
    const auto blob = build_btf({}, {}, 0);
    KernelBtf btf{};
    std::string error;
    expect(recover_single(blob, btf, error), "minimal BTF should permit built-in ABI fallback");
    expect(!btf.load_info, "missing load_info should be represented as absent");
}

}  // namespace

int main() {
    try {
        test_recovers_load_info();
        test_candidate_scanning();
        test_conflicting_load_info_layouts();
        test_incompatible_function_arity();
        test_missing_abi_records();
        if (failures != 0) {
            std::cerr << failures << " test assertion(s) failed\n";
            return 1;
        }
        std::cout << "boot_image_btf_test: all tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "unexpected exception: " << error.what() << '\n';
        return 1;
    } catch (...) {
        std::cerr << "unexpected non-standard exception\n";
        return 1;
    }
}
