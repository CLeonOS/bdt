#include "bdt.h"

#include <string.h>

static const char *target_type(const BdtTarget *target) {
    return target->type[0] ? target->type : "command";
}

static void print_expanded(BdtProject *project, const char *label, const char *value) {
    if (!value || !value[0]) return;
    char expanded[BDT_MAX_TEXT * 2];
    bdt_expand_vars(project, value, expanded, sizeof(expanded));
    printf("  %s: %s\n", label, expanded);
}

static int explain_compile(BdtProject *project, BdtTarget *target) {
    char srcs[BDT_MAX_ITEMS][512], out_dir[1024];
    int count = bdt_split_list(target->sources, srcs, BDT_MAX_ITEMS);
    bdt_expand_vars(project, target->output, out_dir, sizeof(out_dir));
    printf("  sources: %d\n", count);
    for (int i = 0; i < count; ++i) {
        char src[1024], obj[1024], dep[1060], rel[1024], flags[BDT_MAX_TEXT], tool[256], reason[256];
        char expanded_src[1024];
        bdt_expand_vars(project, srcs[i], expanded_src, sizeof(expanded_src));
        if (strchr(expanded_src, ':') || expanded_src[0] == '/' || expanded_src[0] == '\\') snprintf(src, sizeof(src), "%s", expanded_src);
        else bdt_path_join(src, sizeof(src), project->root, expanded_src);
        strncpy(rel, srcs[i], sizeof(rel) - 1);
        rel[sizeof(rel) - 1] = 0;
        for (char *p = rel; *p; ++p) if (*p == '\\') *p = '/';
        char *dot = strrchr(rel, '.');
        if (dot) strcpy(dot, ".o");
        bdt_path_join(obj, sizeof(obj), out_dir, rel);
        snprintf(dep, sizeof(dep), "%s.d", obj);
        bdt_expand_vars(project, target->tool[0] ? target->tool : "{CC}", tool, sizeof(tool));
        const char *ext = strrchr(src, '.');
        const char *base_flags = target->flags;
        if (ext && !strcmp(ext, ".cpp") && target->cxxflags[0]) base_flags = target->cxxflags;
        else if (ext && !strcmp(ext, ".S") && target->asflags[0]) base_flags = target->asflags;
        else if (target->cflags[0]) base_flags = target->cflags;
        bdt_expand_list_vars(project, base_flags, flags, sizeof(flags));
        int fresh = bdt_compile_cache_fresh(project, target, src, obj, dep, tool, flags, reason, sizeof(reason));
        printf("    %s -> %s : %s", srcs[i], obj, fresh ? "fresh" : "rebuild");
        if (!fresh) printf(" (%s)", reason);
        putchar('\n');
    }
    return 0;
}

int bdt_explain_target(BdtProject *project, const char *name) {
    BdtTarget *target = bdt_find_target(project, name);
    if (!target) {
        bdt_log(BDT_LOG_ERROR, "unknown target '%s'", name);
        return -1;
    }
    printf("target: %s\n", target->name);
    printf("  type: %s\n", target_type(target));
    printf("  cache: %s\n", target->cache ? "enabled" : "disabled");
    printf("  always: %s\n", target->always ? "yes" : "no");
    print_expanded(project, "deps", target->deps);
    print_expanded(project, "tool", target->tool);
    print_expanded(project, "command", target->command);
    print_expanded(project, "steps", target->steps);
    print_expanded(project, "inputs", target->inputs);
    print_expanded(project, "sources", target->sources);
    print_expanded(project, "output", target->output);
    print_expanded(project, "objects", target->objects);
    print_expanded(project, "linker_script", target->linker_script);
    if (!strcmp(target_type(target), "compile")) explain_compile(project, target);
    return 0;
}

int bdt_clean_named_target(BdtProject *project, const char *name) {
    BdtTarget *target = bdt_find_target(project, name);
    if (!target) {
        bdt_log(BDT_LOG_ERROR, "unknown target '%s'", name);
        return -1;
    }
    char cache_root[1024], cache_dir[1024], target_cache[1024];
    bdt_path_join(cache_root, sizeof(cache_root), project->root, project->build_dir);
    bdt_path_join(cache_dir, sizeof(cache_dir), cache_root, "cache");
    bdt_path_join(target_cache, sizeof(target_cache), cache_dir, target->name);
    bdt_remove_path(target_cache);

    char hash_file[1024], hash_name[160];
    snprintf(hash_name, sizeof(hash_name), "%s.hash", target->name);
    bdt_path_join(hash_file, sizeof(hash_file), cache_root, hash_name);
    bdt_remove_path(hash_file);

    const char *type = target_type(target);
    if (!strcmp(type, "compile")) {
        char out[1024];
        bdt_expand_vars(project, target->output, out, sizeof(out));
        if (out[0]) bdt_remove_path(out);
    } else if (!strcmp(type, "link") || !strcmp(type, "rust-staticlib") || !strcmp(type, "tar") || !strcmp(type, "truncate")) {
        char out[1024];
        bdt_expand_vars(project, target->output, out, sizeof(out));
        if (out[0]) bdt_remove_path(out);
    } else if (!strcmp(type, "c-apps")) {
        char out[1024], obj[1024];
        bdt_expand_vars(project, target->output, out, sizeof(out));
        bdt_expand_vars(project, target->objects, obj, sizeof(obj));
        if (out[0]) bdt_remove_path(out);
        for (size_t i = 0; i < target->output_group_count; ++i) {
            char group_out[1024];
            bdt_expand_vars(project, target->output_groups[i].output, group_out, sizeof(group_out));
            if (group_out[0]) bdt_remove_path(group_out);
        }
        if (obj[0]) bdt_remove_path(obj);
    } else {
        char outputs[BDT_MAX_ITEMS][512];
        int count = bdt_split_list(target->outputs[0] ? target->outputs : target->output, outputs, BDT_MAX_ITEMS);
        for (int i = 0; i < count; ++i) {
            char path[1024];
            bdt_expand_vars(project, outputs[i], path, sizeof(path));
            bdt_remove_path(path);
        }
    }
    bdt_log(BDT_LOG_INFO, "cleaned target: %s", target->name);
    return 0;
}

static int check_tool(BdtProject *project, const char *name) {
    char value[256];
    bdt_expand_vars(project, name, value, sizeof(value));
    if (!value[0]) {
        strncpy(value, name, sizeof(value) - 1);
        value[sizeof(value) - 1] = 0;
    }
    if (bdt_command_exists(value)) {
        printf("  [ok] tool %s\n", value);
        return 0;
    }
    printf("  [missing] tool %s\n", value);
    return 1;
}

int bdt_doctor(BdtProject *project) {
    int issues = 0;
    printf("bdt doctor: %s\n", project->name);
    printf("root: %s\n", project->root);
    printf("build_dir: %s\n", project->build_dir);

    char build_dir[1024];
    bdt_path_join(build_dir, sizeof(build_dir), project->root, project->build_dir);
    if (bdt_mkdirs(build_dir) == 0) printf("  [ok] build directory writable\n");
    else {
        printf("  [missing] build directory not writable: %s\n", build_dir);
        issues++;
    }

    char tools[BDT_MAX_ITEMS][512];
    int tool_count = bdt_split_list(project->doctor_tools, tools, BDT_MAX_ITEMS);
    for (int i = 0; i < tool_count; ++i) issues += check_tool(project, tools[i]);

    for (size_t i = 0; i < project->subproject_count; ++i) {
        char path[1024];
        bdt_path_join(path, sizeof(path), project->root, project->subprojects[i]);
        if (bdt_dir_exists(path)) printf("  [ok] subproject %s\n", project->subprojects[i]);
        else {
            printf("  [missing] subproject %s\n", project->subprojects[i]);
            issues++;
        }
    }

    for (size_t i = 0; i < project->target_count; ++i) {
        BdtTarget *t = &project->targets[i];
        const char *type = target_type(t);
        if (!strcmp(type, "compile") && (!t->sources[0] || !t->output[0])) {
            printf("  [warn] target %s missing sources/output\n", t->name);
            issues++;
        }
        if (!strcmp(type, "link") && !t->output[0]) {
            printf("  [warn] target %s missing output\n", t->name);
            issues++;
        }
    }
    printf("doctor result: %s\n", issues ? "issues found" : "ok");
    return issues ? -1 : 0;
}
