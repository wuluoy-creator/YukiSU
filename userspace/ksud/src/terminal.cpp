#include "terminal.hpp"

#include <unistd.h>
#include <cerrno>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace ksud::terminal {
namespace {
Color color = Color::Auto;

void styled(FILE* stream, const char* style, std::string_view text) {
    const bool enabled = use_color(stream);
    (void)fprintf(stream, "%s%.*s%s", enabled ? style : "", static_cast<int>(text.size()),
                  text.data(), enabled ? "\033[0m" : "");
}
}  // namespace

bool set_color(std::string_view value) {
    if (value == "auto")
        color = Color::Auto;
    else if (value == "always")
        color = Color::Always;
    else if (value == "never")
        color = Color::Never;
    else
        return false;
    return true;
}

bool use_color(FILE* stream) {
    if (color != Color::Auto)
        return color == Color::Always;
    const char* no_color = getenv("NO_COLOR");
    const char* term = getenv("TERM");
    return !(no_color && *no_color) && term && *term && strcmp(term, "dumb") != 0 &&
           isatty(fileno(stream)) == 1;
}

std::string escape(std::string_view text) {
    std::string result;
    constexpr std::string_view hex = "0123456789abcdef";
    for (const unsigned char ch : text) {
        if (ch < 0x20 || ch == 0x7f) {
            result += "\\x";
            result += hex[ch >> 4];
            result += hex[ch & 0xf];
        } else {
            result += static_cast<char>(ch);
        }
    }
    return result;
}

void heading(FILE* stream, std::string_view text) {
    styled(stream, "\033[1;36m", text);
}

void status(FILE* stream, std::string_view text, bool enabled) {
    styled(stream, enabled ? "\033[1;32m" : "\033[2m", text);
}

void message(FILE* stream, std::string_view label, std::string_view text) {
    const char* style = "\033[1;36m";
    if (label == "error")
        style = "\033[1;31m";
    else if (label == "warning")
        style = "\033[1;33m";
    else if (label == "success")
        style = "\033[1;32m";
    styled(stream, style, label);
    (void)fprintf(stream, ": %s\n", escape(text).c_str());
}

void diagnostics(std::string_view text) {
    while (!text.empty()) {
        const size_t end = text.find('\n');
        auto line = text.substr(0, end);
        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);
        if (!line.empty()) {
            if (line.substr(0, 7) == "error: ")
                line.remove_prefix(7);
            message(stderr, "error", line);
        }
        if (end == std::string_view::npos)
            break;
        text.remove_prefix(end + 1);
    }
}

int error(std::string_view text, std::string_view hint) {
    diagnostics(text);
    if (!hint.empty())
        message(stderr, "hint", hint);
    return 1;
}

int verrorf(const char* format, va_list arguments) {
    va_list copy;
    va_copy(copy, arguments);
    const int length = vsnprintf(nullptr, 0, format, copy);
    va_end(copy);
    if (length < 0) {
        return error("cannot format diagnostic");
    }
    std::vector<char> buffer(static_cast<size_t>(length) + 1);
    const int written = vsnprintf(buffer.data(), buffer.size(), format, arguments);
    return written < 0 ? error("cannot format diagnostic") : error(buffer.data());
}

int errorf(const char* format, ...) {
    va_list arguments;
    va_start(arguments, format);
    const int result = verrorf(format, arguments);
    va_end(arguments);
    return result;
}

int file_error(std::string_view operation, std::string_view path, int error_number) {
    std::string text(operation);
    text += " '" + std::string(path) + "': " + strerror(error_number);
    std::string_view hint;
    if (error_number == ENOENT)
        hint = "Check the path and that its parent directory exists.";
    else if (error_number == EACCES || error_number == EPERM)
        hint = "Check file access permissions and the current root/SELinux context.";
    else if (error_number == EROFS)
        hint = "Choose a destination on a writable filesystem.";
    else if (error_number == ENOSPC)
        hint = "Free space on the destination filesystem and retry.";
    return error(text, hint);
}

int usage_error(std::string_view text, std::string_view usage) {
    message(stderr, "error", text);
    if (!usage.empty()) {
        (void)fputc('\n', stderr);
        heading(stderr, "Usage: ");
        (void)fprintf(stderr, "%.*s\n", static_cast<int>(usage.size()), usage.data());
    }
    return 2;
}

}  // namespace ksud::terminal
