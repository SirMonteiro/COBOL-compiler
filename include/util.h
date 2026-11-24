#ifndef UTIL_H
#define UTIL_H

#include <string>
#include <vector>

std::string to_upper(std::string s);
std::string unquote_cobol(const std::string &raw);

/* Search for a copybook or subprogram across search_dirs (-I), COBCPY env,
 * current_file's directory, and ".", trying each extension in order. */
std::string find_cobol_file(const std::string &name,
                            const std::string &current_file,
                            const std::vector<std::string> &search_dirs,
                            const std::vector<std::string> &exts);

#endif /* UTIL_H */
