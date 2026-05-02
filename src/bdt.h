#ifndef BDT_H
#define BDT_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#define BDT_MAX_ITEMS 256
#define BDT_MAX_TEXT 4096
#define BDT_MAX_CONFIG_SECTIONS 128
#define BDT_MAX_CONFIG_ITEMS 128
#define BDT_MAX_TARGETS 128
#define BDT_MAX_OUTPUT_GROUPS 32
#define BDT_MAX_APP_RULES 64
#define BDT_MAX_VARS 128
#define BDT_MAX_PLUGINS 32
#define BDT_MAX_SUBPROJECTS 64
#define BDT_MAX_BUILD_FILES 128

typedef enum {
    BDT_LANG_EN = 0,
    BDT_LANG_ZH = 1
} BdtLanguage;

typedef enum {
    BDT_LOG_DEBUG = 0,
    BDT_LOG_INFO,
    BDT_LOG_STEP,
    BDT_LOG_WARN,
    BDT_LOG_ERROR
} BdtLogLevel;

typedef struct {
    char key[96];
    char value[BDT_MAX_TEXT];
} BdtPair;

typedef struct {
    char name[96];
    char sources[BDT_MAX_TEXT];
    char source_dirs[BDT_MAX_TEXT];
    char exclude_sources[BDT_MAX_TEXT];
    char cflags[BDT_MAX_TEXT];
    char output_group[96];
    int include_runtime;
} BdtAppRule;

typedef struct {
    char name[96];
    char output[1024];
    char linker_script[1024];
    char apps[BDT_MAX_TEXT];
} BdtOutputGroup;

typedef struct {
    char name[96];
    BdtPair items[BDT_MAX_CONFIG_ITEMS];
    size_t item_count;
} BdtSection;

typedef struct {
    BdtSection sections[BDT_MAX_CONFIG_SECTIONS];
    size_t section_count;
} BdtConfig;

typedef struct {
    char name[96];
    char runner[128];
    char path[1024];
    char command[BDT_MAX_TEXT];
} BdtPlugin;

typedef struct {
    char path[1024];
    char archive[1024];
    char pull_command[BDT_MAX_TEXT];
    char push_command[BDT_MAX_TEXT];
} BdtCacheConfig;

typedef struct {
    char name[96];
    char type[64];
    char deps[BDT_MAX_TEXT];
    char command[BDT_MAX_TEXT];
    char steps[BDT_MAX_TEXT];
    char inputs[BDT_MAX_TEXT];
    char outputs[BDT_MAX_TEXT];
    char sources[BDT_MAX_TEXT];
    char source_dirs[BDT_MAX_TEXT];
    char objects[BDT_MAX_TEXT];
    char output[1024];
    char flags[BDT_MAX_TEXT];
    char cflags[BDT_MAX_TEXT];
    char cxxflags[BDT_MAX_TEXT];
    char asflags[BDT_MAX_TEXT];
    char ldflags[BDT_MAX_TEXT];
    char includes[BDT_MAX_TEXT];
    char linker_script[1024];
    char tool[128];
    char plugin[128];
    char common_dirs[BDT_MAX_TEXT];
    char main_dir[1024];
    char runtime_sources[BDT_MAX_TEXT];
    char entry_suffix[64];
    char secondary_entry_suffix[64];
    char secondary_output_group[96];
    char runtime_exclude_apps[BDT_MAX_TEXT];
    char skip_apps[BDT_MAX_TEXT];
    BdtOutputGroup output_groups[BDT_MAX_OUTPUT_GROUPS];
    size_t output_group_count;
    BdtAppRule app_rules[BDT_MAX_APP_RULES];
    size_t app_rule_count;
    int always;
    int cache;
    int parallel;
    int visited;
    int active;
    int done;
} BdtTarget;

typedef struct {
    char root[1024];
    char name[128];
    char default_target[96];
    char build_dir[1024];
    char language[32];
    char doctor_tools[BDT_MAX_TEXT];
    BdtCacheConfig cache_config;
    BdtLanguage lang;
    BdtPair vars[BDT_MAX_VARS];
    size_t var_count;
    BdtPlugin plugins[BDT_MAX_PLUGINS];
    size_t plugin_count;
    BdtTarget targets[BDT_MAX_TARGETS];
    size_t target_count;
    char subprojects[BDT_MAX_SUBPROJECTS][512];
    size_t subproject_count;
    char build_files[BDT_MAX_BUILD_FILES][1024];
    size_t build_file_count;
} BdtProject;

typedef struct {
    int argc;
    char **argv;
    const char *target;
    const char *project_file;
    int jobs;
    int list;
    int scan;
    int graph;
    int view;
    int explain;
    int why;
    int status;
    int trace;
    int bench;
    int clean_target;
    int doctor;
    int cache_cmd;
    int no_cache;
    int verbose;
    const char *explain_target;
    const char *why_query;
    const char *status_target;
    const char *trace_target;
    const char *trace_path;
    const char *bench_target;
    const char *clean_name;
    const char *cache_action;
    const char *cache_arg;
} BdtCli;

void bdt_log_init(BdtLanguage lang, int verbose);
void bdt_log(BdtLogLevel level, const char *fmt, ...);
void bdt_log_progress(size_t current, size_t total, const char *label);
const char *bdt_msg(BdtLanguage lang, const char *key);
BdtLanguage bdt_lang_from_text(const char *s);

int bdt_read_config(const char *path, BdtConfig *config);
const char *bdt_config_get(const BdtConfig *config, const char *section, const char *key);

int bdt_load_project(const char *root, const char *project_file, BdtProject *project);
int bdt_scan_build_files(BdtProject *project);
void bdt_print_scan(const BdtProject *project);
void bdt_print_targets(const BdtProject *project);
void bdt_print_graph(const BdtProject *project);
int bdt_view_project(const BdtProject *project);
int bdt_status_project(BdtProject *project, const char *name);
int bdt_why_query(BdtProject *project, const char *query);

BdtTarget *bdt_find_target(BdtProject *project, const char *name);
int bdt_run_target(BdtProject *project, const char *name, int no_cache);
int bdt_run_command_target(BdtProject *project, BdtTarget *target, int no_cache);
int bdt_run_c_apps_target(BdtProject *project, BdtTarget *target);
int bdt_c_apps_status(BdtProject *project, BdtTarget *target, size_t *compile_count, size_t *link_count, size_t *app_relink_count);
int bdt_run_plugin_target(BdtProject *project, BdtTarget *target);
BdtPlugin *bdt_find_plugin(BdtProject *project, const char *name);

uint64_t bdt_hash_file(const char *path);
uint64_t bdt_hash_text(const char *text);
uint64_t bdt_hash_target_inputs(const BdtProject *project, const BdtTarget *target);
int bdt_cache_is_fresh(const BdtProject *project, const BdtTarget *target, uint64_t hash);
int bdt_cache_store(const BdtProject *project, const BdtTarget *target, uint64_t hash);
int bdt_compile_cache_fresh(const BdtProject *project, const BdtTarget *target, const char *src, const char *obj,
                            const char *dep, const char *tool, const char *flags, char *reason, size_t reason_size);
int bdt_compile_cache_quick_fresh(const BdtProject *project, const BdtTarget *target, const char *src, const char *obj,
                                  const char *dep, const char *tool, const char *flags, char *reason, size_t reason_size);
int bdt_target_cache_has_manifests(const BdtProject *project, const BdtTarget *target);
int bdt_compile_cache_store(const BdtProject *project, const BdtTarget *target, const char *src, const char *obj,
                            const char *dep, const char *tool, const char *flags);
int bdt_link_cache_fresh(const BdtProject *project, const BdtTarget *target, const char *out, const char *objects,
                         const char *tool, const char *flags, const char *script, char *reason, size_t reason_size);
int bdt_link_cache_quick_fresh(const BdtProject *project, const BdtTarget *target, const char *out, const char *objects,
                               const char *tool, const char *flags, const char *script, char *reason, size_t reason_size);
int bdt_link_cache_store(const BdtProject *project, const BdtTarget *target, const char *out, const char *objects,
                         const char *tool, const char *flags, const char *script);
uint64_t bdt_hash_depfile(const BdtProject *project, const char *depfile);
uint64_t bdt_hash_tool_version(const char *tool);
int bdt_remove_path(const char *path);
int bdt_explain_target(BdtProject *project, const char *name);
int bdt_clean_named_target(BdtProject *project, const char *name);
int bdt_doctor(BdtProject *project);
int bdt_cache_command(BdtProject *project, const char *action, const char *arg);
int bdt_trace_begin(const BdtProject *project, const char *path);
void bdt_trace_end(int rc);
int bdt_trace_enabled(void);
void bdt_trace_event(const char *kind, const char *target, const char *detail, int rc, long duration_ms);
int bdt_bench_report(const char *trace_path);

int bdt_mkdirs(const char *path);
int bdt_file_exists(const char *path);
int bdt_dir_exists(const char *path);
int bdt_path_join(char *out, size_t out_size, const char *a, const char *b);
int bdt_abs_path(char *out, size_t out_size, const char *path);
char *bdt_trim(char *s);
int bdt_split_list(const char *text, char items[][512], size_t max_items);
int bdt_split_delim(const char *text, char delim, char items[][512], size_t max_items);
int bdt_expand_vars(const BdtProject *project, const char *input, char *out, size_t out_size);
int bdt_expand_list_vars(const BdtProject *project, const char *input, char *out, size_t out_size);
int bdt_parse_cli(int argc, char **argv, BdtCli *cli);
int bdt_command_exists(const char *cmd);
void bdt_load_toolchain(BdtProject *project, const BdtConfig *config);

#endif
