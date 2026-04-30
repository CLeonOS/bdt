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

static const BdtAppRule *find_app_rule(const BdtTarget *target, const char *app) {
    for (size_t i = 0; i < target->app_rule_count; ++i) {
        if (!strcmp(target->app_rules[i].name, app)) return &target->app_rules[i];
    }
    return NULL;
}

static int rule_excludes_source(const BdtAppRule *rule, const char *rel) {
    if (!rule || !rule->exclude_sources[0]) return 0;
    const char *base = strrchr(rel, '/');
    base = base ? base + 1 : rel;
    return list_contains(rule->exclude_sources, base) || list_contains(rule->exclude_sources, rel);
}

static int compile_one(BdtProject *project, BdtTarget *target, const char *extra_cflags, const char *src, const char *rel,
                       const char *obj_root, char *obj_out, size_t obj_out_size) {
    char flags[BDT_MAX_TEXT], extra_flags[BDT_MAX_TEXT], all_flags[BDT_MAX_TEXT * 2], tool[256], obj_dir[1024], cmd[BDT_MAX_TEXT * 3];
    bdt_expand_vars(project, target->tool[0] ? target->tool : "{CC}", tool, sizeof(tool));
    bdt_expand_list_vars(project, target->cflags[0] ? target->cflags : target->flags, flags, sizeof(flags));
    bdt_expand_list_vars(project, extra_cflags ? extra_cflags : "", extra_flags, sizeof(extra_flags));
    snprintf(all_flags, sizeof(all_flags), "%s %s", flags, extra_flags);
    obj_for_rel(obj_root, rel, obj_out, obj_out_size);
    snprintf(obj_dir, sizeof(obj_dir), "%s", obj_out);
    char *cut = strrchr(obj_dir, '/');
    char *cut2 = strrchr(obj_dir, '\\');
    if (!cut || cut2 > cut) cut = cut2;
    if (cut) {
        *cut = 0;
        bdt_mkdirs(obj_dir);
    }
    snprintf(cmd, sizeof(cmd), "%s %s -c \"%s\" -o \"%s\"", tool, all_flags, src, obj_out);
    bdt_log(BDT_LOG_DEBUG, "%s", cmd);
    return run_cmd(cmd);
}

static int link_app(BdtProject *project, BdtTarget *target, const char *out, const char *linker, const char *objects) {
    char flags[BDT_MAX_TEXT], tool[256], cmd[BDT_MAX_TEXT * 8];
    bdt_expand_vars(project, target->tool[0] ? "{LD}" : "{LD}", tool, sizeof(tool));
    bdt_expand_list_vars(project, target->ldflags, flags, sizeof(flags));
    snprintf(cmd, sizeof(cmd), "%s %s -T \"%s\" -o \"%s\" %s", tool, flags, linker, out, objects);
    bdt_log(BDT_LOG_DEBUG, "%s", cmd);
    return run_cmd(cmd);
}

static void progress_step(size_t *current, size_t total, const char *label) {
    (*current)++;
    bdt_log_progress(*current, total, label);
}

static void count_rule_sources(BdtProject *project, const BdtAppRule *rule, size_t *total_steps) {
    if (!rule) return;
    char expanded[BDT_MAX_TEXT];
    char sources[BDT_MAX_ITEMS][512];
    bdt_expand_vars(project, rule->sources, expanded, sizeof(expanded));
    int source_count = bdt_split_list(expanded, sources, BDT_MAX_ITEMS);
    *total_steps += (size_t)source_count;

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
            if (!rule_excludes_source(rule, files[i].rel)) (*total_steps)++;
        }
    }
}

static int compile_rule_sources(BdtProject *project, BdtTarget *target, const BdtAppRule *rule, const char *obj_root,
                                char *objects, size_t objects_size, size_t *progress, size_t total_steps) {
    if (!rule) return 0;
    char expanded[BDT_MAX_TEXT];
    char sources[BDT_MAX_ITEMS][512];
    bdt_expand_vars(project, rule->sources, expanded, sizeof(expanded));
    int source_count = bdt_split_list(expanded, sources, BDT_MAX_ITEMS);
    for (int s = 0; s < source_count; ++s) {
        char src[1024], rel[1024], obj[1024];
        bdt_expand_vars(project, sources[s], src, sizeof(src));
        rel_from_root(project, src, rel, sizeof(rel));
        progress_step(progress, total_steps, rel);
        if (compile_one(project, target, rule->cflags, src, rel, obj_root, obj, sizeof(obj)) != 0) return -1;
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
            progress_step(progress, total_steps, files[i].rel);
            if (compile_one(project, target, rule->cflags, files[i].path, files[i].rel, obj_root, obj, sizeof(obj)) != 0) return -1;
            append_object_unique(objects, objects_size, obj);
        }
    }
    return 0;
}

int bdt_run_c_apps_target(BdtProject *project, BdtTarget *target) {
    char main_dir[1024], obj_root[1024], out_dir[1024], sys_out[1024], uwm_out[1024], linker[1024], sys_linker[1024];
    bdt_expand_vars(project, target->main_dir, main_dir, sizeof(main_dir));
    bdt_expand_vars(project, target->objects, obj_root, sizeof(obj_root));
    bdt_expand_vars(project, target->output, out_dir, sizeof(out_dir));
    bdt_expand_vars(project, target->system_output, sys_out, sizeof(sys_out));
    bdt_expand_vars(project, target->uwm_output, uwm_out, sizeof(uwm_out));
    bdt_expand_vars(project, target->linker_script, linker, sizeof(linker));
    bdt_expand_vars(project, target->system_linker_script, sys_linker, sizeof(sys_linker));
    bdt_mkdirs(obj_root);
    bdt_mkdirs(out_dir);
    if (sys_out[0]) bdt_mkdirs(sys_out);
    if (uwm_out[0]) bdt_mkdirs(uwm_out);

    size_t total_steps = 0;
    char count_dirs[BDT_MAX_ITEMS][512];
    int count_dir_count = bdt_split_list(target->common_dirs, count_dirs, BDT_MAX_ITEMS);
    for (int d = 0; d < count_dir_count; ++d) {
        char dir[1024];
        AppFile files[BDT_MAX_ITEMS];
        size_t count = 0;
        bdt_expand_vars(project, count_dirs[d], dir, sizeof(dir));
        collect_c_files(project, dir, files, &count, 1, ".c");
        total_steps += count;
    }
    char count_runtime_sources[BDT_MAX_ITEMS][512];
    int count_runtime_count = bdt_split_list(target->runtime_sources, count_runtime_sources, BDT_MAX_ITEMS);
    total_steps += (size_t)count_runtime_count;
    AppFile count_mains[BDT_MAX_ITEMS];
    size_t count_main_count = 0;
    collect_c_files(project, main_dir, count_mains, &count_main_count, 0, "_main.c");
    for (size_t i = 0; i < count_main_count; ++i) {
        char app[128];
        const char *base = strrchr(count_mains[i].rel, '/');
        base = base ? base + 1 : count_mains[i].rel;
        snprintf(app, sizeof(app), "%s", base);
        char *suffix = strstr(app, "_main.c");
        if (suffix) *suffix = 0;
        if (list_contains(target->skip_apps, app)) continue;
        total_steps += 2;
        count_rule_sources(project, find_app_rule(target, app), &total_steps);
        AppFile top_extras[BDT_MAX_ITEMS];
        size_t top_extra_count = 0;
        collect_c_files(project, main_dir, top_extras, &top_extra_count, 0, ".c");
        for (size_t e = 0; e < top_extra_count; ++e) {
            const char *top_base = strrchr(top_extras[e].rel, '/');
            top_base = top_base ? top_base + 1 : top_extras[e].rel;
            char prefix[160];
            snprintf(prefix, sizeof(prefix), "%s_", app);
            if (strncmp(top_base, prefix, strlen(prefix))) continue;
            if (ends_with(top_extras[e].rel, "_main.c") || ends_with(top_extras[e].rel, "_kmain.c")) continue;
            total_steps++;
        }
        char app_subdir[1024];
        AppFile extras[BDT_MAX_ITEMS];
        size_t extra_count = 0;
        bdt_path_join(app_subdir, sizeof(app_subdir), main_dir, app);
        collect_c_files(project, app_subdir, extras, &extra_count, 0, ".c");
        for (size_t e = 0; e < extra_count; ++e) {
            if (ends_with(extras[e].rel, "_main.c") || ends_with(extras[e].rel, "_kmain.c")) continue;
            total_steps++;
        }
    }
    AppFile count_kmains[BDT_MAX_ITEMS];
    size_t count_kmain_count = 0;
    collect_c_files(project, main_dir, count_kmains, &count_kmain_count, 0, "_kmain.c");
    total_steps += count_kmain_count * 2;
    if (total_steps == 0) total_steps = 1;
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
            progress_step(&progress, total_steps, files[i].rel);
            if (compile_one(project, target, NULL, files[i].path, files[i].rel, obj_root, obj, sizeof(obj)) != 0) return -1;
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
        progress_step(&progress, total_steps, rel);
        if (compile_one(project, target, NULL, src, rel, obj_root, obj, sizeof(obj)) != 0) return -1;
        append_object_unique(runtime_objs, sizeof(runtime_objs), obj);
    }

    AppFile mains[BDT_MAX_ITEMS];
    size_t main_count = 0;
    collect_c_files(project, main_dir, mains, &main_count, 0, "_main.c");
    for (size_t i = 0; i < main_count; ++i) {
        char app[128], obj[1024], objects[BDT_MAX_TEXT * 8], out[1024];
        const char *base = strrchr(mains[i].rel, '/');
        base = base ? base + 1 : mains[i].rel;
        snprintf(app, sizeof(app), "%s", base);
        char *suffix = strstr(app, "_main.c");
        if (suffix) *suffix = 0;
        if (list_contains(target->skip_apps, app)) continue;
        const BdtAppRule *rule = find_app_rule(target, app);
        progress_step(&progress, total_steps, mains[i].rel);
        if (compile_one(project, target, rule ? rule->cflags : NULL, mains[i].path, mains[i].rel, obj_root, obj, sizeof(obj)) != 0) return -1;
        snprintf(objects, sizeof(objects), "%s \"%s\"", shared_objs, obj);
        if (strcmp(app, "shell")) strncat(objects, runtime_objs, sizeof(objects) - strlen(objects) - 1);
        if (compile_rule_sources(project, target, rule, obj_root, objects, sizeof(objects), &progress, total_steps) != 0) return -1;

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
            if (ends_with(top_extras[e].rel, "_main.c") || ends_with(top_extras[e].rel, "_kmain.c")) continue;
            char extra_obj[1024];
            progress_step(&progress, total_steps, top_extras[e].rel);
            if (compile_one(project, target, rule ? rule->cflags : NULL, top_extras[e].path, top_extras[e].rel, obj_root, extra_obj, sizeof(extra_obj)) != 0) return -1;
            append_object_unique(objects, sizeof(objects), extra_obj);
        }

        bdt_path_join(app_subdir, sizeof(app_subdir), main_dir, app);
        collect_c_files(project, app_subdir, extras, &extra_count, 0, ".c");
        for (size_t e = 0; e < extra_count; ++e) {
            if (ends_with(extras[e].rel, "_main.c") || ends_with(extras[e].rel, "_kmain.c")) continue;
            char extra_obj[1024];
            progress_step(&progress, total_steps, extras[e].rel);
            if (compile_one(project, target, rule ? rule->cflags : NULL, extras[e].path, extras[e].rel, obj_root, extra_obj, sizeof(extra_obj)) != 0) return -1;
            append_object_unique(objects, sizeof(objects), extra_obj);
        }

        const char *dest = list_contains(target->uwm_apps, app) && uwm_out[0] ? uwm_out : out_dir;
        char app_elf[256];
        snprintf(app_elf, sizeof(app_elf), "%s.elf", app);
        bdt_path_join(out, sizeof(out), dest, app_elf);
        char link_label[256];
        snprintf(link_label, sizeof(link_label), "link %s.elf", app);
        progress_step(&progress, total_steps, link_label);
        if (link_app(project, target, out, linker, objects) != 0) return -1;
    }

    AppFile kmains[BDT_MAX_ITEMS];
    size_t kmain_count = 0;
    collect_c_files(project, main_dir, kmains, &kmain_count, 0, "_kmain.c");
    for (size_t i = 0; i < kmain_count; ++i) {
        char app[128], obj[1024], out[1024], objects[BDT_MAX_TEXT * 2];
        const char *base = strrchr(kmains[i].rel, '/');
        base = base ? base + 1 : kmains[i].rel;
        snprintf(app, sizeof(app), "%s", base);
        char *suffix = strstr(app, "_kmain.c");
        if (suffix) *suffix = 0;
        progress_step(&progress, total_steps, kmains[i].rel);
        if (compile_one(project, target, NULL, kmains[i].path, kmains[i].rel, obj_root, obj, sizeof(obj)) != 0) return -1;
        char app_elf[256];
        snprintf(app_elf, sizeof(app_elf), "%s.elf", app);
        bdt_path_join(out, sizeof(out), sys_out, app_elf);
        snprintf(objects, sizeof(objects), "\"%s\"", obj);
        char link_label[256];
        snprintf(link_label, sizeof(link_label), "link %s.elf", app);
        progress_step(&progress, total_steps, link_label);
        if (link_app(project, target, out, sys_linker[0] ? sys_linker : linker, objects) != 0) return -1;
    }
    return 0;
}
