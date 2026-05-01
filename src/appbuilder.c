#include "bdt.h"

#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#endif

typedef struct {
    char path[1024];
    char rel[1024];
} AppFile;

static int ends_with(const char *s, const char *suffix) {
    size_t sl = strlen(s), xl = strlen(suffix);
    return sl >= xl && !strcmp(s + sl - xl, suffix);
}

static int list_contains(const char *csv, const char *item) {
    char items[BDT_MAX_ITEMS][512];
    int count = bdt_split_list(csv, items, BDT_MAX_ITEMS);
    for (int i = 0; i < count; ++i) {
        if (!strcmp(items[i], item)) return 1;
    }
    return 0;
}

static void rel_from_root(const BdtProject *project, const char *path, char *out, size_t out_size) {
    const char *rel = path;
    size_t root_len = strlen(project->root);
    if (!strncmp(path, project->root, root_len)) rel = path + root_len;
    while (*rel == '/' || *rel == '\\') rel++;
    snprintf(out, out_size, "%s", rel);
    for (char *p = out; *p; ++p) {
        if (*p == '\\') *p = '/';
    }
}

static void collect_c_files(const BdtProject *project, const char *dir, AppFile *files, size_t *count, int recursive, const char *suffix) {
#ifdef _WIN32
    char pattern[1024];
    bdt_path_join(pattern, sizeof(pattern), dir, "*");
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..")) continue;
        char path[1024];
        bdt_path_join(path, sizeof(path), dir, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (recursive) collect_c_files(project, path, files, count, recursive, suffix);
        } else if (ends_with(fd.cFileName, suffix) && *count < BDT_MAX_ITEMS) {
            snprintf(files[*count].path, sizeof(files[*count].path), "%s", path);
            rel_from_root(project, path, files[*count].rel, sizeof(files[*count].rel));
            (*count)++;
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        char path[1024];
        bdt_path_join(path, sizeof(path), dir, e->d_name);
        if (bdt_dir_exists(path)) {
            if (recursive) collect_c_files(project, path, files, count, recursive, suffix);
        } else if (ends_with(e->d_name, suffix) && *count < BDT_MAX_ITEMS) {
            snprintf(files[*count].path, sizeof(files[*count].path), "%s", path);
            rel_from_root(project, path, files[*count].rel, sizeof(files[*count].rel));
            (*count)++;
        }
    }
    closedir(d);
#endif
}

static void obj_for_rel(const char *obj_root, const char *rel, char *out, size_t out_size) {
    char obj_rel[1024];
    snprintf(obj_rel, sizeof(obj_rel), "%s", rel);
    char *dot = strrchr(obj_rel, '.');
    if (dot) strcpy(dot, ".o");
    bdt_path_join(out, out_size, obj_root, obj_rel);
}

static void obj_for_app_rel(const char *obj_root, const char *app, const char *rel, char *out, size_t out_size) {
    if (!app || !*app) {
        obj_for_rel(obj_root, rel, out, out_size);
        return;
    }
    char scoped[1024];
    snprintf(scoped, sizeof(scoped), "__apps/%s/%s", app, rel);
    obj_for_rel(obj_root, scoped, out, out_size);
}

static int run_cmd(const char *cmd) {
    int rc = system(cmd);
    return rc == 0 ? 0 : -1;
}

static void append_object_unique(char *objects, size_t objects_size, const char *obj) {
    char needle[1200];
    snprintf(needle, sizeof(needle), "\"%s\"", obj);
    if (strstr(objects, needle)) return;
    strncat(objects, " \"", objects_size - strlen(objects) - 1);
    strncat(objects, obj, objects_size - strlen(objects) - 1);
    strncat(objects, "\"", objects_size - strlen(objects) - 1);
}

static int object_list_contains(char *objects, const char *obj) {
    char needle[1200];
    snprintf(needle, sizeof(needle), "\"%s\"", obj);
    return strstr(objects, needle) != NULL;
}

static const BdtAppRule *find_app_rule(const BdtTarget *target, const char *app) {
    for (size_t i = 0; i < target->app_rule_count; ++i) {
        if (!strcmp(target->app_rules[i].name, app)) return &target->app_rules[i];
    }
    return NULL;
}

static const BdtOutputGroup *find_output_group(const BdtTarget *target, const char *name) {
    const char *wanted = (name && name[0]) ? name : "default";
    for (size_t i = 0; i < target->output_group_count; ++i) {
        if (!strcmp(target->output_groups[i].name, wanted)) return &target->output_groups[i];
    }
    for (size_t i = 0; i < target->output_group_count; ++i) {
        if (!strcmp(target->output_groups[i].name, "default")) return &target->output_groups[i];
    }
    return NULL;
}

static const BdtOutputGroup *output_group_for_app(const BdtTarget *target, const char *app, const BdtAppRule *rule) {
    if (rule && rule->output_group[0]) return find_output_group(target, rule->output_group);
    for (size_t i = 0; i < target->output_group_count; ++i) {
        if (target->output_groups[i].apps[0] && list_contains(target->output_groups[i].apps, app)) return &target->output_groups[i];
    }
    return find_output_group(target, "default");
}

static int rule_excludes_source(const BdtAppRule *rule, const char *rel) {
    if (!rule || !rule->exclude_sources[0]) return 0;
    const char *base = strrchr(rel, '/');
    base = base ? base + 1 : rel;
    return list_contains(rule->exclude_sources, base) || list_contains(rule->exclude_sources, rel);
}

static int source_list_contains_rel(BdtProject *project, const char *source_list, const char *rel) {
    char expanded[BDT_MAX_TEXT];
    char sources[BDT_MAX_ITEMS][512];

    if (!source_list || !source_list[0] || !rel || !rel[0]) return 0;

    bdt_expand_vars(project, source_list, expanded, sizeof(expanded));
    int source_count = bdt_split_list(expanded, sources, BDT_MAX_ITEMS);
    for (int s = 0; s < source_count; ++s) {
        char src[1024], src_rel[1024];

        bdt_expand_vars(project, sources[s], src, sizeof(src));
        rel_from_root(project, src, src_rel, sizeof(src_rel));
        if (!strcmp(src_rel, rel)) return 1;
    }

    return 0;
}

static int compile_one_scoped(BdtProject *project, BdtTarget *target, const char *extra_cflags, const char *src,
                              const char *rel, const char *obj_scope, const char *obj_root, char *obj_out,
                              size_t obj_out_size, int *compiled_out) {
    char flags[BDT_MAX_TEXT], extra_flags[BDT_MAX_TEXT], all_flags[BDT_MAX_TEXT * 2], tool[256], obj_dir[1024], dep[1060];
    char cmd[BDT_MAX_TEXT * 3], reason[256];
    bdt_expand_vars(project, target->tool[0] ? target->tool : "{CC}", tool, sizeof(tool));
    bdt_expand_list_vars(project, target->cflags[0] ? target->cflags : target->flags, flags, sizeof(flags));
    bdt_expand_list_vars(project, extra_cflags ? extra_cflags : "", extra_flags, sizeof(extra_flags));
    snprintf(all_flags, sizeof(all_flags), "%s %s", flags, extra_flags);
    obj_for_app_rel(obj_root, obj_scope, rel, obj_out, obj_out_size);
    snprintf(obj_dir, sizeof(obj_dir), "%s", obj_out);
    char *cut = strrchr(obj_dir, '/');
    char *cut2 = strrchr(obj_dir, '\\');
    if (!cut || cut2 > cut) cut = cut2;
    if (cut) {
        *cut = 0;
        bdt_mkdirs(obj_dir);
    }
    snprintf(dep, sizeof(dep), "%s.d", obj_out);
    if (compiled_out) *compiled_out = 0;
    if (target->cache && bdt_compile_cache_fresh(project, target, src, obj_out, dep, tool, all_flags, reason, sizeof(reason))) {
        bdt_log(BDT_LOG_DEBUG, "skip %s: %s", rel, reason);
        return 0;
    }
    snprintf(cmd, sizeof(cmd), "%s %s -MMD -MP -MF \"%s\" -c \"%s\" -o \"%s\"", tool, all_flags, dep, src, obj_out);
    bdt_log(BDT_LOG_DEBUG, "%s", cmd);
    int rc = run_cmd(cmd);
    if (rc == 0 && compiled_out) *compiled_out = 1;
    if (rc == 0 && target->cache) bdt_compile_cache_store(project, target, src, obj_out, dep, tool, all_flags);
    return rc;
}

static int compile_one(BdtProject *project, BdtTarget *target, const char *extra_cflags, const char *src, const char *rel,
                       const char *obj_root, char *obj_out, size_t obj_out_size, int *compiled_out) {
    return compile_one_scoped(project, target, extra_cflags, src, rel, NULL, obj_root, obj_out, obj_out_size, compiled_out);
}

static int compile_one_needs_scoped(BdtProject *project, BdtTarget *target, const char *extra_cflags, const char *src,
                                    const char *rel, const char *obj_scope, const char *obj_root, char *obj_out,
                                    size_t obj_out_size) {
    char flags[BDT_MAX_TEXT], extra_flags[BDT_MAX_TEXT], all_flags[BDT_MAX_TEXT * 2], tool[256], dep[1060], reason[256];
    bdt_expand_vars(project, target->tool[0] ? target->tool : "{CC}", tool, sizeof(tool));
    bdt_expand_list_vars(project, target->cflags[0] ? target->cflags : target->flags, flags, sizeof(flags));
    bdt_expand_list_vars(project, extra_cflags ? extra_cflags : "", extra_flags, sizeof(extra_flags));
    snprintf(all_flags, sizeof(all_flags), "%s %s", flags, extra_flags);
    obj_for_app_rel(obj_root, obj_scope, rel, obj_out, obj_out_size);
    snprintf(dep, sizeof(dep), "%s.d", obj_out);
    if (!target->cache) return 1;
    return !bdt_compile_cache_fresh(project, target, src, obj_out, dep, tool, all_flags, reason, sizeof(reason));
}

static int compile_one_needs(BdtProject *project, BdtTarget *target, const char *extra_cflags, const char *src,
                             const char *rel, const char *obj_root, char *obj_out, size_t obj_out_size) {
    return compile_one_needs_scoped(project, target, extra_cflags, src, rel, NULL, obj_root, obj_out, obj_out_size);
}

static int link_app(BdtProject *project, BdtTarget *target, const char *out, const char *linker, const char *objects, int *linked_out) {
    char flags[BDT_MAX_TEXT], tool[256], cmd[BDT_MAX_TEXT * 8], reason[256];
    if (linked_out) *linked_out = 0;
    bdt_expand_vars(project, target->tool[0] ? "{LD}" : "{LD}", tool, sizeof(tool));
    bdt_expand_list_vars(project, target->ldflags, flags, sizeof(flags));
    if (target->cache && bdt_link_cache_fresh(project, target, out, objects, tool, flags, linker, reason, sizeof(reason))) {
        bdt_log(BDT_LOG_DEBUG, "skip link %s: %s", out, reason);
        return 0;
    }
    snprintf(cmd, sizeof(cmd), "%s %s -T \"%s\" -o \"%s\" %s", tool, flags, linker, out, objects);
    bdt_log(BDT_LOG_DEBUG, "%s", cmd);
    int rc = run_cmd(cmd);
    if (rc == 0 && linked_out) *linked_out = 1;
    if (rc == 0 && target->cache) bdt_link_cache_store(project, target, out, objects, tool, flags, linker);
    return rc;
}

static int link_app_needs(BdtProject *project, BdtTarget *target, const char *out, const char *linker, const char *objects) {
    char flags[BDT_MAX_TEXT], tool[256], reason[256];
    bdt_expand_vars(project, target->tool[0] ? "{LD}" : "{LD}", tool, sizeof(tool));
    bdt_expand_list_vars(project, target->ldflags, flags, sizeof(flags));
    if (!target->cache) return 1;
    return !bdt_link_cache_fresh(project, target, out, objects, tool, flags, linker, reason, sizeof(reason));
}

static void progress_step(size_t *current, size_t total, const char *label) {
    (*current)++;
    bdt_log_progress(*current, total, label);
}

static size_t count_rule_sources_needs(BdtProject *project, BdtTarget *target, const char *app, const BdtAppRule *rule,
                                       const char *obj_root, char *objects, size_t objects_size) {
    if (!rule) return 0;
    size_t needed = 0;
    char expanded[BDT_MAX_TEXT];
    char sources[BDT_MAX_ITEMS][512];
    bdt_expand_vars(project, rule->sources, expanded, sizeof(expanded));
    int source_count = bdt_split_list(expanded, sources, BDT_MAX_ITEMS);
    for (int s = 0; s < source_count; ++s) {
        char src[1024], rel[1024], obj[1024];
        bdt_expand_vars(project, sources[s], src, sizeof(src));
        rel_from_root(project, src, rel, sizeof(rel));
        if (compile_one_needs_scoped(project, target, rule->cflags, src, rel, app, obj_root, obj, sizeof(obj))) needed++;
        append_object_unique(objects, objects_size, obj);
    }

    char dirs[BDT_MAX_ITEMS][512];
    bdt_expand_vars(project, rule->source_dirs, expanded, sizeof(expanded));
    int dir_count = bdt_split_list(expanded, dirs, BDT_MAX_ITEMS);
    for (int d = 0; d < dir_count; ++d) {
        char dir[1024];
        AppFile files[BDT_MAX_ITEMS];
        size_t count = 0;
        bdt_expand_vars(project, dirs[d], dir, sizeof(dir));
        collect_c_files(project, dir, files, &count, 1, ".c");
        for (size_t i = 0; i < count; ++i) {
            char obj[1024];
            if (rule_excludes_source(rule, files[i].rel)) continue;
            if (compile_one_needs_scoped(project, target, rule->cflags, files[i].path, files[i].rel, app, obj_root, obj, sizeof(obj))) needed++;
            append_object_unique(objects, objects_size, obj);
        }
    }
    return needed;
}

static int compile_rule_sources(BdtProject *project, BdtTarget *target, const char *app, const BdtAppRule *rule,
                                const char *obj_root, char *objects, size_t objects_size, size_t *progress,
                                size_t total_steps) {
    if (!rule) return 0;
    char expanded[BDT_MAX_TEXT];
    char sources[BDT_MAX_ITEMS][512];
    bdt_expand_vars(project, rule->sources, expanded, sizeof(expanded));
    int source_count = bdt_split_list(expanded, sources, BDT_MAX_ITEMS);
    for (int s = 0; s < source_count; ++s) {
        char src[1024], rel[1024], obj[1024];
        bdt_expand_vars(project, sources[s], src, sizeof(src));
        rel_from_root(project, src, rel, sizeof(rel));
        int compiled = 0;
        if (compile_one_scoped(project, target, rule->cflags, src, rel, app, obj_root, obj, sizeof(obj), &compiled) != 0) return -1;
        if (compiled) progress_step(progress, total_steps, rel);
        append_object_unique(objects, objects_size, obj);
    }

    char dirs[BDT_MAX_ITEMS][512];
    bdt_expand_vars(project, rule->source_dirs, expanded, sizeof(expanded));
    int dir_count = bdt_split_list(expanded, dirs, BDT_MAX_ITEMS);
    for (int d = 0; d < dir_count; ++d) {
        char dir[1024];
        AppFile files[BDT_MAX_ITEMS];
        size_t count = 0;
        bdt_expand_vars(project, dirs[d], dir, sizeof(dir));
        collect_c_files(project, dir, files, &count, 1, ".c");
        for (size_t i = 0; i < count; ++i) {
            char obj[1024];
            if (rule_excludes_source(rule, files[i].rel)) continue;
            int compiled = 0;
            if (compile_one_scoped(project, target, rule->cflags, files[i].path, files[i].rel, app, obj_root, obj, sizeof(obj), &compiled) != 0) return -1;
            if (compiled) progress_step(progress, total_steps, files[i].rel);
            append_object_unique(objects, objects_size, obj);
        }
    }
    return 0;
}

int bdt_run_c_apps_target(BdtProject *project, BdtTarget *target) {
    char main_dir[1024], obj_root[1024], out_dir[1024], linker[1024], entry_suffix[64], secondary_suffix[64];
    bdt_expand_vars(project, target->main_dir, main_dir, sizeof(main_dir));
    bdt_expand_vars(project, target->objects, obj_root, sizeof(obj_root));
    bdt_expand_vars(project, target->output, out_dir, sizeof(out_dir));
    bdt_expand_vars(project, target->linker_script, linker, sizeof(linker));
    bdt_expand_vars(project, target->entry_suffix[0] ? target->entry_suffix : "_main.c", entry_suffix, sizeof(entry_suffix));
    bdt_expand_vars(project, target->secondary_entry_suffix, secondary_suffix, sizeof(secondary_suffix));
    bdt_mkdirs(obj_root);
    bdt_mkdirs(out_dir);
    for (size_t g = 0; g < target->output_group_count; ++g) {
        char group_out[1024];
        bdt_expand_vars(project, target->output_groups[g].output, group_out, sizeof(group_out));
        if (group_out[0]) bdt_mkdirs(group_out);
    }

    size_t total_steps = 0;
    char count_shared_objs[BDT_MAX_TEXT * 8] = "";
    char count_dirs[BDT_MAX_ITEMS][512];
    int count_dir_count = bdt_split_list(target->common_dirs, count_dirs, BDT_MAX_ITEMS);
    for (int d = 0; d < count_dir_count; ++d) {
        char dir[1024];
        AppFile files[BDT_MAX_ITEMS];
        size_t count = 0;
        bdt_expand_vars(project, count_dirs[d], dir, sizeof(dir));
        collect_c_files(project, dir, files, &count, 1, ".c");
        for (size_t i = 0; i < count; ++i) {
            char obj[1024];
            if (compile_one_needs(project, target, NULL, files[i].path, files[i].rel, obj_root, obj, sizeof(obj))) total_steps++;
            append_object_unique(count_shared_objs, sizeof(count_shared_objs), obj);
        }
    }
    char count_runtime_sources[BDT_MAX_ITEMS][512];
    int count_runtime_count = bdt_split_list(target->runtime_sources, count_runtime_sources, BDT_MAX_ITEMS);
    char count_shared_runtime_objs[BDT_MAX_TEXT * 4] = "";
    for (int r = 0; r < count_runtime_count; ++r) {
        char src[1024], rel[1024], obj[1024];
        bdt_expand_vars(project, count_runtime_sources[r], src, sizeof(src));
        rel_from_root(project, src, rel, sizeof(rel));
        if (compile_one_needs(project, target, NULL, src, rel, obj_root, obj, sizeof(obj))) total_steps++;
        append_object_unique(count_shared_runtime_objs, sizeof(count_shared_runtime_objs), obj);
    }
    AppFile count_mains[BDT_MAX_ITEMS];
    size_t count_main_count = 0;
    collect_c_files(project, main_dir, count_mains, &count_main_count, 0, entry_suffix);
    for (size_t i = 0; i < count_main_count; ++i) {
        char app[128];
        const char *base = strrchr(count_mains[i].rel, '/');
        base = base ? base + 1 : count_mains[i].rel;
        snprintf(app, sizeof(app), "%s", base);
        char *suffix = strstr(app, entry_suffix);
        if (suffix) *suffix = 0;
        if (list_contains(target->skip_apps, app)) continue;
        char obj[1024], objects[BDT_MAX_TEXT * 8], out[1024];
        const BdtAppRule *rule = find_app_rule(target, app);
        if (compile_one_needs(project, target, rule ? rule->cflags : NULL, count_mains[i].path, count_mains[i].rel, obj_root, obj, sizeof(obj))) total_steps++;
        snprintf(objects, sizeof(objects), "%s \"%s\"", count_shared_objs, obj);
        if ((!rule || rule->include_runtime) && !list_contains(target->runtime_exclude_apps, app)) {
            strncat(objects, count_shared_runtime_objs, sizeof(objects) - strlen(objects) - 1);
        }
        total_steps += count_rule_sources_needs(project, target, app, rule, obj_root, objects, sizeof(objects));
        AppFile top_extras[BDT_MAX_ITEMS];
        size_t top_extra_count = 0;
        collect_c_files(project, main_dir, top_extras, &top_extra_count, 0, ".c");
        for (size_t e = 0; e < top_extra_count; ++e) {
            const char *top_base = strrchr(top_extras[e].rel, '/');
            top_base = top_base ? top_base + 1 : top_extras[e].rel;
            char prefix[160];
            snprintf(prefix, sizeof(prefix), "%s_", app);
            if (strncmp(top_base, prefix, strlen(prefix))) continue;
            if (ends_with(top_extras[e].rel, entry_suffix) || (secondary_suffix[0] && ends_with(top_extras[e].rel, secondary_suffix))) continue;
            if (source_list_contains_rel(project, target->runtime_sources, top_extras[e].rel)) continue;
            char shared_obj[1024];
            obj_for_rel(obj_root, top_extras[e].rel, shared_obj, sizeof(shared_obj));
            if (object_list_contains(count_shared_objs, shared_obj) || object_list_contains(count_shared_runtime_objs, shared_obj)) continue;
            char extra_obj[1024];
            if (compile_one_needs_scoped(project, target, rule ? rule->cflags : NULL, top_extras[e].path, top_extras[e].rel, app, obj_root, extra_obj, sizeof(extra_obj))) total_steps++;
            append_object_unique(objects, sizeof(objects), extra_obj);
        }
        char app_subdir[1024];
        AppFile extras[BDT_MAX_ITEMS];
        size_t extra_count = 0;
        bdt_path_join(app_subdir, sizeof(app_subdir), main_dir, app);
        collect_c_files(project, app_subdir, extras, &extra_count, 0, ".c");
        for (size_t e = 0; e < extra_count; ++e) {
            if (ends_with(extras[e].rel, entry_suffix) || (secondary_suffix[0] && ends_with(extras[e].rel, secondary_suffix))) continue;
            if (source_list_contains_rel(project, target->runtime_sources, extras[e].rel)) continue;
            char shared_obj[1024];
            obj_for_rel(obj_root, extras[e].rel, shared_obj, sizeof(shared_obj));
            if (object_list_contains(count_shared_objs, shared_obj) || object_list_contains(count_shared_runtime_objs, shared_obj)) continue;
            char extra_obj[1024];
            if (compile_one_needs_scoped(project, target, rule ? rule->cflags : NULL, extras[e].path, extras[e].rel, app, obj_root, extra_obj, sizeof(extra_obj))) total_steps++;
            append_object_unique(objects, sizeof(objects), extra_obj);
        }
        const BdtOutputGroup *group = output_group_for_app(target, app, rule);
        char dest_buf[1024], linker_buf[1024];
        bdt_expand_vars(project, group && group->output[0] ? group->output : target->output, dest_buf, sizeof(dest_buf));
        bdt_expand_vars(project, group && group->linker_script[0] ? group->linker_script : target->linker_script, linker_buf, sizeof(linker_buf));
        const char *dest = dest_buf[0] ? dest_buf : out_dir;
        char app_elf[256];
        snprintf(app_elf, sizeof(app_elf), "%s.elf", app);
        bdt_path_join(out, sizeof(out), dest, app_elf);
        if (link_app_needs(project, target, out, linker_buf[0] ? linker_buf : linker, objects)) total_steps++;
    }
    AppFile count_secondary[BDT_MAX_ITEMS];
    size_t count_secondary_count = 0;
    if (secondary_suffix[0]) collect_c_files(project, main_dir, count_secondary, &count_secondary_count, 0, secondary_suffix);
    for (size_t i = 0; i < count_secondary_count; ++i) {
        char app[128], obj[1024], out[1024], objects[BDT_MAX_TEXT * 2];
        const char *base = strrchr(count_secondary[i].rel, '/');
        base = base ? base + 1 : count_secondary[i].rel;
        snprintf(app, sizeof(app), "%s", base);
        char *suffix = strstr(app, secondary_suffix);
        if (suffix) *suffix = 0;
        if (compile_one_needs(project, target, NULL, count_secondary[i].path, count_secondary[i].rel, obj_root, obj, sizeof(obj))) total_steps++;
        const BdtOutputGroup *group = find_output_group(target, target->secondary_output_group);
        char group_out[1024], group_linker[1024];
        bdt_expand_vars(project, group && group->output[0] ? group->output : target->output, group_out, sizeof(group_out));
        bdt_expand_vars(project, group && group->linker_script[0] ? group->linker_script : target->linker_script, group_linker, sizeof(group_linker));
        char app_elf[256];
        snprintf(app_elf, sizeof(app_elf), "%s.elf", app);
        bdt_path_join(out, sizeof(out), group_out[0] ? group_out : out_dir, app_elf);
        snprintf(objects, sizeof(objects), "\"%s\"", obj);
        if (link_app_needs(project, target, out, group_linker[0] ? group_linker : linker, objects)) total_steps++;
    }
    if (total_steps == 0 && target->cache) bdt_log(BDT_LOG_INFO, "%s: %s", bdt_msg(project->lang, "cache_hit"), target->name);
    size_t progress = 0;

    char shared_objs[BDT_MAX_TEXT * 8] = "";
    char dirs[BDT_MAX_ITEMS][512];
    int dir_count = bdt_split_list(target->common_dirs, dirs, BDT_MAX_ITEMS);
    for (int d = 0; d < dir_count; ++d) {
        char dir[1024];
        bdt_expand_vars(project, dirs[d], dir, sizeof(dir));
        AppFile files[BDT_MAX_ITEMS];
        size_t count = 0;
        collect_c_files(project, dir, files, &count, 1, ".c");
        for (size_t i = 0; i < count; ++i) {
            char obj[1024];
            int compiled = 0;
            if (compile_one(project, target, NULL, files[i].path, files[i].rel, obj_root, obj, sizeof(obj), &compiled) != 0) return -1;
            if (compiled) progress_step(&progress, total_steps, files[i].rel);
            append_object_unique(shared_objs, sizeof(shared_objs), obj);
        }
    }

    char runtime_objs[BDT_MAX_TEXT * 4] = "";
    char runtime_sources[BDT_MAX_ITEMS][512];
    int runtime_count = bdt_split_list(target->runtime_sources, runtime_sources, BDT_MAX_ITEMS);
    for (int r = 0; r < runtime_count; ++r) {
        char src[1024], rel[1024], obj[1024];
        bdt_expand_vars(project, runtime_sources[r], src, sizeof(src));
        rel_from_root(project, src, rel, sizeof(rel));
        int compiled = 0;
        if (compile_one(project, target, NULL, src, rel, obj_root, obj, sizeof(obj), &compiled) != 0) return -1;
        if (compiled) progress_step(&progress, total_steps, rel);
        append_object_unique(runtime_objs, sizeof(runtime_objs), obj);
    }

    AppFile mains[BDT_MAX_ITEMS];
    size_t main_count = 0;
    collect_c_files(project, main_dir, mains, &main_count, 0, entry_suffix);
    for (size_t i = 0; i < main_count; ++i) {
        char app[128], obj[1024], objects[BDT_MAX_TEXT * 8], out[1024];
        const char *base = strrchr(mains[i].rel, '/');
        base = base ? base + 1 : mains[i].rel;
        snprintf(app, sizeof(app), "%s", base);
        char *suffix = strstr(app, entry_suffix);
        if (suffix) *suffix = 0;
        if (list_contains(target->skip_apps, app)) continue;
        const BdtAppRule *rule = find_app_rule(target, app);
        int compiled = 0;
        if (compile_one(project, target, rule ? rule->cflags : NULL, mains[i].path, mains[i].rel, obj_root, obj, sizeof(obj), &compiled) != 0) return -1;
        if (compiled) progress_step(&progress, total_steps, mains[i].rel);
        snprintf(objects, sizeof(objects), "%s \"%s\"", shared_objs, obj);
        if ((!rule || rule->include_runtime) && !list_contains(target->runtime_exclude_apps, app)) {
            strncat(objects, runtime_objs, sizeof(objects) - strlen(objects) - 1);
        }
        if (compile_rule_sources(project, target, app, rule, obj_root, objects, sizeof(objects), &progress, total_steps) != 0) return -1;

        AppFile extras[BDT_MAX_ITEMS];
        size_t extra_count = 0;
        AppFile top_extras[BDT_MAX_ITEMS];
        size_t top_extra_count = 0;
        char app_subdir[1024];
        collect_c_files(project, main_dir, top_extras, &top_extra_count, 0, ".c");
        for (size_t e = 0; e < top_extra_count; ++e) {
            const char *top_base = strrchr(top_extras[e].rel, '/');
            top_base = top_base ? top_base + 1 : top_extras[e].rel;
            char prefix[160];
            snprintf(prefix, sizeof(prefix), "%s_", app);
            if (strncmp(top_base, prefix, strlen(prefix))) continue;
            if (ends_with(top_extras[e].rel, entry_suffix) || (secondary_suffix[0] && ends_with(top_extras[e].rel, secondary_suffix))) continue;
            if (source_list_contains_rel(project, target->runtime_sources, top_extras[e].rel)) continue;
            char shared_obj[1024];
            obj_for_rel(obj_root, top_extras[e].rel, shared_obj, sizeof(shared_obj));
            if (object_list_contains(shared_objs, shared_obj) || object_list_contains(runtime_objs, shared_obj)) continue;
            char extra_obj[1024];
            int compiled_extra = 0;
            if (compile_one_scoped(project, target, rule ? rule->cflags : NULL, top_extras[e].path, top_extras[e].rel, app, obj_root, extra_obj, sizeof(extra_obj), &compiled_extra) != 0) return -1;
            if (compiled_extra) progress_step(&progress, total_steps, top_extras[e].rel);
            append_object_unique(objects, sizeof(objects), extra_obj);
        }

        bdt_path_join(app_subdir, sizeof(app_subdir), main_dir, app);
        collect_c_files(project, app_subdir, extras, &extra_count, 0, ".c");
        for (size_t e = 0; e < extra_count; ++e) {
            if (ends_with(extras[e].rel, entry_suffix) || (secondary_suffix[0] && ends_with(extras[e].rel, secondary_suffix))) continue;
            if (source_list_contains_rel(project, target->runtime_sources, extras[e].rel)) continue;
            char shared_obj[1024];
            obj_for_rel(obj_root, extras[e].rel, shared_obj, sizeof(shared_obj));
            if (object_list_contains(shared_objs, shared_obj) || object_list_contains(runtime_objs, shared_obj)) continue;
            char extra_obj[1024];
            int compiled_extra = 0;
            if (compile_one_scoped(project, target, rule ? rule->cflags : NULL, extras[e].path, extras[e].rel, app, obj_root, extra_obj, sizeof(extra_obj), &compiled_extra) != 0) return -1;
            if (compiled_extra) progress_step(&progress, total_steps, extras[e].rel);
            append_object_unique(objects, sizeof(objects), extra_obj);
        }

        const BdtOutputGroup *group = output_group_for_app(target, app, rule);
        char dest_buf[1024], linker_buf[1024];
        bdt_expand_vars(project, group && group->output[0] ? group->output : target->output, dest_buf, sizeof(dest_buf));
        bdt_expand_vars(project, group && group->linker_script[0] ? group->linker_script : target->linker_script, linker_buf, sizeof(linker_buf));
        const char *dest = dest_buf[0] ? dest_buf : out_dir;
        char app_elf[256];
        snprintf(app_elf, sizeof(app_elf), "%s.elf", app);
        bdt_path_join(out, sizeof(out), dest, app_elf);
        char link_label[256];
        snprintf(link_label, sizeof(link_label), "link %s.elf", app);
        int linked = 0;
        if (link_app(project, target, out, linker_buf[0] ? linker_buf : linker, objects, &linked) != 0) return -1;
        if (linked) progress_step(&progress, total_steps, link_label);
    }

    AppFile secondary_entries[BDT_MAX_ITEMS];
    size_t secondary_count = 0;
    if (secondary_suffix[0]) collect_c_files(project, main_dir, secondary_entries, &secondary_count, 0, secondary_suffix);
    for (size_t i = 0; i < secondary_count; ++i) {
        char app[128], obj[1024], out[1024], objects[BDT_MAX_TEXT * 2];
        const char *base = strrchr(secondary_entries[i].rel, '/');
        base = base ? base + 1 : secondary_entries[i].rel;
        snprintf(app, sizeof(app), "%s", base);
        char *suffix = strstr(app, secondary_suffix);
        if (suffix) *suffix = 0;
        int compiled = 0;
        if (compile_one(project, target, NULL, secondary_entries[i].path, secondary_entries[i].rel, obj_root, obj, sizeof(obj), &compiled) != 0) return -1;
        if (compiled) progress_step(&progress, total_steps, secondary_entries[i].rel);
        char app_elf[256];
        snprintf(app_elf, sizeof(app_elf), "%s.elf", app);
        const BdtOutputGroup *group = find_output_group(target, target->secondary_output_group);
        char group_out[1024], group_linker[1024];
        bdt_expand_vars(project, group && group->output[0] ? group->output : target->output, group_out, sizeof(group_out));
        bdt_expand_vars(project, group && group->linker_script[0] ? group->linker_script : target->linker_script, group_linker, sizeof(group_linker));
        bdt_path_join(out, sizeof(out), group_out[0] ? group_out : out_dir, app_elf);
        snprintf(objects, sizeof(objects), "\"%s\"", obj);
        char link_label[256];
        snprintf(link_label, sizeof(link_label), "link %s.elf", app);
        int linked = 0;
        if (link_app(project, target, out, group_linker[0] ? group_linker : linker, objects, &linked) != 0) return -1;
        if (linked) progress_step(&progress, total_steps, link_label);
    }
    return 0;
}
