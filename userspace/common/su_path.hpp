#pragma once

#include "uapi/su_path.h"

#include <cerrno>
#include <string>
#include <string_view>

namespace ksud {

inline int validate_su_path(std::string_view path) {
    if (path.size() >= KSU_SU_PATH_MAX)
        return -ENAMETOOLONG;
    if (path.size() < 2 || path.front() != '/')
        return -EINVAL;
    size_t start = 1;
    for (size_t i = 1; i <= path.size(); ++i) {
        if (i < path.size()) {
            if (static_cast<unsigned char>(path[i]) < 0x20)
                return -EINVAL;
            if (path[i] != '/')
                continue;
        }
        const auto part = path.substr(start, i - start);
        if (part.empty() || part == "." || part == "..")
            return -EINVAL;
        if (part.size() > 255)
            return -ENAMETOOLONG;
        start = i + 1;
    }
    return 0;
}

inline int parse_su_path_file(std::string& content) {
    if (!content.empty() && content.back() == '\n') {
        content.pop_back();
        if (!content.empty() && content.back() == '\r')
            content.pop_back();
    }
    return validate_su_path(content);
}

}  // namespace ksud
