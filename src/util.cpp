#include "util.h"

#include <cctype>
#include <cstdio>

bool ci_eq(const std::string &a, const std::string &b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++) {
        if (std::toupper((unsigned char)a[i]) != std::toupper((unsigned char)b[i])) {
            return false;
        }
    }
    return true;
}

std::string to_upper(const std::string &s) {
    std::string out = s;
    for (size_t i = 0; i < out.size(); i++) {
        out[i] = (char)std::toupper((unsigned char)out[i]);
    }
    return out;
}

std::string unquote_cobol(const std::string &raw) {
    if (raw.empty()) return "";
    size_t start = 0, end = raw.size();
    char q = 0;
    if (raw.size() >= 2 && (raw[0] == '\'' || raw[0] == '"') && raw[raw.size() - 1] == raw[0]) {
        q = raw[0];
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
                            const std::vector<std::string> &exts,
                            bool include_exemplos) {
    std::string clean = unquote_cobol(name);
    std::string lower_clean = clean;
    for (size_t i = 0; i < lower_clean.size(); i++) {
        lower_clean[i] = (char)std::tolower((unsigned char)lower_clean[i]);
    }

    std::vector<std::string> dirs = search_dirs;
    std::string src_dir = ".";
    size_t slash = current_file.rfind('/');
    if (slash != std::string::npos && slash > 0) {
        src_dir = current_file.substr(0, slash);
    }
    dirs.push_back(src_dir);
    dirs.push_back("codigo_fonte_cobol");
    if (include_exemplos) dirs.push_back("exemplos");
    dirs.push_back(".");

    for (size_t d = 0; d < dirs.size(); d++) {
        for (size_t e = 0; e < exts.size(); e++) {
            std::string cand = dirs[d] + "/" + clean + exts[e];
            if (file_exists(cand)) return cand;
            if (lower_clean != clean) {
                std::string lcand = dirs[d] + "/" + lower_clean + exts[e];
                if (file_exists(lcand)) return lcand;
            }
        }
    }
    return "";
}
