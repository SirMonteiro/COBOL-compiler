#include "lexer.h"
#include "util.h"

#include <cctype>
#include <cstdio>
#include <cstring>

static const size_t SLICE_W = 65;
static const int MAX_COPY_DEPTH = 16;

static FILE *lexer_out = nullptr;
static TokenList *active_tokens = nullptr;
static int lexer_errors = 0;

struct ContinuationState {
    bool has_prev = false;
    COBOL_LINE prev = {};

    bool in_string = false;
    char quote = '\0';
    std::string text;
    int col = 0;
    int seq = 0;
};

static ContinuationState cont_state;

int lexer_error_count(void) { return lexer_errors; }
void lexer_reset_errors(void) { lexer_errors = 0; }
void lexer_set_output_file(FILE *out) { lexer_out = out; }
void lexer_set_token_list(TokenList *list) { active_tokens = list; }

const char *token_type_to_string(TOKENTYPE type) {
    switch (type) {
        case TOKEN_KEYWORD:              return "TOKEN_KEYWORD";
        case TOKEN_IDENTIFIER:           return "TOKEN_IDENTIFIER";
        case TOKEN_LITERAL_NUMERIC:      return "TOKEN_LITERAL_NUMERIC";
        case TOKEN_LITERAL_ALPHANUMERIC: return "TOKEN_LITERAL_ALPHANUMERIC";
        case TOKEN_PERIOD:               return "TOKEN_PERIOD";
        case TOKEN_COMMA:                return "TOKEN_COMMA";
        case TOKEN_SEMICOLON:            return "TOKEN_SEMICOLON";
        case TOKEN_LPAREN:               return "TOKEN_LPAREN";
        case TOKEN_RPAREN:               return "TOKEN_RPAREN";
        case TOKEN_COLON:                return "TOKEN_COLON";
        case TOKEN_PICTURE:              return "TOKEN_PICTURE";
        case TOKEN_PLUS:                 return "TOKEN_PLUS";
        case TOKEN_MINUS:                return "TOKEN_MINUS";
        case TOKEN_STAR:                 return "TOKEN_STAR";
        case TOKEN_SLASH:                return "TOKEN_SLASH";
        case TOKEN_POWER:                return "TOKEN_POWER";
        case TOKEN_EQ:                   return "TOKEN_EQ";
        case TOKEN_NE:                   return "TOKEN_NE";
        case TOKEN_LT:                   return "TOKEN_LT";
        case TOKEN_LE:                   return "TOKEN_LE";
        case TOKEN_GT:                   return "TOKEN_GT";
        case TOKEN_GE:                   return "TOKEN_GE";
        case TOKEN_UNKNOWN:              return "TOKEN_UNKNOWN";
        case TOKEN_EOF:                  return "TOKEN_EOF";
        default:                         return "TOKEN_INVALID";
    }
}

const char *area_to_string(COBOL_AREA area) {
    switch (area) {
        case AREA_A: return "AREA A";
        case AREA_B: return "AREA B";
        default:     return "UNK   ";
    }
}

static void print_token(FILE *out, const Token &tok) {
    std::fprintf(out, "[SEQ %06d | COL %02d (%s)] %-26s : %s\n",
                 tok.seq_num, tok.column, area_to_string(tok.area),
                 token_type_to_string(tok.type), tok.lexeme.c_str());
}

void emit_token(TOKENTYPE type, const char *lexeme, int start_col) {
    COBOL_AREA area = (start_col >= 8 && start_col <= 11) ? AREA_A :
                      (start_col >= 12 ? AREA_B : AREA_UNKNOWN);

    Token tok;
    tok.type = type;
    tok.lexeme = (type == TOKEN_KEYWORD || type == TOKEN_IDENTIFIER)
                     ? to_upper(lexeme)
                     : std::string(lexeme);
    tok.seq_num = current_seq_num;
    tok.column = start_col;
    tok.area = area;

    if (active_tokens) active_tokens->push_back(tok);
    if (lexer_out) print_token(lexer_out, tok);
}

void emit_lexical_error(const char *msg, const char *lexeme, int start_col) {
    lexer_errors++;
    if (lexer_out) std::fflush(lexer_out);
    std::fprintf(stderr, "[E] Lexical Error (Seq %06d, Col %d): %s -> \"%s\"\n",
                 current_seq_num, start_col, msg, lexeme);
    std::fflush(stderr);
    emit_token(TOKEN_UNKNOWN, lexeme, start_col);
}

static size_t find_closing_quote(const char *s, size_t from, size_t n, char q) {
    for (size_t j = from; j < n; j++) {
        if (s[j] == q) {
            if (j + 1 < n && s[j + 1] == q) {
                j++;
                continue;
            }
            return j;
        }
    }
    return n;
}

static void scan_slice_n(const char *s, size_t n, int seq, FILE *out) {
    if (n > SLICE_W) n = SLICE_W;
    std::string tmp(s, n);
    lexer_scan_cobol_line(tmp.c_str(), seq, out);
}

static void string_pad_to(size_t target_len) {
    if (cont_state.text.size() < target_len) {
        cont_state.text.append(target_len - cont_state.text.size(), ' ');
    }
}

static void flush_open_string(FILE *out) {
    if (!cont_state.in_string) return;
    current_seq_num = cont_state.seq;
    lexer_set_output_file(out);
    while (cont_state.text.size() > 1 && cont_state.text.back() == ' ') {
        cont_state.text.pop_back();
    }
    emit_lexical_error("Unterminated alphanumeric literal", cont_state.text.c_str(), cont_state.col);
    cont_state.in_string = false;
}

static void scan_with_string_check(const char *s, size_t n, int seq, FILE *out) {
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '*' && i + 1 < n && s[i + 1] == '>') break;
        if (s[i] != '"' && s[i] != '\'') continue;

        size_t close = find_closing_quote(s, i + 1, n, s[i]);
        if (close < n) {
            i = close;
            continue;
        }

        scan_slice_n(s, i, seq, out);
        cont_state.in_string = true;
        cont_state.quote = s[i];
        cont_state.col = 8 + (int)i;
        cont_state.seq = seq;
        cont_state.text.assign(s + i, n - i);
        string_pad_to(SLICE_W - i);
        return;
    }
    scan_slice_n(s, n, seq, out);
}

static void handle_string_continuation(const char *s, size_t n, int seq, FILE *out) {
    size_t i = 0;
    while (i < n && s[i] == ' ') i++;
    if (i == n) return;

    if (s[i] != cont_state.quote) {
        flush_open_string(out);
        current_seq_num = seq;
        emit_lexical_error("Continuation line must start with the literal's quote character", "-", 7);
        scan_with_string_check(s, n, seq, out);
        return;
    }

    size_t start = i + 1;
    size_t close = find_closing_quote(s, start, n, cont_state.quote);
    if (close >= n) {
        cont_state.text.append(s + start, n - start);
        string_pad_to(cont_state.text.size() + (SLICE_W - n));
        return;
    }

    cont_state.text.append(s + start, close - start + 1);
    current_seq_num = cont_state.seq;
    lexer_set_output_file(out);
    emit_token(TOKEN_LITERAL_ALPHANUMERIC, cont_state.text.c_str(), cont_state.col);
    cont_state.in_string = false;

    std::string rest(close + 1, ' ');
    rest.append(s + close + 1, n - close - 1);
    scan_with_string_check(rest.c_str(), rest.size(), seq, out);
}

static bool is_word_char(char c) {
    return std::isalnum((unsigned char)c) || c == '-' || c == '_';
}

static void flush_prev_line(FILE *out) {
    if (!cont_state.has_prev) return;
    COBOL_LINE p = cont_state.prev;
    cont_state.has_prev = false;
    scan_with_string_check(p.code_slice, std::strlen(p.code_slice), p.seq_num, out);
}

void lexer_begin_file(void) {
    cont_state = ContinuationState();
    lexer_reset();
}

void lexer_process_line(const COBOL_LINE *line, FILE *out) {
    const char *s = line->code_slice;
    size_t n = std::strlen(s);

    if (line->status == LINE_STATUS_CONTINUATION) {
        if (cont_state.has_prev) {
            COBOL_LINE prev = cont_state.prev;
            size_t plen = std::strlen(prev.code_slice);

            size_t lead = 0;
            while (lead < n && s[lead] == ' ') lead++;

            if (plen == SLICE_W && is_word_char(prev.code_slice[plen - 1]) &&
                lead < n && is_word_char(s[lead])) {
                size_t pstart = plen;
                while (pstart > 0 && is_word_char(prev.code_slice[pstart - 1])) pstart--;

                size_t send = lead;
                while (send < n && is_word_char(s[send])) send++;

                cont_state.has_prev = false;
                scan_with_string_check(prev.code_slice, pstart, prev.seq_num, out);

                if (!cont_state.in_string) {
                    std::string joined(prev.code_slice + pstart, plen - pstart);
                    joined.append(s + lead, send - lead);

                    lexer_scan_cobol_line_at(joined.c_str(), prev.seq_num, 8 + (int)pstart, out);
                    cont_state.prev = *line;
                    std::memset(cont_state.prev.code_slice, ' ', send);
                    std::memcpy(cont_state.prev.code_slice + send, s + send, n - send + 1);
                    cont_state.has_prev = true;
                    return;
                }
                handle_string_continuation(s, n, line->seq_num, out);
                return;
            }
            flush_prev_line(out);
        }

        if (cont_state.in_string) {
            handle_string_continuation(s, n, line->seq_num, out);
            return;
        }

        cont_state.prev = *line;
        cont_state.has_prev = true;
        return;
    }

    flush_prev_line(out);
    flush_open_string(out);
    cont_state.prev = *line;
    cont_state.has_prev = true;
}

void lexer_end_file(FILE *out) {
    flush_prev_line(out);
    flush_open_string(out);
}

static bool lex_cobol_file_rec(const std::string &input_path, const OPTS &opts, FILE *token_out,
                               TokenList *out_tokens, const std::vector<std::string> &copy_dirs,
                               bool expand_copy, int depth) {
    FILE *cobolSrc = std::fopen(input_path.c_str(), "r");
    if (!cobolSrc) {
        std::fprintf(stderr, "[S] ERROR: Failed to open input file \"%s\".\n", input_path.c_str());
        return false;
    }

    TokenList local_tokens;
    TokenList *prev_list = active_tokens;
    FILE *prev_out = lexer_out;

    lexer_set_token_list(&local_tokens);
    lexer_set_output_file(expand_copy ? nullptr : token_out);
    lexer_begin_file();

    bool ok_all = true;
    char raw_buffer[256];
    int line_no = 0;

    while (std::fgets(raw_buffer, sizeof(raw_buffer), cobolSrc) != nullptr) {
        line_no++;
        size_t n = std::strlen(raw_buffer);
        if (n > 0 && raw_buffer[n - 1] != '\n' && !std::feof(cobolSrc)) {
            int c;
            while ((c = std::fgetc(cobolSrc)) != '\n' && c != EOF) {}
        }

        COBOL_LINE parsed_line;
        if (!read_cobol_line(&opts, raw_buffer, &parsed_line)) {
            if (parsed_line.seq_num == 0) parsed_line.seq_num = line_no;
            if (token_out) std::fflush(token_out);
            std::fprintf(stderr,
                         "[S] Indicator Error (%s, Seq %06d): Invalid indicator '%c' in Column 7.\n",
                         input_path.c_str(), parsed_line.seq_num, parsed_line.indicator);
            ok_all = false;
            continue;
        }

        if (parsed_line.seq_num == 0) parsed_line.seq_num = line_no;

        if (parsed_line.status == LINE_STATUS_CODE || parsed_line.status == LINE_STATUS_CONTINUATION) {
            lexer_process_line(&parsed_line, expand_copy ? nullptr : token_out);
        } else if (parsed_line.status == LINE_STATUS_COMMENT && opts.display_comments && token_out) {
            std::fprintf(token_out, "[I] Comment (Seq %06d): %s\n",
                         parsed_line.seq_num, parsed_line.comment);
        }
    }

    lexer_end_file(expand_copy ? nullptr : token_out);
    std::fclose(cobolSrc);

    lexer_set_token_list(prev_list);
    lexer_set_output_file(prev_out);

    static const std::vector<std::string> copy_exts = {
        ".cpy", ".CPY", ".cbl", ".CBL", ".cob", ".COB", ""
    };

    for (size_t i = 0; i < local_tokens.size(); i++) {
        const Token &t = local_tokens[i];
        if (expand_copy && depth < MAX_COPY_DEPTH &&
            t.type == TOKEN_KEYWORD && t.lexeme == "COPY" &&
            i + 2 < local_tokens.size() &&
            (local_tokens[i + 1].type == TOKEN_IDENTIFIER ||
             local_tokens[i + 1].type == TOKEN_LITERAL_ALPHANUMERIC) &&
            local_tokens[i + 2].type == TOKEN_PERIOD) {
            std::string cpy_path = find_cobol_file(
                local_tokens[i + 1].lexeme, input_path, copy_dirs, copy_exts);
            if (!cpy_path.empty()) {
                if (!lex_cobol_file_rec(cpy_path, opts, token_out, out_tokens,
                                        copy_dirs, expand_copy, depth + 1)) {
                    ok_all = false;
                }
                i += 2;
                continue;
            }
            std::fprintf(stderr,
                         "[W] Copybook Warning (%s, Seq %06d): Copybook \"%s\" not found; keeping COPY node.\n",
                         input_path.c_str(), t.seq_num, local_tokens[i + 1].lexeme.c_str());
        }

        if (out_tokens) out_tokens->push_back(t);
        if (expand_copy && token_out) print_token(token_out, t);
    }

    return ok_all;
}

bool lex_cobol_file(const std::string &input_path, const OPTS &opts, FILE *token_out,
                    TokenList *out_tokens, const std::vector<std::string> &copy_dirs,
                    bool expand_copy) {
    return lex_cobol_file_rec(input_path, opts, token_out, out_tokens,
                              copy_dirs, expand_copy, 0);
}
