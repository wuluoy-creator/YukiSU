/**
 * ksuinit - Module Loader
 *
 * Handles loading the KernelSU LKM module with symbol resolution.
 */

#include "loader.hpp"
#include "log.hpp"
#include "module_symbols.hpp"
#include "vermagic.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <elf.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace ksuinit {

namespace {

constexpr size_t kMaximumKmsgCapture = size_t{256} * 1024;
constexpr size_t kMaximumKmsgRecords = 256;

// Bulk POSIX text I/O. The stream versions pulled in the whole locale/facet
// machinery, which is dead weight in a statically linked early-boot init.
bool read_text_file(const char* path, std::string* out) {
    const int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return false;
    }
    out->clear();
    char buffer[65536];
    bool ok = true;
    for (;;) {
        const ssize_t count = read(fd, buffer, sizeof(buffer));
        if (count > 0) {
            out->append(buffer, static_cast<size_t>(count));
            continue;
        }
        if (count == 0) {
            break;
        }
        if (errno == EINTR) {
            continue;
        }
        ok = false;
        break;
    }
    close(fd);
    if (!ok) {
        out->clear();
    }
    return ok;
}

bool write_text_file(const char* path, const std::string& text) {
    const int fd = open(path, O_WRONLY | O_CLOEXEC);
    if (fd < 0) {
        return false;
    }
    size_t written = 0;
    bool ok = true;
    while (written < text.size()) {
        const ssize_t count = write(fd, text.data() + written, text.size() - written);
        if (count > 0) {
            written += static_cast<size_t>(count);
            continue;
        }
        if (count < 0 && errno == EINTR) {
            continue;
        }
        ok = false;
        break;
    }
    close(fd);
    return ok;
}

/**
 * RAII class to manage kptr_restrict setting
 */
class KptrGuard {
public:
    KptrGuard() {
        std::string current;
        if (read_text_file(kPath, &current)) {
            // Keep only the first line, matching the previous std::getline.
            const size_t newline = current.find('\n');
            original_value_ = current.substr(0, newline);
            if (!original_value_.empty() && original_value_ != "1") {
                changed_ = write_text_file(kPath, "1");
            }
        }
    }

    KptrGuard(const KptrGuard&) = delete;
    KptrGuard& operator=(const KptrGuard&) = delete;
    KptrGuard(KptrGuard&&) = delete;
    KptrGuard& operator=(KptrGuard&&) = delete;

    ~KptrGuard() {
        if (changed_) {
            write_text_file(kPath, original_value_);
        }
    }

private:
    static constexpr const char* kPath = "/proc/sys/kernel/kptr_restrict";
    std::string original_value_;
    bool changed_ = false;
};

/**
 * Non-blocking reader positioned after all existing kernel log records.
 */
class KmsgReader {
public:
    KmsgReader() {
        constexpr std::array<const char*, 2> devices = {"/dev/kmsg", "/kmsg"};
        for (const char* device : devices) {
            const int descriptor = open(device, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
            if (descriptor < 0) {
                error_ = errno;
                continue;
            }
            if (lseek(descriptor, 0, SEEK_END) < 0) {
                error_ = errno;
                close(descriptor);
                continue;
            }

            descriptor_ = descriptor;
            device_ = device;
            error_ = 0;
            break;
        }
    }

    KmsgReader(const KmsgReader&) = delete;
    KmsgReader& operator=(const KmsgReader&) = delete;
    KmsgReader(KmsgReader&&) = delete;
    KmsgReader& operator=(KmsgReader&&) = delete;

    ~KmsgReader() {
        if (descriptor_ >= 0) {
            close(descriptor_);
        }
    }

    [[nodiscard]] bool is_open() const { return descriptor_ >= 0; }

    [[nodiscard]] int error() const { return error_; }

    [[nodiscard]] const char* device() const { return device_; }

    std::string read_new(int& read_error) const {
        std::string output;
        std::array<char, 8192> record{};
        read_error = 0;
        size_t records_read = 0;

        if (descriptor_ < 0) {
            read_error = EBADF;
            return output;
        }

        while (true) {
            const ssize_t length = read(descriptor_, record.data(), record.size());
            if (length > 0) {
                const size_t record_size = static_cast<size_t>(length);
                const bool needs_newline = record[record_size - 1] != '\n';
                const size_t required_space = record_size + (needs_newline ? 1 : 0);
                if (records_read >= kMaximumKmsgRecords ||
                    required_space > kMaximumKmsgCapture - output.size()) {
                    output.clear();
                    read_error = EOVERFLOW;
                    break;
                }

                output.append(record.data(), record_size);
                if (needs_newline) {
                    output.push_back('\n');
                }
                ++records_read;
                continue;
            }
            if (length == 0) {
                break;
            }
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            if (errno == EPIPE) {
                output.clear();
                read_error = EOVERFLOW;
                break;
            }

            read_error = errno;
            break;
        }

        return output;
    }

private:
    int descriptor_ = -1;
    int error_ = ENOENT;
    const char* device_ = nullptr;
};

/**
 * Parse /proc/kallsyms to get kernel symbol addresses
 */
bool parse_kallsyms(std::unordered_map<std::string_view, uint64_t>& symbols) {
    const KptrGuard guard;

    // /proc/kallsyms is several MB, so stream it rather than buffering it whole.
    const int fd = open("/proc/kallsyms", O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        KLOGE("Cannot open /proc/kallsyms: %s", strerror(errno));
        return false;
    }

    std::string pending;
    char buffer[65536];
    const auto handle_line = [&](std::string_view line) {
        std::string_view name;
        uint64_t addr = 0;
        if (!parse_kallsyms_line(line, name, addr)) {
            return;
        }
        const auto symbol = symbols.find(name);
        if (symbol != symbols.end()) {
            // Keep the previous last-match behavior for compiler aliases.
            symbol->second = addr;
        }
    };

    bool ok = true;
    for (;;) {
        const ssize_t count = read(fd, buffer, sizeof(buffer));
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            KLOGE("Cannot read /proc/kallsyms: %s", strerror(errno));
            ok = false;
            break;
        }
        if (count == 0) {
            break;
        }
        pending.append(buffer, static_cast<size_t>(count));
        size_t begin = 0;
        for (;;) {
            const size_t newline = pending.find('\n', begin);
            if (newline == std::string::npos) {
                break;
            }
            handle_line(std::string_view(pending.data() + begin, newline - begin));
            begin = newline + 1;
        }
        pending.erase(0, begin);
    }
    if (ok && !pending.empty()) {
        handle_line(pending);
    }
    close(fd);

    return ok;
}

/**
 * Read entire file into a vector
 */
bool read_file(const char* path, std::vector<uint8_t>& buffer) {
    const int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        KLOGE("Cannot open file: %s", path);
        return false;
    }

    struct stat status{};
    if (fstat(fd, &status) != 0) {
        KLOGE("Cannot stat file: %s", path);
        close(fd);
        return false;
    }
    if (!S_ISREG(status.st_mode) || status.st_size < static_cast<off_t>(sizeof(Elf64_Ehdr)) ||
        static_cast<uint64_t>(status.st_size) > buffer.max_size() ||
        static_cast<uint64_t>(status.st_size) > std::numeric_limits<ssize_t>::max()) {
        KLOGE("Invalid module file size or type: %s", path);
        close(fd);
        return false;
    }

    buffer.resize(static_cast<size_t>(status.st_size));
    size_t filled = 0;
    while (filled < buffer.size()) {
        const ssize_t count = read(fd, buffer.data() + filled, buffer.size() - filled);
        if (count > 0) {
            filled += static_cast<size_t>(count);
            continue;
        }
        if (count < 0 && errno == EINTR) {
            continue;
        }
        KLOGE("Cannot read file: %s", path);
        close(fd);
        return false;
    }
    close(fd);

    return true;
}

/**
 * Call init_module syscall
 */
int init_module_syscall(void* module_image, unsigned long len, const char* param_values) {
    return syscall(__NR_init_module, module_image, len, param_values);
}

}  // anonymous namespace

bool load_module(const char* path) {
    // Check if we're PID 1 (init process)
    if (getpid() != 1) {
        KLOGE("Invalid process (not init)");
        return false;
    }

    // Read the module file
    std::vector<uint8_t> buffer;
    if (!read_file(path, buffer)) {
        return false;
    }

    // Retain only names needed by this module instead of allocating a hash
    // table for every kernel symbol. Release the views before vermagic can
    // resize the module image.
    {
        std::vector<ModuleSymbol> undefined_symbols;
        std::string error;
        if (!collect_undefined_symbols(buffer, undefined_symbols, error)) {
            KLOGE("Cannot parse module symbols: %s", error.c_str());
            return false;
        }
        std::unordered_map<std::string_view, uint64_t> kernel_symbols;
        kernel_symbols.reserve(undefined_symbols.size());
        for (const auto& symbol : undefined_symbols) {
            kernel_symbols.emplace(symbol.name, 0);
        }
        if (!kernel_symbols.empty() && !parse_kallsyms(kernel_symbols)) {
            return false;
        }
        for (const auto& symbol : undefined_symbols) {
            const auto resolved = kernel_symbols.find(symbol.name);
            if (resolved == kernel_symbols.end() || resolved->second == 0) {
                const int name_length = static_cast<int>(std::min(symbol.name.size(), size_t{512}));
                // Names have a validated NUL and the format also bounds their length.
                // NOLINTNEXTLINE(bugprone-suspicious-stringview-data-usage)
                KLOGW("Cannot find symbol: %.*s", name_length, symbol.name.data());
                continue;
            }
            Elf64_Sym entry{};
            std::memcpy(&entry, buffer.data() + symbol.offset, sizeof(entry));
            entry.st_shndx = SHN_ABS;
            entry.st_value = resolved->second;
            std::memcpy(buffer.data() + symbol.offset, &entry, sizeof(entry));
        }
    }

    std::string param_values;
    bool config_loaded = false;
    {
        if (read_text_file("/ksu_config", &param_values)) {
            config_loaded = true;
            while (!param_values.empty() &&
                   (param_values.back() == '\n' || param_values.back() == '\r' ||
                    param_values.back() == '\0')) {
                param_values.pop_back();
            }
        }
    }
    if (access("/ksu_allow_shell", F_OK) == 0) {
        KLOGW("ksu allow shell at init");
        if (!param_values.empty()) {
            param_values += " ";
        }
        param_values += "allow_shell=1";
    }
    if (config_loaded) {
        KLOGI("Loading module configuration (%zu bytes)", param_values.size());
    }

    const KmsgReader kmsg_reader;
    if (kmsg_reader.is_open()) {
        KLOGI("Watching kernel log from %s for module load errors", kmsg_reader.device());
    } else {
        KLOGW("Cannot prepare kernel log fallback: %s", strerror(kmsg_reader.error()));
    }

    // Load the module. A version-magic mismatch returns ENOEXEC; in that exact
    // case, use the new kmsg record to safely patch .modinfo and retry once.
    int module_result = init_module_syscall(buffer.data(), buffer.size(), param_values.c_str());
    int module_errno = errno;
    if (module_result != 0 && module_errno == ENOEXEC && kmsg_reader.is_open()) {
        int read_error = 0;
        const std::string new_kmsg = kmsg_reader.read_new(read_error);
        if (read_error != 0) {
            KLOGW("Cannot read new kernel log records: %s", strerror(read_error));
        } else {
            VermagicMismatch mismatch;
            if (extract_vermagic_mismatch(new_kmsg, mismatch)) {
                std::string replacement_error;
                if (replace_module_vermagic(buffer, mismatch.module_vermagic,
                                            mismatch.required_vermagic, replacement_error)) {
                    KLOGW("Retrying module load with kernel-required vermagic: %s",
                          mismatch.required_vermagic.c_str());
                    module_result =
                        init_module_syscall(buffer.data(), buffer.size(), param_values.c_str());
                    module_errno = errno;
                } else {
                    KLOGE("Cannot replace module vermagic: %s", replacement_error.c_str());
                }
            } else {
                KLOGW("init_module returned ENOEXEC without a matching vermagic record");
            }
        }
    }

    std::fill(param_values.begin(), param_values.end(), '\0');
    if (module_result != 0) {
        KLOGE("init_module failed: %s", strerror(module_errno));
        return false;
    }

    if (config_loaded && unlink("/ksu_config") != 0 && errno != ENOENT) {
        KLOGW("Failed to remove module configuration: %s", strerror(errno));
    }
    KLOGI("Module loaded successfully");
    return true;
}

}  // namespace ksuinit
