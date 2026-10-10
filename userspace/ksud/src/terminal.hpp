#pragma once

#include <cstdarg>
#include <cstdio>
#include <string>
#include <string_view>

namespace ksud::terminal {

enum class Color { Auto, Always, Never };

bool set_color(std::string_view value);
bool use_color(FILE* stream);
std::string escape(std::string_view text);
void heading(FILE* stream, std::string_view text);
void status(FILE* stream, std::string_view text, bool enabled);
void message(FILE* stream, std::string_view label, std::string_view text);
void diagnostics(std::string_view text);
int error(std::string_view text, std::string_view hint = {});
int errorf(const char* format, ...);
int verrorf(const char* format, va_list arguments);
int file_error(std::string_view operation, std::string_view path, int error_number);
int usage_error(std::string_view text, std::string_view usage);

}  // namespace ksud::terminal
