#include "util.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>

std::string to_upper(std::string s) {
    for (char &c : s) c = (char)std::toupper((unsigned char)c);
    return s;
}

std::string unquote_cobol(const std::string &raw) {
    if (raw.empty()) return "";
    size_t start = 0, end = raw.size();
    char q = 0;
    if (raw.size() >= 2 && (raw.front() == '\'' || raw.front() == '"') && raw.back() == raw.front()) {
        q = raw.front();
        start = 1;
        end = raw.size() - 1;
    }
    std::string out;
    out.reserve(end - start);
    for (size_t i = start; i < end; i++) {
        if (q && raw[i] == q && i + 1 < end && raw[i + 1] == q) {
            out.push_back(q);
            i++;
        } else {
            out.push_back(raw[i]);
        }
    }
    return out;
}

static bool file_exists(const std::string &path) {
    FILE *f = std::fopen(path.c_str(), "r");
    if (!f) return false;
    std::fclose(f);
    return true;
}

std::string find_cobol_file(const std::string &name,
                            const std::string &current_file,
                            const std::vector<std::string> &search_dirs,
                            const std::vector<std::string> &exts) {
    std::string clean = unquote_cobol(name);
    std::string lower = clean;
    for (char &c : lower) c = (char)std::tolower((unsigned char)c);

    std::vector<std::string> dirs = search_dirs;
    if (const char *env = std::getenv("COBCPY")) {
        std::string cur;
        for (const char *p = env;; p++) {
            if (*p == ':' || *p == '\0') {
                if (!cur.empty()) { dirs.push_back(cur); cur.clear(); }
                if (*p == '\0') break;
            } else {
                cur.push_back(*p);
            }
        }
    }
    size_t slash = current_file.rfind('/');
    if (slash != std::string::npos && slash > 0) {
        dirs.push_back(current_file.substr(0, slash));
    }
    dirs.push_back(".");

    for (const auto &d : dirs) {
        for (const auto &ext : exts) {
            std::string cand = d + "/" + clean + ext;
            if (file_exists(cand)) return cand;
            if (lower != clean) {
                std::string lcand = d + "/" + lower + ext;
                if (file_exists(lcand)) return lcand;
            }
        }
    }
    return "";
}
