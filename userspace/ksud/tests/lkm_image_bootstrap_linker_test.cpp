#include "../src/boot/lkm_image_bootstrap_linker.hpp"
#include "../src/boot/lkm_image_bootstrap.hpp"

#include <cstdint>
#include <exception>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace {

using ksud::boot::lkm_image::BootstrapDefinition;
using ksud::boot::lkm_image::BootstrapObjectView;
using ksud::boot::lkm_image::LinkedBootstrap;

std::vector<std::uint8_t> read_file(const char* path) {
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

std::vector<BootstrapDefinition> definitions(std::uint64_t base) {
    using namespace ksud::boot::lkm_image::symbol;
    const auto at = [base](std::string_view name, std::uint64_t offset) {
        return BootstrapDefinition{name, base + offset};
    };
    return {
        at(kExtAsyncSynchronizeFull, 0x10000),
        at(kExtCapable, 0x10020),
        at(kExtModulesDisabled, 0x20000),
        at(kImageBase, 0),
        at(kExtKimageVoffset, 0x20008),
        at(kExtMemstartAddr, 0x20010),
        at(kExtSecurityKernelLoadData, 0x10040),
        at(kExtVmalloc, 0x10060),
        at(kExtMemcpy, 0x10080),
        at(kExtSecurityKernelPostLoadData, 0x100a0),
        at(kExtLoadModule, 0x100c0),
        at(kExtVfree, 0x100e0),
        at(kExtMemblockReserve, 0x10100),
        at(kExtStrndupUser, 0x10120),
        at(kExtKstrdup, 0x10140),
        {kCapSysModule, 16},
        {kCapsuleImageOffset, 0x200000},
        {kPageOffset, 0xffff000000000000ULL},
        {kCapsuleMagic, 0x314d4b4c55534bULL},
        {kCapsuleVersion, 1},
        {kCapsuleHeaderSize, 96},
        {kCapsuleSize, 0x1000},
        {kModuleCapsuleOffset, 96},
        {kModuleSize, 0x800},
        {kFixupCapsuleOffset, 0x900},
        {kFixupCount, 16},
        {kLoadingModuleId, 2},
        {kLoadInfoSize, 256},
        {kLoadInfoHdrOffset, 16},
        {kLoadInfoLenOffset, 24},
        {kReserveExtension, 0x1000},
        {kGfpKernel, 0xcc0},
    };
}

}  // namespace

int run(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: linker_test <bootstrap.o>\n";
        return 2;
    }
    const auto bytes = read_file(argv[1]);
    if (bytes.empty()) {
        std::cerr << "failed to read bootstrap object\n";
        return 1;
    }
    constexpr std::uint64_t code_address = 0xffffffc008100000ULL;
    const BootstrapObjectView object{bytes.data(), bytes.size()};
    std::string error;
    const std::size_t size = ksud::boot::lkm_image::bootstrap_image_size(object, &error);
    if (size == 0) {
        std::cerr << error << '\n';
        return 1;
    }
    LinkedBootstrap linked;
    if (!ksud::boot::lkm_image::link_bootstrap(object, code_address, definitions(code_address),
                                               &linked, &error)) {
        std::cerr << error << '\n';
        return 1;
    }
    if (linked.data.size() != size || linked.entry_address != code_address ||
        linked.reserve_wrapper_address <= code_address ||
        linked.strndup_adapter_address <= linked.reserve_wrapper_address) {
        std::cerr << "linked bootstrap metadata is inconsistent\n";
        return 1;
    }
    std::cout << "lkm_image_bootstrap_linker_test: all tests passed\n";
    return 0;
}

int main(int argc, char** argv) {
    try {
        return run(argc, argv);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
