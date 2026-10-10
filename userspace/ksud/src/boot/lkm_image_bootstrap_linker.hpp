#pragma once

#include "lkm_image_bootstrap.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ksud::boot::lkm_image {

struct LinkedBootstrap {
    std::vector<std::uint8_t> data;
    std::uint64_t entry_address = 0;
    std::uint64_t reserve_wrapper_address = 0;
    std::uint64_t strndup_adapter_address = 0;
};

std::size_t bootstrap_image_size(const BootstrapObjectView& object, std::string* error = nullptr);

bool link_bootstrap(const BootstrapObjectView& object, std::uint64_t code_address,
                    const std::vector<BootstrapDefinition>& definitions, LinkedBootstrap* linked,
                    std::string* error = nullptr);

}  // namespace ksud::boot::lkm_image
