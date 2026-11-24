%define api.prefix {cob_}
%define parse.error verbose

%{
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "lexer.h"
#include "ast.h"
#include "parser.h"

static const TokenList *g_tokens = nullptr;
static size_t           g_pos = 0;
static const char      *g_filename = nullptr;
static int              g_errors = 0;
static ASTNode         *g_root = nullptr;
static int              g_in_ident_div = 0;
static int              g_pending_comment_text = 0;
static std::string      g_comment_buf;
static Token            g_last_tok;

static int cob_lex(void);
static void cob_error(const char *msg);

static ASTNode *bin_node(ASTKind k, const char *op, ASTNode *a, ASTNode *b) {
    ASTNode *n = ast_new(k, op, a ? a->seq_num : g_last_tok.seq_num, a ? a->col : g_last_tok.column);
    ast_add(n, a);
    ast_add(n, b);
    return n;
}
%}

%union {
    ASTNode *node;
    char    *str;
    int      ival;
}

/* Value-carrying tokens */
%token <str> T_IDENTIFIER T_NUM_LIT T_STR_LIT T_PICTURE T_COMMENT_TEXT
%token <str> T_ID_COMMENT_HDR T_FIGURATIVE

/* Punctuation & Operators */
%token T_PERIOD T_COMMA T_SEMICOLON T_LPAREN T_RPAREN T_COLON
%token T_PLUS T_MINUS T_STAR T_SLASH T_POWER
%token T_EQ T_NE T_LT T_LE T_GT T_GE

/* Division / Section / Header Keywords */
%token KW_IDENTIFICATION KW_ENVIRONMENT KW_DATA KW_PROCEDURE KW_DIVISION KW_SECTION
%token KW_PROGRAM_ID KW_CONFIGURATION KW_SOURCE_COMPUTER KW_OBJECT_COMPUTER
%token KW_SPECIAL_NAMES KW_DECIMAL_POINT KW_COMMA
%token KW_INPUT_OUTPUT KW_FILE_CONTROL KW_IO_CONTROL
%token KW_FILE KW_WORKING_STORAGE KW_LOCAL_STORAGE KW_LINKAGE KW_SCREEN

/* Data / File Declaration Keywords */
%token KW_SELECT KW_ASSIGN KW_OPTIONAL KW_ORGANIZATION KW_LINE KW_SEQUENTIAL
%token KW_INDEXED KW_RELATIVE KW_ACCESS KW_MODE KW_DYNAMIC KW_RANDOM
%token KW_RECORD KW_RECORDS KW_KEY KW_ALTERNATE KW_DUPLICATES KW_STATUS
%token KW_FD KW_SD KW_BLOCK KW_CONTAINS KW_LABEL KW_STANDARD KW_OMITTED
%token KW_PIC KW_VALUE KW_REDEFINES KW_OCCURS KW_TIMES KW_DEPENDING
%token KW_ASCENDING KW_DESCENDING KW_USAGE KW_COMP KW_SIGN KW_LEADING
%token KW_TRAILING KW_SEPARATE KW_CHARACTER KW_CHARACTERS KW_JUSTIFIED
%token KW_RIGHT KW_BLANK KW_SYNC KW_FILLER KW_GLOBAL KW_EXTERNAL KW_COPY

/* Verb Keywords */
%token KW_DISPLAY KW_ACCEPT KW_MOVE KW_ADD KW_SUBTRACT KW_MULTIPLY KW_DIVIDE KW_COMPUTE
%token KW_IF KW_THEN KW_ELSE KW_END_IF
%token KW_EVALUATE KW_WHEN KW_OTHER KW_ALSO KW_ANY KW_END_EVALUATE
%token KW_PERFORM KW_UNTIL KW_VARYING KW_AFTER KW_BEFORE KW_TEST KW_THRU KW_END_PERFORM
%token KW_OPEN KW_CLOSE KW_READ KW_WRITE KW_REWRITE KW_DELETE KW_START
%token KW_END_READ KW_END_WRITE KW_END_REWRITE KW_END_DELETE KW_END_START
%token KW_GO KW_ALTER KW_PROCEED KW_CALL KW_CANCEL KW_END_CALL KW_STOP KW_RUN KW_GOBACK
%token KW_COLUMN
%token KW_EXIT KW_CONTINUE KW_NEXT KW_SENTENCE
%token KW_INITIALIZE KW_INSPECT KW_STRING KW_UNSTRING KW_SET KW_SEARCH
%token KW_SORT KW_MERGE KW_RELEASE KW_RETURN
%token KW_END_ADD KW_END_SUBTRACT KW_END_MULTIPLY KW_END_DIVIDE KW_END_COMPUTE
%token KW_END_STRING KW_END_UNSTRING KW_END_SEARCH KW_END_RETURN KW_END_ACCEPT KW_END_DISPLAY

/* Misc / Clause / Operator Keywords */
%token KW_TO KW_FROM KW_BY KW_INTO KW_GIVING KW_REMAINDER KW_OF KW_IN
%token KW_INPUT KW_OUTPUT KW_IO KW_EXTEND
%token KW_AT KW_END KW_NOT KW_INVALID KW_ON KW_SIZE KW_ERROR KW_OVERFLOW KW_EXCEPTION
%token KW_AND KW_OR KW_EQUAL KW_GREATER KW_LESS KW_THAN KW_IS KW_ARE
%token KW_TRUE KW_FALSE
%token KW_DELIMITED KW_DELIMITER KW_TALLYING KW_REPLACING KW_CONVERTING
%token KW_FOR KW_ALL KW_FIRST KW_INITIAL KW_COUNT KW_POINTER
%token KW_WITH KW_POSITION KW_ADVANCING KW_NO KW_UPON KW_PAGE KW_LINES KW_EOP
%token KW_USING KW_RETURNING KW_REFERENCE KW_CONTENT
%token KW_ROUNDED KW_CORRESPONDING
%token KW_NUMERIC KW_ALPHABETIC KW_ALPHANUMERIC KW_POSITIVE KW_NEGATIVE
%token KW_FUNCTION KW_PROGRAM KW_PARAGRAPH KW_LENGTH KW_UP KW_DOWN KW_CYCLE

/* Operator precedence */
%nonassoc LOWER_THAN_HANDLER
%left KW_OR
%left KW_AND
%right KW_NOT
%left T_PLUS T_MINUS
%left T_STAR T_SLASH
%right T_POWER
%right UMINUS

/* Dangling-else / optional-terminator resolution */
%nonassoc KW_ELSE KW_END_IF
%nonassoc KW_AT KW_INVALID KW_ON
%nonassoc KW_END_READ KW_END_WRITE KW_END_REWRITE KW_END_DELETE KW_END_START
%nonassoc KW_END_ADD KW_END_SUBTRACT KW_END_MULTIPLY KW_END_DIVIDE KW_END_COMPUTE
%nonassoc KW_END_STRING KW_END_UNSTRING KW_END_CALL KW_END_SEARCH KW_END_RETURN
%nonassoc KW_END_ACCEPT KW_END_DISPLAY

%type <node> program identification_div id_entry_list id_entry
%type <node> opt_environment_div env_section_list env_section config_entries config_entry
%type <node> special_name_clauses special_name_clause opt_file_control file_control_entries select_entry select_clauses select_clause
%type <node> opt_data_div data_section_list data_section fd_entries fd_entry fd_clauses fd_clause
%type <node> data_item_list data_item data_clauses data_clause copy_stmt
%type <node> procedure_div proc_using proc_body proc_element sentence stmt_list stmt
%type <node> stmt_display stmt_accept stmt_move stmt_add stmt_subtract stmt_multiply stmt_divide stmt_compute
%type <node> stmt_if stmt_evaluate stmt_perform stmt_open stmt_close stmt_read stmt_write
%type <node> stmt_rewrite stmt_delete stmt_start stmt_goto stmt_call stmt_cancel
%type <node> stmt_stop stmt_goback stmt_exit stmt_continue stmt_next_sentence
%type <node> stmt_initialize stmt_inspect stmt_string stmt_unstring stmt_set stmt_search
%type <node> stmt_sort stmt_release stmt_return
%type <node> display_items display_item rvalue rvalue_list data_ref data_ref_list qualified_name
%type <node> arith_expr condition simple_cond abbreviated_rhs
%type <node> add_target add_target_list open_groups open_group
%type <node> eval_subjects eval_subject when_list when_clause when_objs when_obj
%type <node> perform_ctrl perform_after_list inspect_clauses inspect_clause
%type <node> string_parts string_part unstring_targets unstring_target
%type <node> search_when_list search_when call_args call_arg
%type <node> opt_else opt_size_handlers opt_not_size_error opt_read_handlers opt_not_at_end opt_io_handlers opt_not_invalid_key opt_overflow_handlers opt_not_overflow opt_call_handlers
%type <node> opt_indexed_by opt_giving opt_remainder opt_into opt_from opt_call_using opt_thru opt_search_at_end opt_search_varying
%type <node> opt_init_replacing init_repl_list init_repl_item inspect_tally_list inspect_tally_item inspect_for_list inspect_for_item inspect_repl_list inspect_repl_item inspect_ba_list
%type <node> string_delim opt_pointer opt_unstring_delim opt_tallying
%type <str>  init_category org_type before_or_after opt_accept_fmt
%type <ival> opt_no_advancing opt_rounded opt_line
%type <node> value_literal value_item value_list subscript_list
%type <str>  ident_word proc_name func_name relop relop_core class_type into_or_by

%%

program
    : identification_div opt_environment_div opt_data_div procedure_div opt_end_program
      {
          ASTNode *prog = ast_new(AST_PROGRAM, $1 ? $1->text.c_str() : nullptr, 1, 8);
          ast_add(prog, $1);
          ast_add(prog, $2);
          ast_add(prog, $3);
          ast_add(prog, $4);
          g_root = prog;
          $$ = prog;
      }
    ;

opt_end_program
    : /* empty */
    | KW_END KW_PROGRAM ident_word opt_period { free($3); }
    ;

opt_period
    : /* empty */
    | T_PERIOD
    ;

/* =========================================================================
 * 3.1 IDENTIFICATION DIVISION
 * ========================================================================= */

identification_div
    : KW_IDENTIFICATION KW_DIVISION T_PERIOD
      KW_PROGRAM_ID opt_period ident_word opt_period
      id_entry_list
      {
          ASTNode *id = ast_new(AST_IDENTIFICATION, $6, g_last_tok.seq_num, 8);
          free($6);
          ast_adopt_children(id, $8);
          $$ = id;
      }
    ;

id_entry_list
    : /* empty */                 { $$ = ast_new(AST_LIST, NULL, 0, 0); }
    | id_entry_list id_entry      { $$ = ast_add($1, $2); }
    ;

id_entry
    : T_ID_COMMENT_HDR T_COMMENT_TEXT
      {
          $$ = ast_new_ex(AST_ID_ENTRY, $1, $2, 0, g_last_tok.seq_num, 8);
          free($1); free($2);
      }
    | T_ID_COMMENT_HDR
      {
          $$ = ast_new(AST_ID_ENTRY, $1, g_last_tok.seq_num, 8);
          free($1);
      }
    | copy_stmt { $$ = $1; }
    ;

/* =========================================================================
 * 3.2 ENVIRONMENT DIVISION
 * ========================================================================= */

opt_environment_div
    : /* empty */ { $$ = NULL; }
    | KW_ENVIRONMENT KW_DIVISION T_PERIOD env_section_list
      {
          ASTNode *env = ast_new(AST_ENVIRONMENT, NULL, g_last_tok.seq_num, 8);
          ast_adopt_children(env, $4);
          $$ = env;
      }
    ;

env_section_list
    : /* empty */                      { $$ = ast_new(AST_LIST, NULL, 0, 0); }
    | env_section_list env_section     { $$ = ast_add($1, $2); }
    ;

env_section
    : KW_CONFIGURATION KW_SECTION T_PERIOD config_entries
      {
          ASTNode *c = ast_new(AST_CONFIGURATION, NULL, g_last_tok.seq_num, 8);
          ast_adopt_children(c, $4);
          $$ = c;
      }
    | KW_INPUT_OUTPUT KW_SECTION T_PERIOD opt_file_control opt_io_control
      {
          $$ = $4;
      }
    | copy_stmt { $$ = $1; }
    ;

config_entries
    : /* empty */                  { $$ = ast_new(AST_LIST, NULL, 0, 0); }
    | config_entries config_entry  { $$ = ast_add($1, $2); }
    ;

config_entry
    : KW_SOURCE_COMPUTER T_PERIOD ident_word T_PERIOD
      { $$ = ast_new_ex(AST_CLAUSE, "SOURCE-COMPUTER", $3, 0, g_last_tok.seq_num, 8); free($3); }
    | KW_SOURCE_COMPUTER T_PERIOD
      { $$ = ast_new(AST_CLAUSE, "SOURCE-COMPUTER", g_last_tok.seq_num, 8); }
    | KW_OBJECT_COMPUTER T_PERIOD ident_word T_PERIOD
      { $$ = ast_new_ex(AST_CLAUSE, "OBJECT-COMPUTER", $3, 0, g_last_tok.seq_num, 8); free($3); }
    | KW_OBJECT_COMPUTER T_PERIOD
      { $$ = ast_new(AST_CLAUSE, "OBJECT-COMPUTER", g_last_tok.seq_num, 8); }
    | KW_SPECIAL_NAMES T_PERIOD special_name_clauses
      {
          ASTNode *sn = ast_new(AST_SPECIAL_NAMES, NULL, g_last_tok.seq_num, 8);
          ast_adopt_children(sn, $3);
          $$ = sn;
      }
    ;

special_name_clauses
    : /* empty */                               { $$ = ast_new(AST_LIST, NULL, 0, 0); }
    | special_name_clauses special_name_clause  { $$ = ast_add($1, $2); }
    ;

special_name_clause
    : KW_DECIMAL_POINT opt_is KW_COMMA opt_period
      { $$ = ast_new(AST_CLAUSE, "DECIMAL-POINT-IS-COMMA", g_last_tok.seq_num, g_last_tok.column); }
    | ident_word KW_STATUS opt_is ident_word opt_period
      { $$ = ast_new_ex(AST_CLAUSE, "CRT-STATUS", $4, 0, g_last_tok.seq_num, g_last_tok.column); free($1); free($4); }
    | ident_word opt_is ident_word opt_period
      { $$ = ast_new_ex(AST_CLAUSE, $1, $3, 0, g_last_tok.seq_num, g_last_tok.column); free($1); free($3); }
    ;

opt_file_control
    : /* empty */ { $$ = NULL; }
    | KW_FILE_CONTROL T_PERIOD file_control_entries
      {
          ASTNode *fc = ast_new(AST_FILE_CONTROL, NULL, g_last_tok.seq_num, 8);
          ast_adopt_children(fc, $3);
          $$ = fc;
      }
    ;

opt_io_control
    : /* empty */
    | KW_IO_CONTROL T_PERIOD
    ;

file_control_entries
    : /* empty */                        { $$ = ast_new(AST_LIST, NULL, 0, 0); }
    | file_control_entries select_entry  { $$ = ast_add($1, $2); }
    | file_control_entries copy_stmt     { $$ = ast_add($1, $2); }
    ;

select_entry
    : KW_SELECT opt_optional ident_word select_clauses T_PERIOD
      {
          ASTNode *sel = ast_new(AST_SELECT, $3, g_last_tok.seq_num, g_last_tok.column);
          free($3);
          ast_adopt_children(sel, $4);
          $$ = sel;
      }
    ;

opt_optional
    : /* empty */
    | KW_OPTIONAL
    ;

select_clauses
    : /* empty */                  { $$ = ast_new(AST_LIST, NULL, 0, 0); }
    | select_clauses select_clause { $$ = ast_add($1, $2); }
    ;

select_clause
    : KW_ASSIGN opt_to rvalue
      {
          ASTNode *c = ast_new(AST_CLAUSE, "ASSIGN", g_last_tok.seq_num, g_last_tok.column);
          ast_add(c, $3);
          $$ = c;
      }
    | KW_ORGANIZATION opt_is opt_line org_type
      {
          $$ = ast_new_ex(AST_CLAUSE, "ORGANIZATION", $4, $3, g_last_tok.seq_num, g_last_tok.column);
          free($4);
      }
    | KW_ACCESS opt_mode opt_is access_type
      { $$ = ast_new(AST_CLAUSE, "ACCESS", g_last_tok.seq_num, g_last_tok.column); }
    | KW_RECORD KW_KEY opt_is data_ref
      {
          ASTNode *c = ast_new(AST_CLAUSE, "RECORD-KEY", g_last_tok.seq_num, g_last_tok.column);
          ast_add(c, $4);
          $$ = c;
      }
    | KW_ALTERNATE KW_RECORD KW_KEY opt_is data_ref opt_with_duplicates
      {
          ASTNode *c = ast_new(AST_CLAUSE, "ALTERNATE-KEY", g_last_tok.seq_num, g_last_tok.column);
          ast_add(c, $5);
          $$ = c;
      }
    | opt_file KW_STATUS opt_is data_ref
      {
          ASTNode *c = ast_new(AST_CLAUSE, "FILE-STATUS", g_last_tok.seq_num, g_last_tok.column);
          ast_add(c, $4);
          $$ = c;
      }
    | KW_RELATIVE KW_KEY opt_is data_ref
      {
          ASTNode *c = ast_new(AST_CLAUSE, "RELATIVE-KEY", g_last_tok.seq_num, g_last_tok.column);
          ast_add(c, $4);
          $$ = c;
      }
    ;

opt_to: /* empty */ | KW_TO ;
opt_is: /* empty */ | KW_IS | KW_ARE ;
opt_line: /* empty */ { $$ = 0; } | KW_LINE { $$ = 1; } ;
opt_mode: /* empty */ | KW_MODE ;
opt_file: /* empty */ | KW_FILE ;
opt_with_duplicates: /* empty */ | opt_with KW_DUPLICATES ;
opt_with: /* empty */ | KW_WITH ;

org_type
    : KW_SEQUENTIAL { $$ = strdup("SEQUENTIAL"); }
    | KW_INDEXED    { $$ = strdup("INDEXED"); }
    | KW_RELATIVE   { $$ = strdup("RELATIVE"); }
    ;
access_type: KW_SEQUENTIAL | KW_DYNAMIC | KW_RANDOM ;

/* =========================================================================
 * 3.3 DATA DIVISION
 * ========================================================================= */

opt_data_div
    : /* empty */ { $$ = NULL; }
    | KW_DATA KW_DIVISION T_PERIOD data_section_list
      {
          ASTNode *dd = ast_new(AST_DATA_DIVISION, NULL, g_last_tok.seq_num, 8);
          ast_adopt_children(dd, $4);
          $$ = dd;
      }
    ;

data_section_list
    : /* empty */                     { $$ = ast_new(AST_LIST, NULL, 0, 0); }
    | data_section_list data_section  { $$ = ast_add($1, $2); }
    ;

data_section
    : KW_FILE KW_SECTION T_PERIOD fd_entries
      {
          ASTNode *s = ast_new(AST_DATA_SECTION, "FILE", g_last_tok.seq_num, 8);
          ast_adopt_children(s, $4);
          $$ = s;
      }
    | KW_WORKING_STORAGE KW_SECTION T_PERIOD data_item_list
      {
          ASTNode *s = ast_new(AST_DATA_SECTION, "WORKING-STORAGE", g_last_tok.seq_num, 8);
          ast_adopt_children(s, $4);
          $$ = s;
      }
    | KW_LOCAL_STORAGE KW_SECTION T_PERIOD data_item_list
      {
          ASTNode *s = ast_new(AST_DATA_SECTION, "LOCAL-STORAGE", g_last_tok.seq_num, 8);
          ast_adopt_children(s, $4);
          $$ = s;
      }
    | KW_LINKAGE KW_SECTION T_PERIOD data_item_list
      {
          ASTNode *s = ast_new(AST_DATA_SECTION, "LINKAGE", g_last_tok.seq_num, 8);
          ast_adopt_children(s, $4);
          $$ = s;
      }
    | KW_SCREEN KW_SECTION T_PERIOD data_item_list
      {
          ASTNode *s = ast_new(AST_DATA_SECTION, "SCREEN", g_last_tok.seq_num, 8);
          ast_adopt_children(s, $4);
          $$ = s;
      }
    ;

fd_entries
    : /* empty */            { $$ = ast_new(AST_LIST, NULL, 0, 0); }
    | fd_entries fd_entry    { $$ = ast_add($1, $2); }
    | fd_entries copy_stmt   { $$ = ast_add($1, $2); }
    ;

fd_entry
    : fd_or_sd ident_word fd_clauses T_PERIOD data_item_list
      {
          ASTNode *fd = ast_new(AST_FD, $2, g_last_tok.seq_num, 8);
          free($2);
          ast_adopt_children(fd, $3);
          ast_adopt_children(fd, $5);
          $$ = fd;
      }
    ;

fd_or_sd: KW_FD | KW_SD ;

fd_clauses
    : /* empty */            { $$ = ast_new(AST_LIST, NULL, 0, 0); }
    | fd_clauses fd_clause   { $$ = ast_add($1, $2); }
    ;

fd_clause
    : KW_BLOCK KW_CONTAINS T_NUM_LIT opt_to_num opt_rec_char
      { $$ = ast_new_ex(AST_CLAUSE, "BLOCK-CONTAINS", $3, atoi($3), g_last_tok.seq_num, g_last_tok.column); free($3); }
    | KW_RECORD KW_CONTAINS T_NUM_LIT opt_to_num opt_rec_char
      { $$ = ast_new_ex(AST_CLAUSE, "RECORD-CONTAINS", $3, atoi($3), g_last_tok.seq_num, g_last_tok.column); free($3); }
    | KW_LABEL record_kw opt_is label_kind
      { $$ = ast_new(AST_CLAUSE, "LABEL-RECORD", g_last_tok.seq_num, g_last_tok.column); }
    | KW_DATA record_kw opt_is data_ref_list
      { $$ = ast_new(AST_CLAUSE, "DATA-RECORD", g_last_tok.seq_num, g_last_tok.column); ast_adopt_children($$, $4); }
    | KW_RECORDING_MODE_COMPAT
      { $$ = ast_new(AST_CLAUSE, "RECORDING-MODE", g_last_tok.seq_num, g_last_tok.column); }
    ;

KW_RECORDING_MODE_COMPAT
    : KW_EXTERNAL
    | KW_GLOBAL
    ;

opt_to_num: /* empty */ | KW_TO T_NUM_LIT { free($2); } ;
opt_rec_char: /* empty */ | KW_RECORDS | KW_RECORD | KW_CHARACTERS | KW_CHARACTER ;
record_kw: KW_RECORD | KW_RECORDS ;
label_kind: KW_STANDARD | KW_OMITTED ;

data_item_list
    : /* empty */                { $$ = ast_new(AST_LIST, NULL, 0, 0); }
    | data_item_list data_item   { $$ = ast_add($1, $2); }
    | data_item_list copy_stmt   { $$ = ast_add($1, $2); }
    ;

data_item
    : T_NUM_LIT ident_word data_clauses T_PERIOD
      {
          ASTNode *it = ast_new_ex(AST_DATA_ITEM, $2, NULL, atoi($1), g_last_tok.seq_num, g_last_tok.column);
          free($1); free($2);
          ast_adopt_children(it, $3);
          $$ = it;
      }
    | T_NUM_LIT KW_FILLER data_clauses T_PERIOD
      {
          ASTNode *it = ast_new_ex(AST_DATA_ITEM, "FILLER", NULL, atoi($1), g_last_tok.seq_num, g_last_tok.column);
          free($1);
          ast_adopt_children(it, $3);
          $$ = it;
      }
    | T_NUM_LIT data_clauses T_PERIOD
      {
          ASTNode *it = ast_new_ex(AST_DATA_ITEM, "FILLER", NULL, atoi($1), g_last_tok.seq_num, g_last_tok.column);
          free($1);
          ast_adopt_children(it, $2);
          $$ = it;
      }
    ;

data_clauses
    : /* empty */              { $$ = ast_new(AST_LIST, NULL, 0, 0); }
    | data_clauses data_clause { $$ = ast_add($1, $2); }
    ;

data_clause
    : KW_PIC opt_is T_PICTURE
      {
          $$ = ast_new_ex(AST_CLAUSE, "PIC", $3, 0, g_last_tok.seq_num, g_last_tok.column);
          free($3);
      }
    | KW_VALUE opt_is value_list
      {
          ASTNode *v = ast_new(AST_CLAUSE, "VALUE", g_last_tok.seq_num, g_last_tok.column);
          ast_adopt_children(v, $3);
          $$ = v;
      }
    | KW_REDEFINES ident_word
      {
          $$ = ast_new_ex(AST_CLAUSE, "REDEFINES", $2, 0, g_last_tok.seq_num, g_last_tok.column);
          free($2);
      }
    | KW_OCCURS T_NUM_LIT opt_to_num opt_times opt_depending opt_key_orders opt_indexed_by
      {
          ASTNode *oc = ast_new_ex(AST_CLAUSE, "OCCURS", $2, atoi($2), g_last_tok.seq_num, g_last_tok.column);
          free($2);
          ast_add(oc, $7);
          $$ = oc;
      }
    | opt_usage_kw KW_COMP
      { $$ = ast_new(AST_CLAUSE, "USAGE-COMP", g_last_tok.seq_num, g_last_tok.column); }
    | KW_USAGE opt_is KW_DISPLAY
      { $$ = ast_new(AST_CLAUSE, "USAGE-DISPLAY", g_last_tok.seq_num, g_last_tok.column); }
    | KW_USAGE opt_is KW_POINTER
      { $$ = ast_new(AST_CLAUSE, "USAGE-POINTER", g_last_tok.seq_num, g_last_tok.column); }
    | KW_SIGN opt_is leading_or_trailing opt_separate
      { $$ = ast_new(AST_CLAUSE, "SIGN", g_last_tok.seq_num, g_last_tok.column); }
    | KW_JUSTIFIED opt_right
      { $$ = ast_new(AST_CLAUSE, "JUSTIFIED", g_last_tok.seq_num, g_last_tok.column); }
    | KW_BLANK opt_when_zero
      { $$ = ast_new(AST_CLAUSE, "BLANK", g_last_tok.seq_num, g_last_tok.column); }
    | KW_SYNC opt_right
      { $$ = ast_new(AST_CLAUSE, "SYNC", g_last_tok.seq_num, g_last_tok.column); }
    | KW_GLOBAL
      { $$ = ast_new(AST_CLAUSE, "GLOBAL", g_last_tok.seq_num, g_last_tok.column); }
    | KW_EXTERNAL
      { $$ = ast_new(AST_CLAUSE, "EXTERNAL", g_last_tok.seq_num, g_last_tok.column); }
    | KW_LINE opt_is T_NUM_LIT
      { $$ = ast_new_ex(AST_CLAUSE, "LINE", $3, atoi($3), g_last_tok.seq_num, g_last_tok.column); free($3); }
    | KW_COLUMN opt_is T_NUM_LIT
      { $$ = ast_new_ex(AST_CLAUSE, "COLUMN", $3, atoi($3), g_last_tok.seq_num, g_last_tok.column); free($3); }
    | KW_USING data_ref
      { ASTNode *c = ast_new(AST_CLAUSE, "USING", g_last_tok.seq_num, g_last_tok.column); ast_add(c, $2); $$ = c; }
    | KW_FROM data_ref
      { ASTNode *c = ast_new(AST_CLAUSE, "FROM", g_last_tok.seq_num, g_last_tok.column); ast_add(c, $2); $$ = c; }
    | KW_TO data_ref
      { ASTNode *c = ast_new(AST_CLAUSE, "TO", g_last_tok.seq_num, g_last_tok.column); ast_add(c, $2); $$ = c; }
    ;

opt_usage_kw: /* empty */ | KW_USAGE opt_is ;
opt_times: /* empty */ | KW_TIMES ;
opt_depending: /* empty */ | KW_DEPENDING opt_on data_ref { ast_free($3); } ;
opt_on: /* empty */ | KW_ON ;
opt_key_orders: /* empty */ | opt_key_orders asc_or_desc opt_key opt_is data_ref_list { ast_free($5); } ;
asc_or_desc: KW_ASCENDING | KW_DESCENDING ;
opt_key: /* empty */ | KW_KEY ;
opt_indexed_by
    : /* empty */ { $$ = NULL; }
    | KW_INDEXED opt_by data_ref_list
      {
          ASTNode *ix = ast_new(AST_CLAUSE, "INDEXED-BY", g_last_tok.seq_num, g_last_tok.column);
          ast_adopt_children(ix, $3);
          $$ = ix;
      }
    ;
opt_by: /* empty */ | KW_BY ;
leading_or_trailing: KW_LEADING | KW_TRAILING ;
opt_separate: /* empty */ | KW_SEPARATE opt_character ;
opt_character: /* empty */ | KW_CHARACTER ;
opt_right: /* empty */ | KW_RIGHT ;
opt_when_zero: /* empty */ | KW_WHEN T_FIGURATIVE { free($2); } | KW_SCREEN ;

value_list
    : value_item                       { $$ = ast_add(ast_new(AST_LIST, NULL, 0, 0), $1); }
    | value_list opt_comma value_item  { $$ = ast_add($1, $3); }
    ;

value_literal
    : T_NUM_LIT    { $$ = ast_new(AST_LITERAL_NUM, $1, g_last_tok.seq_num, g_last_tok.column); free($1); }
    | T_STR_LIT    { $$ = ast_new(AST_LITERAL_STR, $1, g_last_tok.seq_num, g_last_tok.column); free($1); }
    | T_FIGURATIVE { $$ = ast_new(AST_FIGURATIVE,  $1, g_last_tok.seq_num, g_last_tok.column); free($1); }
    | KW_TRUE      { $$ = ast_new(AST_FIGURATIVE,  "TRUE", g_last_tok.seq_num, g_last_tok.column); }
    | KW_FALSE     { $$ = ast_new(AST_FIGURATIVE,  "FALSE", g_last_tok.seq_num, g_last_tok.column); }
    ;

value_item
    : value_literal                         { $$ = $1; }
    | value_literal KW_THRU value_literal   { $$ = bin_node(AST_BINOP, "THRU", $1, $3); }
    | KW_ALL T_STR_LIT                      { $$ = ast_new_ex(AST_LITERAL_STR, $2, "ALL", 0, g_last_tok.seq_num, g_last_tok.column); free($2); }
    | KW_ALL T_FIGURATIVE                   { $$ = ast_new_ex(AST_FIGURATIVE, $2, "ALL", 0, g_last_tok.seq_num, g_last_tok.column); free($2); }
    ;

copy_stmt
    : KW_COPY ident_word T_PERIOD
      { $$ = ast_new(AST_COPY, $2, g_last_tok.seq_num, g_last_tok.column); free($2); }
    | KW_COPY T_STR_LIT T_PERIOD
      { $$ = ast_new(AST_COPY, $2, g_last_tok.seq_num, g_last_tok.column); free($2); }
    ;

/* =========================================================================
 * 3.4 PROCEDURE DIVISION
 * ========================================================================= */

procedure_div
    : KW_PROCEDURE KW_DIVISION proc_using opt_returning T_PERIOD proc_body
      {
          ASTNode *pd = ast_new(AST_PROCEDURE_DIVISION, NULL, g_last_tok.seq_num, 8);
          ast_add(pd, $3);
          ast_adopt_children(pd, $6);
          $$ = pd;
      }
    ;

proc_using
    : /* empty */ { $$ = NULL; }
    | KW_USING data_ref_list
      {
          ASTNode *u = ast_new(AST_CLAUSE, "USING", g_last_tok.seq_num, g_last_tok.column);
          ast_adopt_children(u, $2);
          $$ = u;
      }
    ;

opt_returning
    : /* empty */
    | KW_RETURNING data_ref { ast_free($2); }
    ;

proc_body
    : /* empty */              { $$ = ast_new(AST_LIST, NULL, 0, 0); }
    | proc_body proc_element   { $$ = ast_add($1, $2); }
    ;

proc_element
    : proc_name KW_SECTION T_PERIOD
      { $$ = ast_new(AST_PROC_SECTION, $1, g_last_tok.seq_num, 8); free($1); }
    | proc_name T_PERIOD
      { $$ = ast_new(AST_PARAGRAPH, $1, g_last_tok.seq_num, 8); free($1); }
    | sentence
      { $$ = $1; }
    | copy_stmt
      { $$ = $1; }
    ;

sentence
    : stmt_list T_PERIOD
      {
          ASTNode *s = ast_new(AST_SENTENCE, NULL, g_last_tok.seq_num, g_last_tok.column);
          ast_adopt_children(s, $1);
          $$ = s;
      }
    | T_PERIOD
      { $$ = NULL; }
    ;

stmt_list
    : stmt             { $$ = ast_add(ast_new(AST_LIST, NULL, 0, 0), $1); }
    | stmt_list stmt   { $$ = ast_add($1, $2); }
    ;

stmt
    : stmt_display
    | stmt_accept
    | stmt_move
    | stmt_add
    | stmt_subtract
    | stmt_multiply
    | stmt_divide
    | stmt_compute
    | stmt_if
    | stmt_evaluate
    | stmt_perform
    | stmt_open
    | stmt_close
    | stmt_read
    | stmt_write
    | stmt_rewrite
    | stmt_delete
    | stmt_start
    | stmt_goto
    | stmt_call
    | stmt_cancel
    | stmt_stop
    | stmt_goback
    | stmt_exit
    | stmt_continue
    | stmt_next_sentence
    | stmt_initialize
    | stmt_inspect
    | stmt_string
    | stmt_unstring
    | stmt_set
    | stmt_search
    | stmt_sort
    | stmt_release
    | stmt_return
    ;

/* --- DISPLAY --- */
stmt_display
    : KW_DISPLAY display_items opt_upon opt_no_advancing opt_end_display
      {
          ASTNode *d = ast_new_ex(AST_STMT_DISPLAY, NULL, NULL, $4, g_last_tok.seq_num, g_last_tok.column);
          ast_adopt_children(d, $2);
          $$ = d;
      }
    ;

display_items
    : display_item                         { $$ = ast_add(ast_new(AST_LIST, NULL, 0, 0), $1); }
    | display_items opt_comma display_item { $$ = ast_add($1, $3); }
    ;

display_item
    : rvalue { $$ = $1; }
    | KW_AT display_pos_clauses
      { $$ = ast_new(AST_CLAUSE, "AT-POS", g_last_tok.seq_num, g_last_tok.column); }
    ;

display_pos_clauses
    : display_pos_clause
    | display_pos_clauses display_pos_clause
    ;

display_pos_clause
    : KW_LINE rvalue   { ast_free($2); }
    | KW_COLUMN rvalue { ast_free($2); }
    | T_NUM_LIT        { free($1); }
    ;

opt_upon
    : /* empty */
    | KW_UPON ident_word { free($2); }
    ;

opt_no_advancing
    : /* empty */               { $$ = 0; }
    | opt_with KW_NO KW_ADVANCING { $$ = 1; }
    ;

opt_end_display
    : /* empty */ %prec LOWER_THAN_HANDLER
    | KW_END_DISPLAY
    ;

/* --- ACCEPT --- */
stmt_accept
    : KW_ACCEPT data_ref opt_end_accept
      {
          ASTNode *a = ast_new(AST_STMT_ACCEPT, NULL, g_last_tok.seq_num, g_last_tok.column);
          ast_add(a, $2);
          $$ = a;
      }
    | KW_ACCEPT data_ref KW_FROM ident_word opt_accept_fmt opt_end_accept
      {
          ASTNode *a = ast_new_ex(AST_STMT_ACCEPT, $4, $5, 0, g_last_tok.seq_num, g_last_tok.column);
          free($4); free($5);
          ast_add(a, $2);
          $$ = a;
      }
    ;

opt_accept_fmt
    : /* empty */ { $$ = NULL; }
    | ident_word  { $$ = $1; }
    ;

opt_end_accept
    : /* empty */ %prec LOWER_THAN_HANDLER
    | KW_END_ACCEPT
    ;

/* --- MOVE --- */
stmt_move
    : KW_MOVE opt_corr rvalue KW_TO data_ref_list
      {
          ASTNode *m = ast_new(AST_STMT_MOVE, NULL, g_last_tok.seq_num, g_last_tok.column);
          ast_add(m, $3);
          ast_adopt_children(m, $5);
          $$ = m;
      }
    ;

opt_corr
    : /* empty */
    | KW_CORRESPONDING
    ;

/* --- ARITHMETIC: ADD / SUBTRACT / MULTIPLY / DIVIDE / COMPUTE --- */
stmt_add
    : KW_ADD rvalue_list KW_TO add_target_list opt_giving opt_size_handlers opt_end_add
      {
          ASTNode *n = ast_new(AST_STMT_ADD, NULL, g_last_tok.seq_num, g_last_tok.column);
          ast_add(n, $2); ast_add(n, $4); ast_add(n, $5); ast_add(n, $6);
          $$ = n;
      }
    | KW_ADD rvalue_list KW_GIVING add_target_list opt_size_handlers opt_end_add
      {
          ASTNode *n = ast_new(AST_STMT_ADD, "GIVING", g_last_tok.seq_num, g_last_tok.column);
          ast_add(n, $2); ast_add(n, $4); ast_add(n, $5);
          $$ = n;
      }
    | KW_ADD KW_CORRESPONDING data_ref KW_TO data_ref opt_rounded opt_size_handlers opt_end_add
      {
          ASTNode *n = ast_new(AST_STMT_ADD, "CORR", g_last_tok.seq_num, g_last_tok.column);
          ast_add(n, $3); ast_add(n, $5); ast_add(n, $7);
          $$ = n;
      }
    ;

stmt_subtract
    : KW_SUBTRACT rvalue_list KW_FROM add_target_list opt_giving opt_size_handlers opt_end_subtract
      {
          ASTNode *n = ast_new(AST_STMT_SUBTRACT, NULL, g_last_tok.seq_num, g_last_tok.column);
          ast_add(n, $2); ast_add(n, $4); ast_add(n, $5); ast_add(n, $6);
          $$ = n;
      }
    | KW_SUBTRACT KW_CORRESPONDING data_ref KW_FROM data_ref opt_rounded opt_size_handlers opt_end_subtract
      {
          ASTNode *n = ast_new(AST_STMT_SUBTRACT, "CORR", g_last_tok.seq_num, g_last_tok.column);
          ast_add(n, $3); ast_add(n, $5); ast_add(n, $7);
          $$ = n;
      }
    ;

stmt_multiply
    : KW_MULTIPLY rvalue KW_BY add_target_list opt_giving opt_size_handlers opt_end_multiply
      {
          ASTNode *n = ast_new(AST_STMT_MULTIPLY, NULL, g_last_tok.seq_num, g_last_tok.column);
          ast_add(n, $2); ast_add(n, $4); ast_add(n, $5); ast_add(n, $6);
          $$ = n;
      }
    ;

stmt_divide
    : KW_DIVIDE rvalue into_or_by add_target_list opt_giving opt_remainder opt_size_handlers opt_end_divide
      {
          ASTNode *n = ast_new(AST_STMT_DIVIDE, $3, g_last_tok.seq_num, g_last_tok.column);
          free($3);
          ast_add(n, $2); ast_add(n, $4); ast_add(n, $5); ast_add(n, $6); ast_add(n, $7);
          $$ = n;
      }
    ;

into_or_by
    : KW_INTO { $$ = strdup("INTO"); }
    | KW_BY   { $$ = strdup("BY"); }
    ;

stmt_compute
    : KW_COMPUTE add_target_list eq_or_equal arith_expr opt_size_handlers opt_end_compute
      {
          ASTNode *n = ast_new(AST_STMT_COMPUTE, NULL, g_last_tok.seq_num, g_last_tok.column);
          ast_add(n, $2); ast_add(n, $4); ast_add(n, $5);
          $$ = n;
      }
    ;

eq_or_equal: T_EQ | KW_EQUAL ;

add_target_list
    : add_target                           { $$ = ast_add(ast_new(AST_LIST, NULL, 0, 0), $1); }
    | add_target_list opt_comma add_target { $$ = ast_add($1, $3); }
    ;

add_target
    : rvalue opt_rounded { if ($1 && $2) $1->ival = 1; $$ = $1; }
    ;

opt_rounded: /* empty */ { $$ = 0; } | KW_ROUNDED { $$ = 1; } ;
opt_giving
    : /* empty */ { $$ = NULL; }
    | KW_GIVING add_target_list
      {
          ASTNode *g = ast_new(AST_CLAUSE, "GIVING", g_last_tok.seq_num, g_last_tok.column);
          ast_adopt_children(g, $2);
          $$ = g;
      }
    ;
opt_remainder
    : /* empty */ { $$ = NULL; }
    | KW_REMAINDER data_ref
      {
          ASTNode *r = ast_new(AST_CLAUSE, "REMAINDER", $2->seq_num, $2->col);
          ast_add(r, $2);
          $$ = r;
      }
    ;

opt_size_handlers
    : /* empty */ %prec LOWER_THAN_HANDLER { $$ = NULL; }
    | opt_on KW_SIZE KW_ERROR stmt_list opt_not_size_error
      {
          ASTNode *h = ast_new(AST_CLAUSE, "ON-SIZE-ERROR", g_last_tok.seq_num, g_last_tok.column);
          ast_adopt_children(h, $4);
          ast_add(h, $5);
          $$ = h;
      }
    | KW_NOT opt_on KW_SIZE KW_ERROR stmt_list
      {
          ASTNode *h = ast_new(AST_CLAUSE, "NOT-ON-SIZE-ERROR", g_last_tok.seq_num, g_last_tok.column);
          ast_adopt_children(h, $5);
          $$ = h;
      }
    ;

opt_not_size_error
    : /* empty */ %prec LOWER_THAN_HANDLER { $$ = NULL; }
    | KW_NOT opt_on KW_SIZE KW_ERROR stmt_list
      {
          ASTNode *h = ast_new(AST_CLAUSE, "NOT-ON-SIZE-ERROR", g_last_tok.seq_num, g_last_tok.column);
          ast_adopt_children(h, $5);
          $$ = h;
      }
    ;

opt_end_add:      /* empty */ %prec LOWER_THAN_HANDLER | KW_END_ADD ;
opt_end_subtract: /* empty */ %prec LOWER_THAN_HANDLER | KW_END_SUBTRACT ;
opt_end_multiply: /* empty */ %prec LOWER_THAN_HANDLER | KW_END_MULTIPLY ;
opt_end_divide:   /* empty */ %prec LOWER_THAN_HANDLER | KW_END_DIVIDE ;
opt_end_compute:  /* empty */ %prec LOWER_THAN_HANDLER | KW_END_COMPUTE ;

/* --- IF --- */
stmt_if
    : KW_IF condition opt_then stmt_list opt_else opt_end_if
      {
          ASTNode *n = ast_new(AST_STMT_IF, NULL, g_last_tok.seq_num, g_last_tok.column);
          ast_add(n, $2);
          ast_add(n, $4);
          ast_add(n, $5);
          $$ = n;
      }
    ;

opt_then: /* empty */ | KW_THEN ;

opt_else
    : /* empty */ %prec LOWER_THAN_HANDLER { $$ = NULL; }
    | KW_ELSE stmt_list
      {
          ASTNode *e = ast_new(AST_CLAUSE, "ELSE", g_last_tok.seq_num, g_last_tok.column);
          ast_adopt_children(e, $2);
          $$ = e;
      }
    ;

opt_end_if
    : /* empty */ %prec LOWER_THAN_HANDLER
    | KW_END_IF
    ;

/* --- EVALUATE --- */
stmt_evaluate
    : KW_EVALUATE eval_subjects when_list opt_end_evaluate
      {
          ASTNode *ev = ast_new(AST_STMT_EVALUATE, NULL, g_last_tok.seq_num, g_last_tok.column);
          ast_add(ev, $2);
          ast_adopt_children(ev, $3);
          $$ = ev;
      }
    ;

eval_subjects
    : eval_subject                        { $$ = ast_add(ast_new(AST_LIST, NULL, 0, 0), $1); }
    | eval_subjects KW_ALSO eval_subject  { $$ = ast_add($1, $3); }
    ;

eval_subject
    : condition { $$ = $1; }
    | KW_TRUE   { $$ = ast_new(AST_FIGURATIVE, "TRUE", g_last_tok.seq_num, g_last_tok.column); }
    | KW_FALSE  { $$ = ast_new(AST_FIGURATIVE, "FALSE", g_last_tok.seq_num, g_last_tok.column); }
    ;

when_list
    : when_clause            { $$ = ast_add(ast_new(AST_LIST, NULL, 0, 0), $1); }
    | when_list when_clause  { $$ = ast_add($1, $2); }
    ;

when_clause
    : KW_WHEN KW_OTHER stmt_list
      {
          ASTNode *w = ast_new(AST_WHEN, "OTHER", g_last_tok.seq_num, g_last_tok.column);
          ast_adopt_children(w, $3);
          $$ = w;
      }
    | KW_WHEN when_objs stmt_list
      {
          ASTNode *w = ast_new(AST_WHEN, NULL, g_last_tok.seq_num, g_last_tok.column);
          ast_add(w, $2);
          ast_adopt_children(w, $3);
          $$ = w;
      }
    ;

when_objs
    : when_obj                    { $$ = ast_add(ast_new(AST_LIST, NULL, 0, 0), $1); }
    | when_objs KW_ALSO when_obj  { $$ = ast_add($1, $3); }
    ;

when_obj
    : KW_ANY
      { $$ = ast_new(AST_FIGURATIVE, "ANY", g_last_tok.seq_num, g_last_tok.column); }
    | KW_TRUE
      { $$ = ast_new(AST_FIGURATIVE, "TRUE", g_last_tok.seq_num, g_last_tok.column); }
    | KW_FALSE
      { $$ = ast_new(AST_FIGURATIVE, "FALSE", g_last_tok.seq_num, g_last_tok.column); }
    | relop arith_expr
      { ASTNode *n = ast_new(AST_COND_REL, $1, g_last_tok.seq_num, g_last_tok.column); free($1); ast_add(n, $2); $$ = n; }
    | opt_is KW_NOT class_type
      { $$ = ast_new_ex(AST_COND_CLASS, $3, "NOT", 0, g_last_tok.seq_num, g_last_tok.column); free($3); }
    | opt_is class_type
      { $$ = ast_new(AST_COND_CLASS, $2, g_last_tok.seq_num, g_last_tok.column); free($2); }
    | arith_expr KW_THRU arith_expr
      { $$ = bin_node(AST_BINOP, "THRU", $1, $3); }
    | condition
      { $$ = $1; }
    ;

opt_end_evaluate
    : /* empty */ %prec LOWER_THAN_HANDLER
    | KW_END_EVALUATE
    ;

/* --- PERFORM --- */
stmt_perform
    : KW_PERFORM proc_name opt_thru perform_ctrl
      {
          ASTNode *p = ast_new(AST_STMT_PERFORM, $2, g_last_tok.seq_num, g_last_tok.column);
          free($2);
          ast_add(p, $3);
          ast_add(p, $4);
          $$ = p;
      }
    | KW_PERFORM proc_name opt_thru
      {
          ASTNode *p = ast_new(AST_STMT_PERFORM, $2, g_last_tok.seq_num, g_last_tok.column);
          free($2);
          ast_add(p, $3);
          $$ = p;
      }
    | KW_PERFORM perform_ctrl stmt_list KW_END_PERFORM
      {
          ASTNode *p = ast_new(AST_STMT_PERFORM, "INLINE", g_last_tok.seq_num, g_last_tok.column);
          ast_add(p, $2);
          ast_adopt_children(p, $3);
          $$ = p;
      }
    | KW_PERFORM stmt_list KW_END_PERFORM
      {
          ASTNode *p = ast_new(AST_STMT_PERFORM, "INLINE", g_last_tok.seq_num, g_last_tok.column);
          ast_adopt_children(p, $2);
          $$ = p;
      }
    ;

opt_thru
    : /* empty */ { $$ = NULL; }
    | KW_THRU proc_name
      {
          $$ = ast_new(AST_CLAUSE, "THRU", g_last_tok.seq_num, g_last_tok.column);
          ast_set_extra($$, $2);
          free($2);
      }
    ;

perform_ctrl
    : rvalue KW_TIMES
      {
          ASTNode *c = ast_new(AST_CLAUSE, "TIMES", g_last_tok.seq_num, g_last_tok.column);
          ast_add(c, $1);
          $$ = c;
      }
    | opt_with_test KW_UNTIL condition
      {
          ASTNode *c = ast_new(AST_CLAUSE, "UNTIL", g_last_tok.seq_num, g_last_tok.column);
          ast_add(c, $3);
          $$ = c;
      }
    | opt_with_test KW_VARYING data_ref KW_FROM rvalue KW_BY rvalue KW_UNTIL condition perform_after_list
      {
          ASTNode *c = ast_new(AST_CLAUSE, "VARYING", g_last_tok.seq_num, g_last_tok.column);
          ast_add(c, $3); ast_add(c, $5); ast_add(c, $7); ast_add(c, $9);
          ast_adopt_children(c, $10);
          $$ = c;
      }
    ;

opt_with_test
    : /* empty */
    | opt_with KW_TEST before_or_after { free($3); }
    ;

before_or_after
    : KW_BEFORE { $$ = strdup("BEFORE"); }
    | KW_AFTER  { $$ = strdup("AFTER"); }
    ;

perform_after_list
    : /* empty */ { $$ = ast_new(AST_LIST, NULL, 0, 0); }
    | perform_after_list KW_AFTER data_ref KW_FROM rvalue KW_BY rvalue KW_UNTIL condition
      {
          ASTNode *a = ast_new(AST_CLAUSE, "AFTER", g_last_tok.seq_num, g_last_tok.column);
          ast_add(a, $3); ast_add(a, $5); ast_add(a, $7); ast_add(a, $9);
          $$ = ast_add($1, a);
      }
    ;

/* --- FILE I/O: OPEN / CLOSE / READ / WRITE / REWRITE / DELETE / START --- */
stmt_open
    : KW_OPEN open_groups
      {
          ASTNode *o = ast_new(AST_STMT_OPEN, NULL, g_last_tok.seq_num, g_last_tok.column);
          ast_adopt_children(o, $2);
          $$ = o;
      }
    ;

open_groups
    : open_group              { $$ = ast_add(ast_new(AST_LIST, NULL, 0, 0), $1); }
    | open_groups open_group  { $$ = ast_add($1, $2); }
    ;

open_group
    : KW_INPUT data_ref_list  { ASTNode *g = ast_new(AST_CLAUSE, "INPUT",  g_last_tok.seq_num, g_last_tok.column); ast_adopt_children(g, $2); $$ = g; }
    | KW_OUTPUT data_ref_list { ASTNode *g = ast_new(AST_CLAUSE, "OUTPUT", g_last_tok.seq_num, g_last_tok.column); ast_adopt_children(g, $2); $$ = g; }
    | KW_IO data_ref_list     { ASTNode *g = ast_new(AST_CLAUSE, "I-O",    g_last_tok.seq_num, g_last_tok.column); ast_adopt_children(g, $2); $$ = g; }
    | KW_EXTEND data_ref_list { ASTNode *g = ast_new(AST_CLAUSE, "EXTEND", g_last_tok.seq_num, g_last_tok.column); ast_adopt_children(g, $2); $$ = g; }
    ;

stmt_close
    : KW_CLOSE data_ref_list
      {
          ASTNode *c = ast_new(AST_STMT_CLOSE, NULL, g_last_tok.seq_num, g_last_tok.column);
          ast_adopt_children(c, $2);
          $$ = c;
      }
    ;

stmt_read
    : KW_READ data_ref opt_next opt_record opt_into opt_read_key opt_read_handlers opt_end_read
      {
          ASTNode *r = ast_new(AST_STMT_READ, $2->text, $2->seq_num, $2->col);
          ast_add(r, $2);
          ast_add(r, $5);
          ast_add(r, $7);
          $$ = r;
      }
    ;

opt_next: /* empty */ | KW_NEXT ;
opt_record: /* empty */ | KW_RECORD ;
opt_into
    : /* empty */ { $$ = NULL; }
    | KW_INTO data_ref
      {
          ASTNode *c = ast_new(AST_CLAUSE, "INTO", $2->seq_num, $2->col);
          ast_add(c, $2);
          $$ = c;
      }
    ;
opt_read_key: /* empty */ | KW_KEY opt_is data_ref { ast_free($3); } ;

opt_read_handlers
    : /* empty */ %prec LOWER_THAN_HANDLER { $$ = NULL; }
    | opt_at KW_END stmt_list opt_not_at_end
      {
          ASTNode *h = ast_new(AST_CLAUSE, "AT-END", g_last_tok.seq_num, g_last_tok.column);
          ast_adopt_children(h, $3);
          ast_add(h, $4);
          $$ = h;
      }
    | KW_NOT opt_at KW_END stmt_list
      {
          ASTNode *h = ast_new(AST_CLAUSE, "NOT-AT-END", g_last_tok.seq_num, g_last_tok.column);
          ast_adopt_children(h, $4);
          $$ = h;
      }
    | KW_INVALID opt_key stmt_list opt_not_invalid_key
      {
          ASTNode *h = ast_new(AST_CLAUSE, "INVALID-KEY", g_last_tok.seq_num, g_last_tok.column);
          ast_adopt_children(h, $3);
          ast_add(h, $4);
          $$ = h;
      }
    | KW_NOT KW_INVALID opt_key stmt_list
      {
          ASTNode *h = ast_new(AST_CLAUSE, "NOT-INVALID-KEY", g_last_tok.seq_num, g_last_tok.column);
          ast_adopt_children(h, $4);
          $$ = h;
      }
    ;

opt_at: /* empty */ | KW_AT ;

opt_not_at_end
    : /* empty */ %prec LOWER_THAN_HANDLER { $$ = NULL; }
    | KW_NOT opt_at KW_END stmt_list
      {
          ASTNode *h = ast_new(AST_CLAUSE, "NOT-AT-END", g_last_tok.seq_num, g_last_tok.column);
          ast_adopt_children(h, $4);
          $$ = h;
      }
    ;

opt_io_handlers
    : /* empty */ %prec LOWER_THAN_HANDLER { $$ = NULL; }
    | KW_INVALID opt_key stmt_list opt_not_invalid_key
      {
          ASTNode *h = ast_new(AST_CLAUSE, "INVALID-KEY", g_last_tok.seq_num, g_last_tok.column);
          ast_adopt_children(h, $3);
          ast_add(h, $4);
          $$ = h;
      }
    | KW_NOT KW_INVALID opt_key stmt_list
      {
          ASTNode *h = ast_new(AST_CLAUSE, "NOT-INVALID-KEY", g_last_tok.seq_num, g_last_tok.column);
          ast_adopt_children(h, $4);
          $$ = h;
      }
    | opt_at KW_EOP stmt_list
      {
          ASTNode *h = ast_new(AST_CLAUSE, "AT-EOP", g_last_tok.seq_num, g_last_tok.column);
          ast_adopt_children(h, $3);
          $$ = h;
      }
    ;

opt_not_invalid_key
    : /* empty */ %prec LOWER_THAN_HANDLER { $$ = NULL; }
    | KW_NOT KW_INVALID opt_key stmt_list
      {
          ASTNode *h = ast_new(AST_CLAUSE, "NOT-INVALID-KEY", g_last_tok.seq_num, g_last_tok.column);
          ast_adopt_children(h, $4);
          $$ = h;
      }
    ;

opt_end_read: /* empty */ %prec LOWER_THAN_HANDLER | KW_END_READ ;

stmt_write
    : KW_WRITE data_ref opt_from opt_write_adv opt_io_handlers opt_end_write
      {
          ASTNode *w = ast_new(AST_STMT_WRITE, $2->text, $2->seq_num, $2->col);
          ast_add(w, $2);
          ast_add(w, $3);
          ast_add(w, $5);
          $$ = w;
      }
    ;

opt_from
    : /* empty */ { $$ = NULL; }
    | KW_FROM rvalue
      {
          ASTNode *c = ast_new(AST_CLAUSE, "FROM", $2->seq_num, $2->col);
          ast_add(c, $2);
          $$ = c;
      }
    ;
opt_write_adv
    : /* empty */
    | before_or_after opt_advancing adv_target { free($1); }
    ;
opt_advancing: /* empty */ | KW_ADVANCING ;
adv_target
    : KW_PAGE
    | rvalue opt_lines { ast_free($1); }
    ;
opt_lines: /* empty */ | KW_LINE | KW_LINES ;
opt_end_write: /* empty */ %prec LOWER_THAN_HANDLER | KW_END_WRITE ;

stmt_rewrite
    : KW_REWRITE data_ref opt_from opt_io_handlers opt_end_rewrite
      {
          ASTNode *w = ast_new(AST_STMT_REWRITE, $2->text, $2->seq_num, $2->col);
          ast_add(w, $2); ast_add(w, $3); ast_add(w, $4);
          $$ = w;
      }
    ;
opt_end_rewrite: /* empty */ %prec LOWER_THAN_HANDLER | KW_END_REWRITE ;

stmt_delete
    : KW_DELETE data_ref opt_record opt_io_handlers opt_end_delete
      {
          ASTNode *d = ast_new(AST_STMT_DELETE, $2->text, $2->seq_num, $2->col);
          ast_add(d, $2); ast_add(d, $4);
          $$ = d;
      }
    ;
opt_end_delete: /* empty */ %prec LOWER_THAN_HANDLER | KW_END_DELETE ;

stmt_start
    : KW_START data_ref opt_start_key opt_io_handlers opt_end_start
      {
          ASTNode *s = ast_new(AST_STMT_START, $2->text, $2->seq_num, $2->col);
          ast_add(s, $2); ast_add(s, $4);
          $$ = s;
      }
    ;

opt_start_key
    : /* empty */
    | KW_KEY opt_is relop data_ref { free($3); ast_free($4); }
    | KW_KEY opt_is data_ref { ast_free($3); }
    ;
opt_end_start: /* empty */ %prec LOWER_THAN_HANDLER | KW_END_START ;

/* --- CONTROL FLOW: GO TO / ALTER / CALL / CANCEL / STOP / GOBACK / EXIT / CONTINUE --- */
stmt_goto
    : KW_GO opt_to proc_name
      { $$ = ast_new(AST_STMT_GOTO, $3, g_last_tok.seq_num, g_last_tok.column); free($3); }
    | KW_ALTER proc_name KW_TO opt_proceed proc_name
      { $$ = ast_new_ex(AST_STMT_GOTO, $2, $5, 0, g_last_tok.seq_num, g_last_tok.column); free($2); free($5); }
    ;

opt_proceed: /* empty */ | KW_PROCEED opt_to ;

stmt_call
    : KW_CALL rvalue opt_call_using opt_returning opt_call_handlers opt_end_call
      {
          ASTNode *c = ast_new(AST_STMT_CALL, NULL, g_last_tok.seq_num, g_last_tok.column);
          ast_add(c, $2);
          ast_add(c, $3);
          ast_add(c, $5);
          $$ = c;
      }
    ;

opt_call_using
    : /* empty */ { $$ = NULL; }
    | KW_USING call_args
      {
          ASTNode *u = ast_new(AST_CLAUSE, "USING", g_last_tok.seq_num, g_last_tok.column);
          ast_adopt_children(u, $2);
          $$ = u;
      }
    ;

call_args
    : call_arg                     { $$ = ast_add(ast_new(AST_LIST, NULL, 0, 0), $1); }
    | call_args opt_comma call_arg { $$ = ast_add($1, $3); }
    ;

call_arg
    : opt_by_mode rvalue { $$ = $2; }
    ;

opt_by_mode
    : /* empty */
    | opt_by KW_REFERENCE
    | opt_by KW_CONTENT
    | opt_by KW_VALUE
    ;

opt_call_handlers
    : /* empty */ %prec LOWER_THAN_HANDLER { $$ = NULL; }
    | opt_on KW_EXCEPTION stmt_list
      { ASTNode *h = ast_new(AST_CLAUSE, "ON-EXCEPTION", g_last_tok.seq_num, g_last_tok.column); ast_adopt_children(h, $3); $$ = h; }
    | opt_on KW_OVERFLOW stmt_list
      { ASTNode *h = ast_new(AST_CLAUSE, "ON-OVERFLOW", g_last_tok.seq_num, g_last_tok.column); ast_adopt_children(h, $3); $$ = h; }
    ;

opt_end_call: /* empty */ %prec LOWER_THAN_HANDLER | KW_END_CALL ;

stmt_cancel
    : KW_CANCEL rvalue_list
      { ASTNode *c = ast_new(AST_STMT_CANCEL, NULL, g_last_tok.seq_num, g_last_tok.column); ast_adopt_children(c, $2); $$ = c; }
    ;

stmt_stop
    : KW_STOP KW_RUN { $$ = ast_new(AST_STMT_STOP_RUN, NULL, g_last_tok.seq_num, g_last_tok.column); }
    | KW_STOP rvalue { ASTNode *s = ast_new(AST_STMT_STOP_RUN, "LITERAL", g_last_tok.seq_num, g_last_tok.column); ast_add(s, $2); $$ = s; }
    ;

stmt_goback
    : KW_GOBACK { $$ = ast_new(AST_STMT_GOBACK, NULL, g_last_tok.seq_num, g_last_tok.column); }
    ;

stmt_exit
    : KW_EXIT opt_exit_kind { $$ = ast_new(AST_STMT_EXIT, NULL, g_last_tok.seq_num, g_last_tok.column); }
    ;

opt_exit_kind
    : /* empty */
    | KW_PROGRAM
    | KW_PARAGRAPH
    | KW_SECTION
    | KW_PERFORM opt_cycle
    ;

opt_cycle: /* empty */ | KW_CYCLE ;

stmt_continue
    : KW_CONTINUE { $$ = ast_new(AST_STMT_CONTINUE, NULL, g_last_tok.seq_num, g_last_tok.column); }
    ;

stmt_next_sentence
    : KW_NEXT KW_SENTENCE { $$ = ast_new(AST_STMT_NEXT_SENTENCE, NULL, g_last_tok.seq_num, g_last_tok.column); }
    ;

/* --- INITIALIZE --- */
stmt_initialize
    : KW_INITIALIZE data_ref_list opt_init_replacing
      {
          ASTNode *n = ast_new(AST_STMT_INITIALIZE, NULL, g_last_tok.seq_num, g_last_tok.column);
          ast_adopt_children(n, $2);
          if ($3) ast_adopt_children(n, $3);
          $$ = n;
      }
    ;

opt_init_replacing
    : /* empty */              { $$ = NULL; }
    | KW_REPLACING init_repl_list { $$ = $2; }
    ;

init_repl_list
    : init_repl_item                 { $$ = ast_add(ast_new(AST_LIST, NULL, 0, 0), $1); }
    | init_repl_list init_repl_item  { $$ = ast_add($1, $2); }
    ;

init_repl_item
    : init_category opt_data KW_BY rvalue
      {
          ASTNode *c = ast_new_ex(AST_CLAUSE, "REPLACING", $1, 0, g_last_tok.seq_num, g_last_tok.column);
          free($1);
          ast_add(c, $4);
          $$ = c;
      }
    ;

init_category
    : KW_ALPHABETIC   { $$ = strdup("ALPHABETIC"); }
    | KW_ALPHANUMERIC { $$ = strdup("ALPHANUMERIC"); }
    | KW_NUMERIC      { $$ = strdup("NUMERIC"); }
    ;
opt_data: /* empty */ | KW_DATA ;

/* --- INSPECT --- */
stmt_inspect
    : KW_INSPECT rvalue inspect_clauses
      {
          ASTNode *n = ast_new(AST_STMT_INSPECT, NULL, g_last_tok.seq_num, g_last_tok.column);
          ast_add(n, $2);
          ast_adopt_children(n, $3);
          $$ = n;
      }
    ;

inspect_clauses
    : inspect_clause                  { $$ = ast_add(ast_new(AST_LIST, NULL, 0, 0), $1); }
    | inspect_clauses inspect_clause  { $$ = ast_add($1, $2); }
    ;

inspect_clause
    : KW_TALLYING inspect_tally_list
      {
          ASTNode *c = ast_new(AST_CLAUSE, "TALLYING", g_last_tok.seq_num, g_last_tok.column);
          ast_adopt_children(c, $2);
          $$ = c;
      }
    | KW_REPLACING inspect_repl_list
      {
          ASTNode *c = ast_new(AST_CLAUSE, "REPLACING", g_last_tok.seq_num, g_last_tok.column);
          ast_adopt_children(c, $2);
          $$ = c;
      }
    | KW_CONVERTING rvalue KW_TO rvalue inspect_ba_list
      {
          ASTNode *c = ast_new(AST_CLAUSE, "CONVERTING", g_last_tok.seq_num, g_last_tok.column);
          ast_add(c, $2); ast_add(c, $4);
          ast_adopt_children(c, $5);
          $$ = c;
      }
    ;

inspect_tally_list
    : inspect_tally_item                    { $$ = ast_add(ast_new(AST_LIST, NULL, 0, 0), $1); }
    | inspect_tally_list inspect_tally_item { $$ = ast_add($1, $2); }
    ;

inspect_tally_item
    : data_ref KW_FOR inspect_for_list
      {
          ASTNode *t = ast_new(AST_CLAUSE, "TALLY-ITEM", $1->seq_num, $1->col);
          ast_add(t, $1);
          ast_adopt_children(t, $3);
          $$ = t;
      }
    ;

inspect_for_list
    : inspect_for_item                  { $$ = ast_add(ast_new(AST_LIST, NULL, 0, 0), $1); }
    | inspect_for_list inspect_for_item { $$ = ast_add($1, $2); }
    ;

inspect_for_item
    : KW_CHARACTERS inspect_ba_list
      {
          ASTNode *f = ast_new(AST_CLAUSE, "FOR-CHARACTERS", g_last_tok.seq_num, g_last_tok.column);
          ast_adopt_children(f, $2);
          $$ = f;
      }
    | KW_ALL rvalue inspect_ba_list
      {
          ASTNode *f = ast_new(AST_CLAUSE, "FOR-ALL", g_last_tok.seq_num, g_last_tok.column);
          ast_add(f, $2); ast_adopt_children(f, $3);
          $$ = f;
      }
    | KW_LEADING rvalue inspect_ba_list
      {
          ASTNode *f = ast_new(AST_CLAUSE, "FOR-LEADING", g_last_tok.seq_num, g_last_tok.column);
          ast_add(f, $2); ast_adopt_children(f, $3);
          $$ = f;
      }
    ;

inspect_repl_list
    : inspect_repl_item                   { $$ = ast_add(ast_new(AST_LIST, NULL, 0, 0), $1); }
    | inspect_repl_list inspect_repl_item { $$ = ast_add($1, $2); }
    ;

inspect_repl_item
    : KW_CHARACTERS KW_BY rvalue inspect_ba_list
      {
          ASTNode *r = ast_new(AST_CLAUSE, "REPL-CHARACTERS", g_last_tok.seq_num, g_last_tok.column);
          ast_add(r, $3); ast_adopt_children(r, $4);
          $$ = r;
      }
    | KW_ALL rvalue KW_BY rvalue inspect_ba_list
      {
          ASTNode *r = ast_new(AST_CLAUSE, "REPL-ALL", g_last_tok.seq_num, g_last_tok.column);
          ast_add(r, $2); ast_add(r, $4); ast_adopt_children(r, $5);
          $$ = r;
      }
    | KW_LEADING rvalue KW_BY rvalue inspect_ba_list
      {
          ASTNode *r = ast_new(AST_CLAUSE, "REPL-LEADING", g_last_tok.seq_num, g_last_tok.column);
          ast_add(r, $2); ast_add(r, $4); ast_adopt_children(r, $5);
          $$ = r;
      }
    | KW_FIRST rvalue KW_BY rvalue inspect_ba_list
      {
          ASTNode *r = ast_new(AST_CLAUSE, "REPL-FIRST", g_last_tok.seq_num, g_last_tok.column);
          ast_add(r, $2); ast_add(r, $4); ast_adopt_children(r, $5);
          $$ = r;
      }
    ;

inspect_ba_list
    : /* empty */ { $$ = ast_new(AST_LIST, NULL, 0, 0); }
    | inspect_ba_list before_or_after opt_initial rvalue
      {
          ASTNode *ba = ast_new(AST_CLAUSE, $2, g_last_tok.seq_num, g_last_tok.column);
          free($2);
          ast_add(ba, $4);
          $$ = ast_add($1, ba);
      }
    ;

opt_initial: /* empty */ | KW_INITIAL ;

/* --- STRING / UNSTRING --- */
stmt_string
    : KW_STRING string_parts KW_INTO data_ref opt_pointer opt_overflow_handlers opt_end_string
      {
          ASTNode *s = ast_new(AST_STMT_STRING, NULL, g_last_tok.seq_num, g_last_tok.column);
          ast_add(s, $2);
          ast_add(s, $4);
          ast_add(s, $5);
          ast_add(s, $6);
          $$ = s;
      }
    ;

string_parts
    : string_part              { $$ = ast_add(ast_new(AST_LIST, NULL, 0, 0), $1); }
    | string_parts string_part { $$ = ast_add($1, $2); }
    ;

string_part
    : rvalue_list KW_DELIMITED opt_by string_delim
      {
          ASTNode *p = ast_new(AST_CLAUSE, "STRING-PART", g_last_tok.seq_num, g_last_tok.column);
          ast_add(p, $1);
          ast_add(p, $4);
          $$ = p;
      }
    ;

string_delim
    : KW_SIZE { $$ = ast_new(AST_CLAUSE, "DELIM-SIZE", g_last_tok.seq_num, g_last_tok.column); }
    | rvalue  { $$ = $1; }
    ;

opt_pointer
    : /* empty */ { $$ = NULL; }
    | opt_with KW_POINTER data_ref
      { ASTNode *p = ast_new(AST_CLAUSE, "POINTER", $3->seq_num, $3->col); ast_add(p, $3); $$ = p; }
    ;

opt_overflow_handlers
    : /* empty */ %prec LOWER_THAN_HANDLER { $$ = NULL; }
    | opt_on KW_OVERFLOW stmt_list opt_not_overflow
      { ASTNode *h = ast_new(AST_CLAUSE, "ON-OVERFLOW", g_last_tok.seq_num, g_last_tok.column); ast_adopt_children(h, $3); ast_add(h, $4); $$ = h; }
    | KW_NOT opt_on KW_OVERFLOW stmt_list
      { ASTNode *h = ast_new(AST_CLAUSE, "NOT-ON-OVERFLOW", g_last_tok.seq_num, g_last_tok.column); ast_adopt_children(h, $4); $$ = h; }
    ;

opt_not_overflow
    : /* empty */ %prec LOWER_THAN_HANDLER { $$ = NULL; }
    | KW_NOT opt_on KW_OVERFLOW stmt_list
      {
          ASTNode *h = ast_new(AST_CLAUSE, "NOT-ON-OVERFLOW", g_last_tok.seq_num, g_last_tok.column);
          ast_adopt_children(h, $4);
          $$ = h;
      }
    ;

opt_end_string: /* empty */ %prec LOWER_THAN_HANDLER | KW_END_STRING ;

stmt_unstring
    : KW_UNSTRING data_ref opt_unstring_delim KW_INTO unstring_targets opt_pointer opt_tallying opt_overflow_handlers opt_end_unstring
      {
          ASTNode *u = ast_new(AST_STMT_UNSTRING, NULL, g_last_tok.seq_num, g_last_tok.column);
          ast_add(u, $2);
          ast_add(u, $3);
          ast_add(u, $5);
          ast_add(u, $6);
          ast_add(u, $7);
          ast_add(u, $8);
          $$ = u;
      }
    ;

opt_unstring_delim
    : /* empty */ { $$ = NULL; }
    | KW_DELIMITED opt_by opt_all rvalue unstring_or_delims
      { ASTNode *d = ast_new(AST_CLAUSE, "UNSTRING-DELIM", $4->seq_num, $4->col); ast_add(d, $4); $$ = d; }
    ;

opt_all: /* empty */ | KW_ALL ;

unstring_or_delims
    : /* empty */
    | unstring_or_delims KW_OR opt_all rvalue { ast_free($4); }
    ;

unstring_targets
    : unstring_target                           { $$ = ast_add(ast_new(AST_LIST, NULL, 0, 0), $1); }
    | unstring_targets opt_comma unstring_target { $$ = ast_add($1, $3); }
    ;

unstring_target
    : data_ref opt_delim_in opt_count_in { $$ = $1; }
    ;

opt_delim_in: /* empty */ | KW_DELIMITER opt_in data_ref { ast_free($3); } ;
opt_count_in: /* empty */ | KW_COUNT opt_in data_ref { ast_free($3); } ;
opt_in: /* empty */ | KW_IN ;
opt_tallying
    : /* empty */ { $$ = NULL; }
    | KW_TALLYING opt_in data_ref
      { ASTNode *t = ast_new(AST_CLAUSE, "TALLYING", $3->seq_num, $3->col); ast_add(t, $3); $$ = t; }
    ;
opt_end_unstring: /* empty */ %prec LOWER_THAN_HANDLER | KW_END_UNSTRING ;

/* --- SET / SEARCH / SORT / RELEASE / RETURN --- */
stmt_set
    : KW_SET data_ref_list KW_TO rvalue
      {
          ASTNode *s = ast_new(AST_STMT_SET, "TO", g_last_tok.seq_num, g_last_tok.column);
          ast_adopt_children(s, $2); ast_add(s, $4);
          $$ = s;
      }
    | KW_SET data_ref_list KW_TO KW_TRUE
      {
          ASTNode *s = ast_new(AST_STMT_SET, "TO-TRUE", g_last_tok.seq_num, g_last_tok.column);
          ast_adopt_children(s, $2);
          $$ = s;
      }
    | KW_SET data_ref_list KW_TO KW_FALSE
      {
          ASTNode *s = ast_new(AST_STMT_SET, "TO-FALSE", g_last_tok.seq_num, g_last_tok.column);
          ast_adopt_children(s, $2);
          $$ = s;
      }
    | KW_SET data_ref_list KW_UP KW_BY rvalue
      {
          ASTNode *s = ast_new(AST_STMT_SET, "UP", g_last_tok.seq_num, g_last_tok.column);
          ast_adopt_children(s, $2); ast_add(s, $5);
          $$ = s;
      }
    | KW_SET data_ref_list KW_DOWN KW_BY rvalue
      {
          ASTNode *s = ast_new(AST_STMT_SET, "DOWN", g_last_tok.seq_num, g_last_tok.column);
          ast_adopt_children(s, $2); ast_add(s, $5);
          $$ = s;
      }
    ;

stmt_search
    : KW_SEARCH opt_all data_ref opt_search_varying opt_search_at_end search_when_list opt_end_search
      {
          ASTNode *s = ast_new(AST_STMT_SEARCH, $3->text, $3->seq_num, $3->col);
          ast_add(s, $3);
          ast_add(s, $4);
          ast_add(s, $5);
          ast_adopt_children(s, $6);
          $$ = s;
      }
    ;

opt_search_varying
    : /* empty */ { $$ = NULL; }
    | KW_VARYING data_ref { $$ = $2; }
    ;

opt_search_at_end
    : /* empty */ { $$ = NULL; }
    | opt_at KW_END stmt_list
      {
          ASTNode *h = ast_new(AST_CLAUSE, "AT-END", g_last_tok.seq_num, g_last_tok.column);
          ast_adopt_children(h, $3);
          $$ = h;
      }
    ;

search_when_list
    : search_when                  { $$ = ast_add(ast_new(AST_LIST, NULL, 0, 0), $1); }
    | search_when_list search_when { $$ = ast_add($1, $2); }
    ;

search_when
    : KW_WHEN condition stmt_list
      {
          ASTNode *w = ast_new(AST_WHEN, NULL, g_last_tok.seq_num, g_last_tok.column);
          ast_add(w, $2);
          ast_adopt_children(w, $3);
          $$ = w;
      }
    ;

opt_end_search: /* empty */ %prec LOWER_THAN_HANDLER | KW_END_SEARCH ;

stmt_sort
    : KW_SORT data_ref sort_key_list opt_sort_in opt_sort_out
      { ASTNode *s = ast_new(AST_STMT_SORT, $2->text, $2->seq_num, $2->col); ast_add(s, $2); $$ = s; }
    | KW_MERGE data_ref sort_key_list KW_USING data_ref_list opt_sort_out
      { ASTNode *s = ast_new(AST_STMT_MERGE, $2->text, $2->seq_num, $2->col); ast_add(s, $2); ast_free($5); $$ = s; }
    ;

sort_key_list
    : opt_on asc_or_desc opt_key data_ref_list { ast_free($4); }
    | sort_key_list opt_on asc_or_desc opt_key data_ref_list { ast_free($5); }
    ;

opt_sort_in
    : /* empty */
    | KW_USING data_ref_list { ast_free($2); }
    | KW_INPUT KW_PROCEDURE opt_is proc_name opt_thru { free($4); }
    ;

opt_sort_out
    : /* empty */
    | KW_GIVING data_ref_list { ast_free($2); }
    | KW_OUTPUT KW_PROCEDURE opt_is proc_name opt_thru { free($4); }
    ;

stmt_release
    : KW_RELEASE data_ref opt_from
      { ASTNode *r = ast_new(AST_STMT_RELEASE, $2->text, $2->seq_num, $2->col); ast_add(r, $2); $$ = r; }
    ;

stmt_return
    : KW_RETURN data_ref opt_record opt_into opt_read_handlers opt_end_return
      { ASTNode *r = ast_new(AST_STMT_RETURN, $2->text, $2->seq_num, $2->col); ast_add(r, $2); ast_add(r, $5); $$ = r; }
    ;

opt_end_return: /* empty */ %prec LOWER_THAN_HANDLER | KW_END_RETURN ;

/* =========================================================================
 * EXPRESSIONS, CONDITIONS, DATA REFERENCES
 * ========================================================================= */

condition
    : simple_cond                         { $$ = $1; }
    | KW_NOT condition                    { ASTNode *n = ast_new(AST_COND_NOT, "NOT", g_last_tok.seq_num, g_last_tok.column); ast_add(n, $2); $$ = n; }
    | condition KW_AND condition          { $$ = bin_node(AST_COND_LOGIC, "AND", $1, $3); }
    | condition KW_OR condition           { $$ = bin_node(AST_COND_LOGIC, "OR", $1, $3); }
    | condition KW_OR abbreviated_rhs     { $$ = bin_node(AST_COND_LOGIC, "OR", $1, $3); }
    | condition KW_AND abbreviated_rhs    { $$ = bin_node(AST_COND_LOGIC, "AND", $1, $3); }
    ;

/* Supports COBOL abbreviated relation conditions like: IF X > 1 AND < 10 */
abbreviated_rhs
    : relop arith_expr
      { ASTNode *n = ast_new(AST_COND_REL, $1, g_last_tok.seq_num, g_last_tok.column); free($1); ast_add(n, $2); $$ = n; }
    ;

simple_cond
    : T_LPAREN condition T_RPAREN { $$ = $2; }
    | arith_expr KW_IS KW_NOT relop_core arith_expr
      {
          char buf[16];
          snprintf(buf, sizeof(buf), "NOT %s", $4);
          free($4);
          $$ = bin_node(AST_COND_REL, buf, $1, $5);
      }
    | arith_expr KW_IS relop_core arith_expr
      {
          $$ = bin_node(AST_COND_REL, $3, $1, $4);
          free($3);
      }
    | arith_expr KW_NOT relop_core arith_expr
      {
          char buf[16];
          snprintf(buf, sizeof(buf), "NOT %s", $3);
          free($3);
          $$ = bin_node(AST_COND_REL, buf, $1, $4);
      }
    | arith_expr relop_core arith_expr
      {
          $$ = bin_node(AST_COND_REL, $2, $1, $3);
          free($2);
      }
    | arith_expr KW_IS KW_NOT class_type
      {
          ASTNode *n = ast_new_ex(AST_COND_CLASS, $4, "NOT", 0, $1->seq_num, $1->col);
          free($4); ast_add(n, $1); $$ = n;
      }
    | arith_expr KW_IS class_type
      {
          ASTNode *n = ast_new(AST_COND_CLASS, $3, $1->seq_num, $1->col);
          free($3); ast_add(n, $1); $$ = n;
      }
    | arith_expr KW_NOT class_type
      {
          ASTNode *n = ast_new_ex(AST_COND_CLASS, $3, "NOT", 0, $1->seq_num, $1->col);
          free($3); ast_add(n, $1); $$ = n;
      }
    | arith_expr class_type
      {
          ASTNode *n = ast_new(AST_COND_CLASS, $2, $1->seq_num, $1->col);
          free($2); ast_add(n, $1); $$ = n;
      }
    | arith_expr %prec LOWER_THAN_HANDLER
      { $$ = $1; }
    ;

relop
    : opt_is relop_core             { $$ = $2; }
    | opt_is KW_NOT relop_core      { char b[16]; snprintf(b, sizeof(b), "NOT %s", $3); free($3); $$ = strdup(b); }
    ;

relop_core
    : T_EQ                           { $$ = strdup("="); }
    | T_NE                           { $$ = strdup("<>"); }
    | T_LT                           { $$ = strdup("<"); }
    | T_LE                           { $$ = strdup("<="); }
    | T_GT                           { $$ = strdup(">"); }
    | T_GE                           { $$ = strdup(">="); }
    | KW_EQUAL opt_to                { $$ = strdup("="); }
    | KW_GREATER opt_than            { $$ = strdup(">"); }
    | KW_LESS opt_than               { $$ = strdup("<"); }
    | KW_GREATER opt_than KW_OR KW_EQUAL opt_to { $$ = strdup(">="); }
    | KW_LESS opt_than KW_OR KW_EQUAL opt_to    { $$ = strdup("<="); }
    ;

opt_than: /* empty */ | KW_THAN ;

class_type
    : KW_NUMERIC       { $$ = strdup("NUMERIC"); }
    | KW_ALPHABETIC    { $$ = strdup("ALPHABETIC"); }
    | KW_ALPHANUMERIC  { $$ = strdup("ALPHANUMERIC"); }
    | KW_POSITIVE      { $$ = strdup("POSITIVE"); }
    | KW_NEGATIVE      { $$ = strdup("NEGATIVE"); }
    ;

arith_expr
    : rvalue                          { $$ = $1; }
    | arith_expr T_PLUS arith_expr    { $$ = bin_node(AST_BINOP, "+", $1, $3); }
    | arith_expr T_MINUS arith_expr   { $$ = bin_node(AST_BINOP, "-", $1, $3); }
    | arith_expr T_STAR arith_expr    { $$ = bin_node(AST_BINOP, "*", $1, $3); }
    | arith_expr T_SLASH arith_expr   { $$ = bin_node(AST_BINOP, "/", $1, $3); }
    | arith_expr T_POWER arith_expr   { $$ = bin_node(AST_BINOP, "**", $1, $3); }
    | T_MINUS arith_expr %prec UMINUS { ASTNode *u = ast_new(AST_UNOP, "-", g_last_tok.seq_num, g_last_tok.column); ast_add(u, $2); $$ = u; }
    | T_PLUS arith_expr  %prec UMINUS { $$ = $2; }
    | T_LPAREN arith_expr T_RPAREN    { $$ = $2; }
    ;

rvalue_list
    : rvalue                        { $$ = ast_add(ast_new(AST_LIST, NULL, 0, 0), $1); }
    | rvalue_list opt_comma rvalue  { $$ = ast_add($1, $3); }
    ;

rvalue
    : data_ref
    | T_NUM_LIT
      { $$ = ast_new(AST_LITERAL_NUM, $1, g_last_tok.seq_num, g_last_tok.column); free($1); }
    | T_STR_LIT
      { $$ = ast_new(AST_LITERAL_STR, $1, g_last_tok.seq_num, g_last_tok.column); free($1); }
    | T_FIGURATIVE
      { $$ = ast_new(AST_FIGURATIVE, $1, g_last_tok.seq_num, g_last_tok.column); free($1); }
    | KW_FUNCTION func_name T_LPAREN subscript_list T_RPAREN
      {
          ASTNode *f = ast_new(AST_FUNC_CALL, $2, g_last_tok.seq_num, g_last_tok.column);
          free($2);
          ast_adopt_children(f, $4);
          $$ = f;
      }
    | KW_FUNCTION func_name
      { $$ = ast_new(AST_FUNC_CALL, $2, g_last_tok.seq_num, g_last_tok.column); free($2); }
    | KW_LENGTH KW_OF data_ref
      {
          ASTNode *l = ast_new(AST_LENGTH_OF, NULL, $3->seq_num, $3->col);
          ast_add(l, $3);
          $$ = l;
      }
    ;

data_ref_list
    : data_ref                           { $$ = ast_add(ast_new(AST_LIST, NULL, 0, 0), $1); }
    | data_ref_list opt_comma data_ref   { $$ = ast_add($1, $3); }
    ;

data_ref
    : qualified_name
      { $$ = $1; }
    | qualified_name T_LPAREN subscript_list T_RPAREN
      {
          ASTNode *sub = ast_new(AST_SUBSCRIPT, NULL, $1->seq_num, $1->col);
          ast_adopt_children(sub, $3);
          ast_add($1, sub);
          $$ = $1;
      }
    | qualified_name T_LPAREN arith_expr T_COLON arith_expr T_RPAREN
      {
          ASTNode *rm = ast_new(AST_REFMOD, NULL, $1->seq_num, $1->col);
          ast_add(rm, $3); ast_add(rm, $5);
          ast_add($1, rm);
          $$ = $1;
      }
    | qualified_name T_LPAREN arith_expr T_COLON T_RPAREN
      {
          ASTNode *rm = ast_new(AST_REFMOD, NULL, $1->seq_num, $1->col);
          ast_add(rm, $3);
          ast_add($1, rm);
          $$ = $1;
      }
    | qualified_name T_LPAREN subscript_list T_RPAREN T_LPAREN arith_expr T_COLON arith_expr T_RPAREN
      {
          ASTNode *sub = ast_new(AST_SUBSCRIPT, NULL, $1->seq_num, $1->col);
          ast_adopt_children(sub, $3);
          ASTNode *rm = ast_new(AST_REFMOD, NULL, $1->seq_num, $1->col);
          ast_add(rm, $6); ast_add(rm, $8);
          ast_add($1, sub); ast_add($1, rm);
          $$ = $1;
      }
    ;

qualified_name
    : ident_word
      { $$ = ast_new(AST_DATA_REF, $1, g_last_tok.seq_num, g_last_tok.column); free($1); }
    | qualified_name of_or_in ident_word
      {
          ASTNode *q = ast_new(AST_DATA_REF, $3, g_last_tok.seq_num, g_last_tok.column);
          free($3);
          ast_add($1, q);
          $$ = $1;
      }
    ;

of_or_in: KW_OF | KW_IN ;

subscript_list
    : arith_expr                             { $$ = ast_add(ast_new(AST_LIST, NULL, 0, 0), $1); }
    | subscript_list opt_comma arith_expr    { $$ = ast_add($1, $3); }
    ;

opt_comma
    : /* empty */
    | T_COMMA
    | T_SEMICOLON
    ;

proc_name
    : ident_word { $$ = $1; }
    | T_NUM_LIT  { $$ = $1; }
    ;

func_name
    : ident_word { $$ = $1; }
    | KW_RANDOM  { $$ = strdup("RANDOM"); }
    | KW_LENGTH  { $$ = strdup("LENGTH"); }
    ;

ident_word
    : T_IDENTIFIER { $$ = $1; }
    ;

%%

/* =========================================================================
 * TokenList -> Bison Lexer Adapter
 * ========================================================================= */

static const std::unordered_map<std::string, int> &keyword_map() {
    static const std::unordered_map<std::string, int> table = {
        {"IDENTIFICATION", KW_IDENTIFICATION}, {"ENVIRONMENT", KW_ENVIRONMENT},
        {"DATA", KW_DATA}, {"PROCEDURE", KW_PROCEDURE}, {"DIVISION", KW_DIVISION},
        {"SECTION", KW_SECTION}, {"PROGRAM-ID", KW_PROGRAM_ID},
        {"CONFIGURATION", KW_CONFIGURATION}, {"SOURCE-COMPUTER", KW_SOURCE_COMPUTER},
        {"OBJECT-COMPUTER", KW_OBJECT_COMPUTER}, {"SPECIAL-NAMES", KW_SPECIAL_NAMES},
        {"DECIMAL-POINT", KW_DECIMAL_POINT}, {"COMMA", KW_COMMA},
        {"INPUT-OUTPUT", KW_INPUT_OUTPUT}, {"FILE-CONTROL", KW_FILE_CONTROL},
        {"I-O-CONTROL", KW_IO_CONTROL}, {"FILE", KW_FILE},
        {"WORKING-STORAGE", KW_WORKING_STORAGE}, {"LOCAL-STORAGE", KW_LOCAL_STORAGE},
        {"LINKAGE", KW_LINKAGE}, {"SCREEN", KW_SCREEN},

        {"SELECT", KW_SELECT}, {"ASSIGN", KW_ASSIGN}, {"OPTIONAL", KW_OPTIONAL},
        {"ORGANIZATION", KW_ORGANIZATION}, {"LINE", KW_LINE}, {"LINES", KW_LINES},
        {"PAGE", KW_PAGE}, {"SEQUENTIAL", KW_SEQUENTIAL}, {"INDEXED", KW_INDEXED},
        {"RELATIVE", KW_RELATIVE}, {"ACCESS", KW_ACCESS}, {"MODE", KW_MODE},
        {"DYNAMIC", KW_DYNAMIC}, {"RANDOM", KW_RANDOM}, {"RECORD", KW_RECORD},
        {"RECORDS", KW_RECORDS}, {"KEY", KW_KEY}, {"ALTERNATE", KW_ALTERNATE},
        {"DUPLICATES", KW_DUPLICATES}, {"STATUS", KW_STATUS},
        {"FD", KW_FD}, {"SD", KW_SD}, {"BLOCK", KW_BLOCK}, {"CONTAINS", KW_CONTAINS},
        {"LABEL", KW_LABEL}, {"STANDARD", KW_STANDARD}, {"OMITTED", KW_OMITTED},
        {"PIC", KW_PIC}, {"PICTURE", KW_PIC},
        {"VALUE", KW_VALUE}, {"VALUES", KW_VALUE},
        {"REDEFINES", KW_REDEFINES}, {"OCCURS", KW_OCCURS}, {"TIMES", KW_TIMES},
        {"DEPENDING", KW_DEPENDING}, {"ASCENDING", KW_ASCENDING}, {"DESCENDING", KW_DESCENDING},
        {"USAGE", KW_USAGE}, {"COMP", KW_COMP}, {"COMP-1", KW_COMP}, {"COMP-2", KW_COMP},
        {"COMP-3", KW_COMP}, {"COMP-4", KW_COMP}, {"COMP-5", KW_COMP},
        {"COMPUTATIONAL", KW_COMP}, {"COMPUTATIONAL-3", KW_COMP},
        {"BINARY", KW_COMP}, {"PACKED-DECIMAL", KW_COMP},
        {"SIGN", KW_SIGN}, {"LEADING", KW_LEADING}, {"TRAILING", KW_TRAILING},
        {"SEPARATE", KW_SEPARATE}, {"CHARACTER", KW_CHARACTER}, {"CHARACTERS", KW_CHARACTERS},
        {"JUSTIFIED", KW_JUSTIFIED}, {"JUST", KW_JUSTIFIED}, {"RIGHT", KW_RIGHT},
        {"BLANK", KW_BLANK}, {"SYNC", KW_SYNC}, {"SYNCHRONIZED", KW_SYNC},
        {"FILLER", KW_FILLER}, {"GLOBAL", KW_GLOBAL}, {"EXTERNAL", KW_EXTERNAL},
        {"COPY", KW_COPY},

        {"DISPLAY", KW_DISPLAY}, {"ACCEPT", KW_ACCEPT}, {"MOVE", KW_MOVE},
        {"ADD", KW_ADD}, {"SUBTRACT", KW_SUBTRACT}, {"MULTIPLY", KW_MULTIPLY},
        {"DIVIDE", KW_DIVIDE}, {"COMPUTE", KW_COMPUTE},
        {"IF", KW_IF}, {"THEN", KW_THEN}, {"ELSE", KW_ELSE}, {"END-IF", KW_END_IF},
        {"EVALUATE", KW_EVALUATE}, {"WHEN", KW_WHEN}, {"OTHER", KW_OTHER},
        {"ALSO", KW_ALSO}, {"ANY", KW_ANY}, {"END-EVALUATE", KW_END_EVALUATE},
        {"PERFORM", KW_PERFORM}, {"UNTIL", KW_UNTIL}, {"VARYING", KW_VARYING},
        {"AFTER", KW_AFTER}, {"BEFORE", KW_BEFORE}, {"TEST", KW_TEST},
        {"THRU", KW_THRU}, {"THROUGH", KW_THRU}, {"END-PERFORM", KW_END_PERFORM},
        {"OPEN", KW_OPEN}, {"CLOSE", KW_CLOSE}, {"READ", KW_READ}, {"WRITE", KW_WRITE},
        {"REWRITE", KW_REWRITE}, {"DELETE", KW_DELETE}, {"START", KW_START},
        {"END-READ", KW_END_READ}, {"END-WRITE", KW_END_WRITE},
        {"END-REWRITE", KW_END_REWRITE}, {"END-DELETE", KW_END_DELETE},
        {"END-START", KW_END_START},
        {"GO", KW_GO}, {"ALTER", KW_ALTER}, {"PROCEED", KW_PROCEED}, {"COLUMN", KW_COLUMN},
        {"CALL", KW_CALL}, {"CANCEL", KW_CANCEL}, {"END-CALL", KW_END_CALL},
        {"STOP", KW_STOP}, {"RUN", KW_RUN}, {"GOBACK", KW_GOBACK},
        {"EXIT", KW_EXIT}, {"CONTINUE", KW_CONTINUE}, {"NEXT", KW_NEXT},
        {"SENTENCE", KW_SENTENCE},
        {"INITIALIZE", KW_INITIALIZE}, {"INSPECT", KW_INSPECT},
        {"STRING", KW_STRING}, {"UNSTRING", KW_UNSTRING},
        {"SET", KW_SET}, {"SEARCH", KW_SEARCH},
        {"SORT", KW_SORT}, {"MERGE", KW_MERGE}, {"RELEASE", KW_RELEASE}, {"RETURN", KW_RETURN},
        {"END-ADD", KW_END_ADD}, {"END-SUBTRACT", KW_END_SUBTRACT},
        {"END-MULTIPLY", KW_END_MULTIPLY}, {"END-DIVIDE", KW_END_DIVIDE},
        {"END-COMPUTE", KW_END_COMPUTE}, {"END-STRING", KW_END_STRING},
        {"END-UNSTRING", KW_END_UNSTRING}, {"END-SEARCH", KW_END_SEARCH},
        {"END-RETURN", KW_END_RETURN}, {"END-ACCEPT", KW_END_ACCEPT},
        {"END-DISPLAY", KW_END_DISPLAY},

        {"TO", KW_TO}, {"FROM", KW_FROM}, {"BY", KW_BY}, {"INTO", KW_INTO},
        {"GIVING", KW_GIVING}, {"REMAINDER", KW_REMAINDER}, {"OF", KW_OF}, {"IN", KW_IN},
        {"INPUT", KW_INPUT}, {"OUTPUT", KW_OUTPUT}, {"I-O", KW_IO}, {"EXTEND", KW_EXTEND},
        {"AT", KW_AT}, {"END", KW_END}, {"NOT", KW_NOT}, {"INVALID", KW_INVALID},
        {"ON", KW_ON}, {"SIZE", KW_SIZE}, {"ERROR", KW_ERROR},
        {"OVERFLOW", KW_OVERFLOW}, {"EXCEPTION", KW_EXCEPTION},
        {"AND", KW_AND}, {"OR", KW_OR},
        {"EQUAL", KW_EQUAL}, {"EQUALS", KW_EQUAL},
        {"GREATER", KW_GREATER}, {"LESS", KW_LESS}, {"THAN", KW_THAN},
        {"IS", KW_IS}, {"ARE", KW_ARE}, {"TRUE", KW_TRUE}, {"FALSE", KW_FALSE},
        {"DELIMITED", KW_DELIMITED}, {"DELIMITER", KW_DELIMITER},
        {"TALLYING", KW_TALLYING}, {"REPLACING", KW_REPLACING},
        {"CONVERTING", KW_CONVERTING}, {"FOR", KW_FOR}, {"ALL", KW_ALL},
        {"FIRST", KW_FIRST}, {"INITIAL", KW_INITIAL}, {"COUNT", KW_COUNT},
        {"POINTER", KW_POINTER}, {"WITH", KW_WITH}, {"POSITION", KW_POSITION},
        {"ADVANCING", KW_ADVANCING}, {"NO", KW_NO}, {"UPON", KW_UPON},
        {"EOP", KW_EOP}, {"END-OF-PAGE", KW_EOP},
        {"USING", KW_USING}, {"RETURNING", KW_RETURNING},
        {"REFERENCE", KW_REFERENCE}, {"CONTENT", KW_CONTENT},
        {"ROUNDED", KW_ROUNDED}, {"CORRESPONDING", KW_CORRESPONDING}, {"CORR", KW_CORRESPONDING},
        {"NUMERIC", KW_NUMERIC}, {"ALPHABETIC", KW_ALPHABETIC},
        {"ALPHABETIC-LOWER", KW_ALPHABETIC}, {"ALPHABETIC-UPPER", KW_ALPHABETIC},
        {"ALPHANUMERIC", KW_ALPHANUMERIC},
        {"POSITIVE", KW_POSITIVE}, {"NEGATIVE", KW_NEGATIVE},
        {"FUNCTION", KW_FUNCTION}, {"PROGRAM", KW_PROGRAM}, {"PARAGRAPH", KW_PARAGRAPH},
        {"LENGTH", KW_LENGTH}, {"UP", KW_UP}, {"DOWN", KW_DOWN}, {"CYCLE", KW_CYCLE}
    };
    return table;
}

static bool is_id_comment_header(const std::string &s) {
    static const std::unordered_set<std::string> headers = {
        "AUTHOR", "INSTALLATION", "DATE-WRITTEN", "DATE-COMPILED", "SECURITY", "REMARKS"
    };
    return headers.count(s) > 0;
}

static bool is_figurative(const std::string &s) {
    static const std::unordered_set<std::string> figs = {
        "SPACE", "SPACES", "ZERO", "ZEROS", "ZEROES",
        "HIGH-VALUE", "HIGH-VALUES", "LOW-VALUE", "LOW-VALUES",
        "QUOTE", "QUOTES", "NULL", "NULLS"
    };
    return figs.count(s) > 0;
}

static bool is_division_start(size_t idx) {
    if (idx >= g_tokens->size()) return false;
    const Token &t = (*g_tokens)[idx];
    if (t.type != TOKEN_KEYWORD) return false;
    return t.lexeme == "ENVIRONMENT" || t.lexeme == "DATA" || t.lexeme == "PROCEDURE";
}

static int lookup_keyword(const std::string &s) {
    const std::unordered_map<std::string, int> &m = keyword_map();
    auto it = m.find(s);
    return it != m.end() ? it->second : 0;
}

static int cob_lex(void) {
    if (g_pending_comment_text) {
        g_pending_comment_text = 0;
        cob_lval.str = strdup(g_comment_buf.c_str());
        return T_COMMENT_TEXT;
    }

    if (!g_tokens || g_pos >= g_tokens->size()) return 0;

    const Token &t = (*g_tokens)[g_pos++];
    g_last_tok = t;

    if (t.type == TOKEN_KEYWORD) {
        if (t.lexeme == "IDENTIFICATION") {
            g_in_ident_div = 1;
        } else if (t.lexeme == "ENVIRONMENT" || t.lexeme == "DATA" || t.lexeme == "PROCEDURE") {
            g_in_ident_div = 0;
        }

        if (g_in_ident_div && is_id_comment_header(t.lexeme)) {
            if (g_pos < g_tokens->size() && (*g_tokens)[g_pos].type == TOKEN_PERIOD) {
                g_pos++;
            }
            g_comment_buf.clear();
            while (g_pos < g_tokens->size()) {
                const Token &nt = (*g_tokens)[g_pos];
                if (is_division_start(g_pos)) break;
                if (nt.area == AREA_A && nt.type == TOKEN_KEYWORD &&
                    (is_id_comment_header(nt.lexeme) || nt.lexeme == "PROGRAM-ID")) {
                    break;
                }
                if (!g_comment_buf.empty()) g_comment_buf.push_back(' ');
                g_comment_buf.append(nt.lexeme);
                g_pos++;
                if (nt.type == TOKEN_PERIOD) {
                    if (g_pos >= g_tokens->size() || is_division_start(g_pos) ||
                        ((*g_tokens)[g_pos].area == AREA_A &&
                         (*g_tokens)[g_pos].type == TOKEN_KEYWORD &&
                         is_id_comment_header((*g_tokens)[g_pos].lexeme))) {
                        break;
                    }
                }
            }
            if (!g_comment_buf.empty()) g_pending_comment_text = 1;
            cob_lval.str = strdup(t.lexeme.c_str());
            return T_ID_COMMENT_HDR;
        }

        if (is_figurative(t.lexeme)) {
            cob_lval.str = strdup(t.lexeme.c_str());
            return T_FIGURATIVE;
        }

        int kw = lookup_keyword(t.lexeme);
        if (kw != 0) return kw;

        cob_lval.str = strdup(t.lexeme.c_str());
        return T_IDENTIFIER;
    }

    switch (t.type) {
        case TOKEN_IDENTIFIER:
            cob_lval.str = strdup(t.lexeme.c_str());
            return T_IDENTIFIER;
        case TOKEN_LITERAL_NUMERIC:
            cob_lval.str = strdup(t.lexeme.c_str());
            return T_NUM_LIT;
        case TOKEN_LITERAL_ALPHANUMERIC:
            cob_lval.str = strdup(t.lexeme.c_str());
            return T_STR_LIT;
        case TOKEN_PICTURE:
            cob_lval.str = strdup(t.lexeme.c_str());
            return T_PICTURE;
        case TOKEN_PERIOD:    return T_PERIOD;
        case TOKEN_COMMA:     return T_COMMA;
        case TOKEN_SEMICOLON: return T_SEMICOLON;
        case TOKEN_LPAREN:    return T_LPAREN;
        case TOKEN_RPAREN:    return T_RPAREN;
        case TOKEN_COLON:     return T_COLON;
        case TOKEN_PLUS:      return T_PLUS;
        case TOKEN_MINUS:     return T_MINUS;
        case TOKEN_STAR:      return T_STAR;
        case TOKEN_SLASH:     return T_SLASH;
        case TOKEN_POWER:     return T_POWER;
        case TOKEN_EQ:        return T_EQ;
        case TOKEN_NE:        return T_NE;
        case TOKEN_LT:        return T_LT;
        case TOKEN_LE:        return T_LE;
        case TOKEN_GT:        return T_GT;
        case TOKEN_GE:        return T_GE;
        default:              return 0;
    }
}

static void cob_error(const char *msg) {
    g_errors++;
    std::fprintf(stderr,
                 "[E] Syntax Error (%s, Seq %06d, Col %d): %s (near \"%s\")\n",
                 g_filename ? g_filename : "<input>",
                 g_last_tok.seq_num,
                 g_last_tok.column,
                 msg,
                 g_last_tok.lexeme.c_str());
}

ASTNode *parse_tokens(const TokenList &tokens, const std::string &filename, int *error_count) {
    g_tokens = &tokens;
    g_pos = 0;
    g_filename = filename.c_str();
    g_errors = 0;
    g_root = nullptr;
    g_in_ident_div = 0;
    g_pending_comment_text = 0;
    g_comment_buf.clear();
    g_last_tok = Token();

    int rc = cob_parse();
    if (error_count) *error_count = g_errors;
    if (rc != 0 || g_errors > 0) {
        ast_free(g_root);
        g_root = nullptr;
        return nullptr;
    }
    return g_root;
}
