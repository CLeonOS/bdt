#include "bdt.h"

#include <stdlib.h>
#include <string.h>

static const char *target_type(const BdtTarget *target) {
    return target->type[0] ? target->type : "command";
}

static int is_abs_path(const char *path) {
    return path && (path[0] == '/' || path[0] == '\\' || strchr(path, ':'));
}

static void normalize_path(const char *in, char *out, size_t out_size) {
    snprintf(out, out_size, "%s", in ? in : "");
    for (char *p = out; *p; ++p) {
        if (*p == '\\') *p = '/';
    }
}

static void source_abs(BdtProject *project, const char *raw, char *src, size_t src_size) {
    char expanded[1024];
    bdt_expand_vars(project, raw, expanded, sizeof(expanded));
    if (is_abs_path(expanded)) snprintf(src, src_size, "%s", expanded);
    else bdt_path_join(src, src_size, project->root, expanded);
}

static void object_for_compile(BdtProject *project, BdtTarget *target, const char *raw, const char *out_dir, char *obj, size_t obj_size) {
    (void)project;
    char rel[1024];
    snprintf(rel, sizeof(rel), "%s", raw);
    for (char *p = rel; *p; ++p) {
        if (*p == '\\') *p = '/';
    }
    char *dot = strrchr(rel, '.');
    if (dot) strcpy(dot, ".o");
    bdt_path_join(obj, obj_size, out_dir, rel);
    (void)target;
}

static int compile_target_status(BdtProject *project, BdtTarget *target, size_t *compile_count) {
    char srcs[BDT_MAX_ITEMS][512], out_dir[1024];
    int count = bdt_split_list(target->sources, srcs, BDT_MAX_ITEMS);
    bdt_expand_vars(project, target->output, out_dir, sizeof(out_dir));
    for (int i = 0; i < count; ++i) {
        char src[1024], obj[1024], dep[1060], flags[BDT_MAX_TEXT], tool[256], reason[256];
        source_abs(project, srcs[i], src, sizeof(src));
        object_for_compile(project, target, srcs[i], out_dir, obj, sizeof(obj));
        snprintf(dep, sizeof(dep), "%s.d", obj);
        bdt_expand_vars(project, target->tool[0] ? target->tool : "{CC}", tool, sizeof(tool));
        const char *ext = strrchr(src, '.');
        const char *base_flags = target->flags;
        if (ext && !strcmp(ext, ".cpp") && target->cxxflags[0]) base_flags = target->cxxflags;
        else if (ext && !strcmp(ext, ".S") && target->asflags[0]) base_flags = target->asflags;
        else if (target->cflags[0]) base_flags = target->cflags;
        bdt_expand_list_vars(project, base_flags, flags, sizeof(flags));
        if (!target->cache || !bdt_compile_cache_quick_fresh(project, target, src, obj, dep, tool, flags, reason, sizeof(reason))) {
            (*compile_count)++;
        }
    }
    return 0;
}

static int link_target_status(BdtProject *project, BdtTarget *target, size_t *link_count) {
    char out[1024], flags[BDT_MAX_TEXT], script[1024], tool[256], reason[256];
    bdt_expand_vars(project, target->output, out, sizeof(out));
    bdt_expand_list_vars(project, target->ldflags[0] ? target->ldflags : target->flags, flags, sizeof(flags));
    bdt_expand_vars(project, target->linker_script, script, sizeof(script));
    bdt_expand_vars(project, target->tool[0] ? target->tool : "{LD}", tool, sizeof(tool));
    if (!target->cache || !bdt_link_cache_quick_fresh(project, target, out, target->objects, tool, flags, script, reason, sizeof(reason))) {
        (*link_count)++;
    }
    return 0;
}

static int status_one(BdtProject *project, BdtTarget *target) {
    const char *type = target_type(target);
    size_t compile_count = 0, link_count = 0, app_relink_count = 0;
    if (!strcmp(type, "compile")) compile_target_status(project, target, &compile_count);
    else if (!strcmp(type, "link")) link_target_status(project, target, &link_count);
    else if (!strcmp(type, "c-apps")) bdt_c_apps_status(project, target, &compile_count, &link_count, &app_relink_count);
    else if (!target->cache || target->always) {
        printf("%s: will run (%s)\n", target->name, type);
        return 0;
    }

    if (compile_count || link_count || app_relink_count) {
        printf("%s:", target->name);
        if (compile_count) printf(" %zu compile", compile_count);
        if (link_count) printf(" %zu link", link_count);
        if (app_relink_count) printf(" %zu apps relink", app_relink_count);
        putchar('\n');
    } else {
        printf("%s: up to date\n", target->name);
    }
    return 0;
}

static int visit_status(BdtProject *project, BdtTarget *target) {
    if (target->visited) return 0;
    target->visited = 1;
    char deps[BDT_MAX_ITEMS][512];
    int dep_count = bdt_split_list(target->deps, deps, BDT_MAX_ITEMS);
    for (int i = 0; i < dep_count; ++i) {
        BdtTarget *dep = bdt_find_target(project, deps[i]);
        if (dep) visit_status(project, dep);
    }
    return status_one(project, target);
}

int bdt_status_project(BdtProject *project, const char *name) {
    for (size_t i = 0; i < project->target_count; ++i) project->targets[i].visited = 0;
    if (name && name[0]) {
        BdtTarget *target = bdt_find_target(project, name);
        if (!target) {
            bdt_log(BDT_LOG_ERROR, "unknown target '%s'", name);
            return -1;
        }
        return visit_status(project, target);
    }
    for (size_t i = 0; i < project->target_count; ++i) status_one(project, &project->targets[i]);
    return 0;
}

static int text_mentions_path(const char *text, const char *query) {
    char a[BDT_MAX_TEXT], b[1024];
    normalize_path(text, a, sizeof(a));
    normalize_path(query, b, sizeof(b));
    return strstr(a, b) != NULL;
}

static int why_target(BdtProject *project, const char *query) {
    BdtTarget *target = bdt_find_target(project, query);
    if (!target) return 0;
    printf("target: %s\n", target->name);
    printf("  type: %s\n", target_type(target));
    if (target->deps[0]) printf("  depends on: %s\n", target->deps);
    printf("  used by:");
    int users = 0;
    for (size_t i = 0; i < project->target_count; ++i) {
        char deps[BDT_MAX_ITEMS][512];
        int dep_count = bdt_split_list(project->targets[i].deps, deps, BDT_MAX_ITEMS);
        for (int d = 0; d < dep_count; ++d) {
            if (!strcmp(deps[d], query)) {
                printf(" %s", project->targets[i].name);
                users++;
            }
        }
    }
    printf("%s\n", users ? "" : " none");
    return 1;
}

static int why_compile(BdtProject *project, BdtTarget *target, const char *query) {
    char srcs[BDT_MAX_ITEMS][512], out_dir[1024];
    int found = 0;
    int count = bdt_split_list(target->sources, srcs, BDT_MAX_ITEMS);
    bdt_expand_vars(project, target->output, out_dir, sizeof(out_dir));
    for (int i = 0; i < count; ++i) {
        char src[1024], obj[1024];
        source_abs(project, srcs[i], src, sizeof(src));
        object_for_compile(project, target, srcs[i], out_dir, obj, sizeof(obj));
        if (text_mentions_path(src, query) || text_mentions_path(srcs[i], query) || text_mentions_path(obj, query)) {
            printf("file: %s\n", query);
            printf("  compiled by target: %s\n", target->name);
            printf("  source: %s\n", src);
            printf("  object: %s\n", obj);
            found = 1;
        }
    }
    return found;
}

static int why_c_apps(BdtProject *project, BdtTarget *target, const char *query) {
    (void)project;
    int found = 0;
    if (text_mentions_path(target->main_dir, query)) {
        printf("file/path: %s\n  scanned by c-apps target: %s\n  main_dir: %s\n", query, target->name, target->main_dir);
        found = 1;
    }
    if (text_mentions_path(target->common_dirs, query)) {
        printf("file/path: %s\n  compiled as common user app code by: %s\n", query, target->name);
        found = 1;
    }
    if (text_mentions_path(target->runtime_sources, query)) {
        printf("file/path: %s\n  compiled as runtime source by: %s\n  linked into apps unless excluded\n", query, target->name);
        found = 1;
    }
    for (size_t i = 0; i < target->app_rule_count; ++i) {
        BdtAppRule *rule = &target->app_rules[i];
        if (text_mentions_path(rule->sources, query) || text_mentions_path(rule->source_dirs, query)) {
            printf("file/path: %s\n  used by app: %s\n  target: %s\n", query, rule->name, target->name);
            found = 1;
        }
    }
    return found;
}

int bdt_why_query(BdtProject *project, const char *query) {
    if (!query || !query[0]) {
        bdt_log(BDT_LOG_ERROR, "usage: bdt why <file|target>");
        return -1;
    }
    int found = why_target(project, query);
    for (size_t i = 0; i < project->target_count; ++i) {
        const char *type = target_type(&project->targets[i]);
        if (!strcmp(type, "compile")) found += why_compile(project, &project->targets[i], query);
        else if (!strcmp(type, "c-apps")) found += why_c_apps(project, &project->targets[i], query);
        else if (text_mentions_path(project->targets[i].inputs, query) || text_mentions_path(project->targets[i].outputs, query) ||
                 text_mentions_path(project->targets[i].output, query) || text_mentions_path(project->targets[i].objects, query)) {
            printf("file/path: %s\n  referenced by target: %s (%s)\n", query, project->targets[i].name, type);
            found++;
        }
    }
    if (!found) {
        printf("no build-system owner found for: %s\n", query);
        return 1;
    }
    return 0;
}
