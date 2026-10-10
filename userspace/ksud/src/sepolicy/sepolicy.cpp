#include "sepolicy.hpp"
#include "../core/ksucalls.hpp"
#include "../log.hpp"
#include "../utils.hpp"
#include "xperm_parser.hpp"

#include <sys/stat.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ksud {

// Constants matching kernel interface
static constexpr size_t SEPOLICY_MAX_LEN = 128;

static constexpr uint32_t CMD_NORMAL_PERM = 1;
static constexpr uint32_t CMD_XPERM = 2;
static constexpr uint32_t CMD_TYPE_STATE = 3;
static constexpr uint32_t CMD_TYPE = 4;
static constexpr uint32_t CMD_TYPE_ATTR = 5;
static constexpr uint32_t CMD_ATTR = 6;
static constexpr uint32_t CMD_TYPE_TRANSITION = 7;
static constexpr uint32_t CMD_TYPE_CHANGE = 8;
static constexpr uint32_t CMD_GENFSCON = 9;

// Subcmd for CMD_NORMAL_PERM
static constexpr uint32_t SUBCMD_ALLOW = 1;
static constexpr uint32_t SUBCMD_DENY = 2;
static constexpr uint32_t SUBCMD_AUDITALLOW = 3;
static constexpr uint32_t SUBCMD_DONTAUDIT = 4;

// Subcmd for CMD_XPERM
static constexpr uint32_t SUBCMD_ALLOWXPERM = 1;
static constexpr uint32_t SUBCMD_AUDITALLOWXPERM = 2;
static constexpr uint32_t SUBCMD_DONTAUDITXPERM = 3;

// Subcmd for CMD_TYPE_STATE
static constexpr uint32_t SUBCMD_PERMISSIVE = 1;
static constexpr uint32_t SUBCMD_ENFORCING = 2;

// Subcmd for CMD_TYPE_CHANGE
static constexpr uint32_t SUBCMD_TYPE_CHANGE = 1;
static constexpr uint32_t SUBCMD_TYPE_MEMBER = 2;

// PolicyObject - holds a sepolicy string or represents "all" (*)
class PolicyObject {
public:
    enum class Type : std::uint8_t { NONE, ALL, ONE, INVALID };

    PolicyObject() = default;

    static PolicyObject none() { return {}; }

    static PolicyObject all() {
        PolicyObject obj;
        obj.type_ = Type::ALL;
        return obj;
    }

    static PolicyObject from_str(const std::string& s) {
        PolicyObject obj;
        if (s == "*") {
            obj.type_ = Type::ALL;
        } else if (!s.empty() && s.length() < SEPOLICY_MAX_LEN) {
            obj.type_ = Type::ONE;
            (void)strncpy(obj.buf_.data(), s.c_str(), SEPOLICY_MAX_LEN - 1);
            obj.buf_[SEPOLICY_MAX_LEN - 1] = '\0';
        } else {
            obj.type_ = Type::INVALID;
        }
        return obj;
    }

    [[nodiscard]] const char* c_ptr() const {
        if (type_ == Type::ONE) {
            return buf_.data();
        }
        return nullptr;  // NULL for NONE and ALL
    }

    [[nodiscard]] Type type() const { return type_; }
    [[nodiscard]] bool valid() const { return type_ != Type::INVALID; }

private:
    Type type_{Type::NONE};
    std::array<char, SEPOLICY_MAX_LEN> buf_{};
};

// AtomicStatement - a single sepolicy operation to send to kernel (aggregate for FFI)
struct AtomicStatement {
    // NOLINTBEGIN(misc-non-private-member-variables-in-classes)
    uint32_t cmd{};
    uint32_t subcmd{};
    PolicyObject sepol1;
    PolicyObject sepol2;
    PolicyObject sepol3;
    PolicyObject sepol4;
    PolicyObject sepol5;
    PolicyObject sepol6;
    PolicyObject sepol7;
    // NOLINTEND(misc-non-private-member-variables-in-classes)

    [[nodiscard]] std::array<const PolicyObject*, 7> args() const {
        return {&sepol1, &sepol2, &sepol3, &sepol4, &sepol5, &sepol6, &sepol7};
    }

    [[nodiscard]] bool valid() const {
        const auto values = args();
        return std::all_of(values.begin(), values.end(),
                           [](const PolicyObject* value) { return value->valid(); });
    }
};

namespace {

// Helper: check if char is valid in sepolicy identifier
bool is_sepolicy_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-';
}

// Helper: skip whitespace
const char* skip_space(const char* p) {
    while (*p && std::isspace(static_cast<unsigned char>(*p)))
        p++;
    return p;
}

// Helper: parse a single word
const char* parse_word(const char* p, std::string& out) {
    out.clear();
    while (*p && is_sepolicy_char(*p)) {
        out += *p++;
    }
    return p;
}

// Helper: parse objects (single word, {word1 word2 ...}, or *)
const char* parse_seobj(const char* p, std::vector<std::string>& out) {
    out.clear();
    p = skip_space(p);

    if (*p == '*') {
        out.push_back("*");
        return p + 1;
    }

    if (*p == '{') {
        p++;  // skip '{'
        while (*p && *p != '}') {
            p = skip_space(p);
            if (*p == '}')
                break;
            std::string word;
            p = parse_word(p, word);
            if (!word.empty()) {
                out.push_back(word);
            }
            p = skip_space(p);
        }
        if (*p == '}')
            p++;
        return p;
    }

    // Single word
    std::string word;
    p = parse_word(p, word);
    if (!word.empty()) {
        out.push_back(word);
    }
    return p;
}

const char* parse_seobj_strict(const char* p, std::vector<std::string>& out) {
    out.clear();
    if (*p == '*') {
        out.push_back("*");
        return p + 1;
    }

    if (*p == '{') {
        ++p;
        for (;;) {
            p = skip_space(p);
            if (*p == '}')
                return out.empty() ? nullptr : p + 1;
            if (*p == '\0')
                return nullptr;

            std::string word;
            const char* next = parse_word(p, word);
            if (word.empty())
                return nullptr;
            out.push_back(std::move(word));
            p = next;
        }
    }

    std::string word;
    p = parse_word(p, word);
    if (word.empty())
        return nullptr;
    out.push_back(std::move(word));
    return p;
}

const char* skip_required_space(const char* p) {
    if (*p == '\0' || !std::isspace(static_cast<unsigned char>(*p)))
        return nullptr;
    return skip_space(p);
}

const char* parse_class(const char* p, std::vector<std::string>& classes) {
    if (*p == ':') {
        ++p;
        p = skip_space(p);
        return parse_seobj_strict(p, classes);
    }

    p = skip_required_space(p);
    if (p == nullptr)
        return nullptr;
    if (*p == ':') {
        ++p;
        p = skip_space(p);
    }
    return parse_seobj_strict(p, classes);
}

// Parse and expand a single rule into AtomicStatements
bool parse_rule(const std::string& rule, std::vector<AtomicStatement>& statements) {
    const char* p = rule.c_str();
    p = skip_space(p);

    if (*p == '\0' || *p == '#') {
        return true;  // Empty or comment
    }

    std::string cmd_str;
    p = parse_word(p, cmd_str);

    // allow/deny/auditallow/dontaudit source target:class perm
    if (cmd_str == "allow" || cmd_str == "deny" || cmd_str == "auditallow" ||
        cmd_str == "dontaudit") {
        uint32_t subcmd;
        if (cmd_str == "allow") {
            subcmd = SUBCMD_ALLOW;
        } else if (cmd_str == "deny") {
            subcmd = SUBCMD_DENY;
        } else if (cmd_str == "auditallow") {
            subcmd = SUBCMD_AUDITALLOW;
        } else {
            subcmd = SUBCMD_DONTAUDIT;
        }

        std::vector<std::string> sources;
        std::vector<std::string> targets;
        std::vector<std::string> classes;
        std::vector<std::string> perms;

        p = skip_required_space(p);
        if (p == nullptr)
            return false;
        p = parse_seobj_strict(p, sources);
        if (p == nullptr)
            return false;
        p = skip_required_space(p);
        if (p == nullptr)
            return false;
        p = parse_seobj_strict(p, targets);
        if (p == nullptr)
            return false;
        p = parse_class(p, classes);
        if (p == nullptr)
            return false;
        p = skip_required_space(p);
        if (p == nullptr)
            return false;
        p = parse_seobj_strict(p, perms);
        if (p == nullptr)
            return false;
        if (*skip_space(p) != '\0')
            return false;

        // Expand to atomic statements
        for (const auto& s : sources) {
            for (const auto& t : targets) {
                for (const auto& c : classes) {
                    for (const auto& perm : perms) {
                        AtomicStatement stmt;
                        stmt.cmd = CMD_NORMAL_PERM;
                        stmt.subcmd = subcmd;
                        stmt.sepol1 = PolicyObject::from_str(s);
                        stmt.sepol2 = PolicyObject::from_str(t);
                        stmt.sepol3 = PolicyObject::from_str(c);
                        stmt.sepol4 = PolicyObject::from_str(perm);
                        statements.push_back(stmt);
                    }
                }
            }
        }
        return true;
    }

    // allowxperm/auditallowxperm/dontauditxperm source target:class operation xperm_set
    if (cmd_str == "allowxperm" || cmd_str == "auditallowxperm" || cmd_str == "dontauditxperm") {
        uint32_t subcmd;
        if (cmd_str == "allowxperm") {
            subcmd = SUBCMD_ALLOWXPERM;
        } else if (cmd_str == "auditallowxperm") {
            subcmd = SUBCMD_AUDITALLOWXPERM;
        } else {
            subcmd = SUBCMD_DONTAUDITXPERM;
        }

        std::vector<std::string> sources;
        std::vector<std::string> targets;
        std::vector<std::string> classes;
        std::string operation;
        std::vector<std::string> perm_sets;

        p = skip_required_space(p);
        if (p == nullptr)
            return false;
        p = parse_seobj_strict(p, sources);
        if (p == nullptr)
            return false;
        p = skip_required_space(p);
        if (p == nullptr)
            return false;
        p = parse_seobj_strict(p, targets);
        if (p == nullptr)
            return false;
        p = parse_class(p, classes);
        if (p == nullptr)
            return false;
        p = skip_required_space(p);
        if (p == nullptr)
            return false;
        p = parse_word(p, operation);
        if (operation.empty())
            return false;
        p = skip_required_space(p);
        if (p == nullptr)
            return false;
        auto parsed_perm_sets = parse_xperm_set(p);
        if (!parsed_perm_sets)
            return false;
        perm_sets = std::move(*parsed_perm_sets);

        for (const auto& s : sources) {
            for (const auto& t : targets) {
                for (const auto& c : classes) {
                    for (const auto& perm_set : perm_sets) {
                        AtomicStatement stmt;
                        stmt.cmd = CMD_XPERM;
                        stmt.subcmd = subcmd;
                        stmt.sepol1 = PolicyObject::from_str(s);
                        stmt.sepol2 = PolicyObject::from_str(t);
                        stmt.sepol3 = PolicyObject::from_str(c);
                        stmt.sepol4 = PolicyObject::from_str(operation);
                        stmt.sepol5 = PolicyObject::from_str(perm_set);
                        statements.push_back(stmt);
                    }
                }
            }
        }
        return true;
    }

    // permissive/enforce type
    if (cmd_str == "permissive" || cmd_str == "enforce") {
        const uint32_t subcmd = (cmd_str == "permissive") ? SUBCMD_PERMISSIVE : SUBCMD_ENFORCING;

        std::vector<std::string> types;
        p = parse_seobj(p, types);

        for (const auto& t : types) {
            AtomicStatement stmt;
            stmt.cmd = CMD_TYPE_STATE;
            stmt.subcmd = subcmd;
            stmt.sepol1 = PolicyObject::from_str(t);
            statements.push_back(stmt);
        }
        return true;
    }

    // type type_name attr1 attr2 ...
    if (cmd_str == "type") {
        std::string type_name;
        p = skip_space(p);
        p = parse_word(p, type_name);

        std::vector<std::string> attrs;
        p = parse_seobj(p, attrs);

        if (attrs.empty()) {
            // Type with no attributes
            AtomicStatement stmt;
            stmt.cmd = CMD_TYPE;
            stmt.subcmd = 0;
            stmt.sepol1 = PolicyObject::from_str(type_name);
            statements.push_back(stmt);
        } else {
            for (const auto& attr : attrs) {
                AtomicStatement stmt;
                stmt.cmd = CMD_TYPE;
                stmt.subcmd = 0;
                stmt.sepol1 = PolicyObject::from_str(type_name);
                stmt.sepol2 = PolicyObject::from_str(attr);
                statements.push_back(stmt);
            }
        }
        return true;
    }

    // typeattribute type attr1 attr2 ...
    if (cmd_str == "typeattribute") {
        std::vector<std::string> types;
        std::vector<std::string> attrs;
        p = parse_seobj(p, types);
        p = parse_seobj(p, attrs);

        for (const auto& t : types) {
            for (const auto& attr : attrs) {
                AtomicStatement stmt;
                stmt.cmd = CMD_TYPE_ATTR;
                stmt.subcmd = 0;
                stmt.sepol1 = PolicyObject::from_str(t);
                stmt.sepol2 = PolicyObject::from_str(attr);
                statements.push_back(stmt);
            }
        }
        return true;
    }

    // attribute attr_name
    if (cmd_str == "attribute") {
        std::string attr_name;
        p = skip_space(p);
        p = parse_word(p, attr_name);

        AtomicStatement stmt;
        stmt.cmd = CMD_ATTR;
        stmt.subcmd = 0;
        stmt.sepol1 = PolicyObject::from_str(attr_name);
        statements.push_back(stmt);
        return true;
    }

    // type_transition source target:class default_type [object_name]
    if (cmd_str == "type_transition") {
        std::string source;
        std::string target;
        std::string tclass;
        std::string default_type;
        std::string object_name;

        p = skip_space(p);
        p = parse_word(p, source);
        p = skip_space(p);
        p = parse_word(p, target);

        // Handle target:class format
        const size_t colon = target.find(':');
        if (colon != std::string::npos) {
            tclass = target.substr(colon + 1);
            target = target.substr(0, colon);
        } else {
            p = skip_space(p);
            if (*p == ':') {
                p++;
                p = parse_word(p, tclass);
            } else {
                p = parse_word(p, tclass);
            }
        }

        p = skip_space(p);
        p = parse_word(p, default_type);

        p = skip_space(p);
        if (*p) {
            // Optional object_name (may be quoted)
            if (*p == '"') {
                p++;
                while (*p && *p != '"') {
                    object_name += *p++;
                }
                if (*p == '"')
                    p++;
            } else {
                p = parse_word(p, object_name);
            }
        }

        AtomicStatement stmt;
        stmt.cmd = CMD_TYPE_TRANSITION;
        stmt.subcmd = 0;
        stmt.sepol1 = PolicyObject::from_str(source);
        stmt.sepol2 = PolicyObject::from_str(target);
        stmt.sepol3 = PolicyObject::from_str(tclass);
        stmt.sepol4 = PolicyObject::from_str(default_type);
        if (!object_name.empty()) {
            stmt.sepol5 = PolicyObject::from_str(object_name);
        }
        statements.push_back(stmt);
        return true;
    }

    // type_change/type_member source target:class default_type
    if (cmd_str == "type_change" || cmd_str == "type_member") {
        const uint32_t subcmd =
            (cmd_str == "type_change") ? SUBCMD_TYPE_CHANGE : SUBCMD_TYPE_MEMBER;

        std::string source;
        std::string target;
        std::string tclass;
        std::string default_type;

        p = skip_space(p);
        p = parse_word(p, source);
        p = skip_space(p);
        p = parse_word(p, target);

        const size_t colon = target.find(':');
        if (colon != std::string::npos) {
            tclass = target.substr(colon + 1);
            target = target.substr(0, colon);
        } else {
            p = skip_space(p);
            if (*p == ':') {
                p++;
                p = parse_word(p, tclass);
            } else {
                p = parse_word(p, tclass);
            }
        }

        p = skip_space(p);
        p = parse_word(p, default_type);

        AtomicStatement stmt;
        stmt.cmd = CMD_TYPE_CHANGE;
        stmt.subcmd = subcmd;
        stmt.sepol1 = PolicyObject::from_str(source);
        stmt.sepol2 = PolicyObject::from_str(target);
        stmt.sepol3 = PolicyObject::from_str(tclass);
        stmt.sepol4 = PolicyObject::from_str(default_type);
        statements.push_back(stmt);
        return true;
    }

    // genfscon fs_name partial_path fs_context
    if (cmd_str == "genfscon") {
        std::string fs_name;
        std::string partial_path;
        std::string fs_context;

        p = skip_space(p);
        p = parse_word(p, fs_name);
        p = skip_space(p);

        // partial_path might be quoted or not
        if (*p == '"') {
            p++;
            while (*p && *p != '"') {
                partial_path += *p++;
            }
            if (*p == '"')
                p++;
        } else {
            p = parse_word(p, partial_path);
        }

        p = skip_space(p);
        p = parse_word(p, fs_context);

        AtomicStatement stmt;
        stmt.cmd = CMD_GENFSCON;
        stmt.subcmd = 0;
        stmt.sepol1 = PolicyObject::from_str(fs_name);
        stmt.sepol2 = PolicyObject::from_str(partial_path);
        stmt.sepol3 = PolicyObject::from_str(fs_context);
        statements.push_back(stmt);
        return true;
    }

    LOGW("Unknown sepolicy command: %s", cmd_str.c_str());
    return false;
}

int expected_argc(uint32_t cmd) {
    switch (cmd) {
    case CMD_NORMAL_PERM:
        return 4;
    case CMD_XPERM:
        return 5;
    case CMD_TYPE_STATE:
        return 1;
    case CMD_TYPE:
    case CMD_TYPE_ATTR:
        return 2;
    case CMD_ATTR:
        return 1;
    case CMD_TYPE_TRANSITION:
        return 5;
    case CMD_TYPE_CHANGE:
        return 4;
    case CMD_GENFSCON:
        return 3;
    default:
        return -1;
    }
}

void append_u32(std::vector<uint8_t>& payload, uint32_t value) {
    const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
    payload.insert(payload.end(), bytes, bytes + sizeof(value));
}

bool append_policy_object(std::vector<uint8_t>& payload, const PolicyObject& object) {
    if (!object.valid()) {
        LOGW("Invalid or oversized sepolicy object");
        return false;
    }
    const char* value = object.c_ptr();
    const uint32_t len = value ? static_cast<uint32_t>(strlen(value)) : 0;

    append_u32(payload, len);
    if (len > 0) {
        payload.insert(payload.end(), value, value + len);
    }
    payload.push_back('\0');
    return true;
}

bool serialize_statement(std::vector<uint8_t>& payload, const AtomicStatement& stmt) {
    const int argc = expected_argc(stmt.cmd);
    if (argc < 0) {
        LOGW("Unknown sepolicy cmd: %u", stmt.cmd);
        return false;
    }

    append_u32(payload, stmt.cmd);
    append_u32(payload, stmt.subcmd);

    auto args = stmt.args();
    for (int i = 0; i < argc; i++) {
        if (!append_policy_object(payload, *args[static_cast<size_t>(i)])) {
            return false;
        }
    }
    return true;
}

bool serialize_statements(const std::vector<AtomicStatement>& statements,
                          std::vector<uint8_t>& payload) {
    payload.clear();
    for (const auto& stmt : statements) {
        if (!serialize_statement(payload, stmt)) {
            return false;
        }
    }
    return true;
}

bool parse_policy_text(std::string_view policy, std::vector<AtomicStatement>* statements) {
    bool success = true;
    for_each_line(policy, [&](std::string_view line) {
        const size_t comment = line.find('#');
        if (comment != std::string_view::npos)
            line = line.substr(0, comment);
        for_each_field(line, ';', [&](std::string_view raw_rule) {
            const std::string_view trimmed_view = trim_view(raw_rule);
            if (trimmed_view.empty())
                return;

            const std::string trimmed(trimmed_view);
            std::vector<AtomicStatement> parsed;
            if (!parse_rule(trimmed, parsed) || parsed.empty() ||
                !std::all_of(parsed.begin(), parsed.end(),
                             [](const AtomicStatement& statement) { return statement.valid(); })) {
                LOGW("Failed to parse rule: %s", trimmed.c_str());
                success = false;
                return;
            }
            if (statements != nullptr)
                statements->insert(statements->end(), parsed.begin(), parsed.end());
        });
    });
    return success;
}

}  // namespace

int sepolicy_live_patch(const std::string& policy) {
    std::vector<AtomicStatement> statements;
    if (!parse_policy_text(policy, &statements))
        return 1;

    if (statements.empty()) {
        return 0;
    }

    std::vector<uint8_t> payload;
    if (!serialize_statements(statements, payload)) {
        return 1;
    }

    const int applied = set_sepolicy(payload.data(), payload.size());
    if (applied < 0) {
        LOGW("Failed to apply sepolicy batch");
        return 1;
    }
    if (static_cast<size_t>(applied) < statements.size()) {
        LOGW("sepolicy batch partially applied: %d/%zu", applied, statements.size());
        return 1;
    }

    return 0;
}

int sepolicy_apply_file(const std::string& file) {
    auto content = read_file(file);
    if (!content) {
        LOGE("Failed to read file: %s", file.c_str());
        return 1;
    }

    return sepolicy_live_patch(*content);
}

int sepolicy_check_rule(const std::string& policy_or_file) {
    struct stat st{};
    if (stat(policy_or_file.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
        auto content = read_file(policy_or_file);
        if (!content) {
            printf("Failed to read file: %s\n", policy_or_file.c_str());
            return 1;
        }
        if (!parse_policy_text(*content, nullptr)) {
            printf("Invalid sepolicy rules\n");
            return 1;
        }
        printf("All sepolicy rules are valid\n");
        return 0;
    }

    if (trim_view(policy_or_file).empty()) {
        printf("Invalid: empty rule\n");
        return 1;
    }
    if (parse_policy_text(policy_or_file, nullptr)) {
        printf("Valid sepolicy rule\n");
        return 0;
    }
    printf("Invalid sepolicy rule\n");
    return 1;
}

}  // namespace ksud
