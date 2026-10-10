#pragma once

#include <string>
#include <vector>

namespace sumhp {

int run_daemon_command(const std::vector<std::string>& args);
int run_via_daemon(const std::vector<std::string>& args, bool display_errors = false);

}  // namespace sumhp
