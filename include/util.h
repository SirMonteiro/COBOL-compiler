#ifndef UTIL_H
#define UTIL_H

#include <string>
#include <vector>

bool ci_eq(const std::string &a, const std::string &b);
std::string to_upper(const std::string &s);
std::string unquote_cobol(const std::string &raw);

/* Search for a copybook or subprogram across search_dirs, current_file's dir,
 * "codigo_fonte_cobol", optional "exemplos", and ".", trying each extension. */
std::string find_cobol_file(const std::string &name,
                            const std::string &current_file,
                            const std::vector<std::string> &search_dirs,
                            const std::vector<std::string> &exts,
                            bool include_exemplos = false);

#endif /* UTIL_H */
