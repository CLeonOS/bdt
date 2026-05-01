#include "bdt.h"

#include <stdlib.h>
#include <string.h>

static void add_project_var(BdtProject *project, const char *key, const char *value) {
    if (!key || !value || project->var_count >= BDT_MAX_ITEMS) return;
    for (size_t i = 0; i < project->var_count; ++i) {
        if (!strcmp(project->vars[i].key, key)) {
            snprintf(project->vars[i].value, sizeof(project->vars[i].value), "%s", value);
            return;
        }
    }
    snprintf(project->vars[project->var_count].key, sizeof(project->vars[project->var_count].key), "%s", key);
    snprintf(project->vars[project->var_count].value, sizeof(project->vars[project->var_count].value), "%s", value);
    project->var_count++;
}

static int parse_bool(const char *value, int fallback) {
    if (!value || !*value) return fallback;
    if (!strcmp(value, "true") || !strcmp(value, "1") || !strcmp(value, "yes") || !strcmp(value, "on")) return 1;
    if (!strcmp(value, "false") || !strcmp(value, "0") || !strcmp(value, "no") || !strcmp(value, "off")) return 0;
    return fallback;
}

static BdtOutputGroup *find_or_add_output_group(BdtTarget *target, const char *name) {
    if (!name || !*name) return NULL;
    for (size_t i = 0; i < target->output_group_count; ++i) {
        if (!strcmp(target->output_groups[i].name, name)) return &target->output_groups[i];
    }
    if (target->output_group_count >= BDT_MAX_ITEMS) return NULL;
    BdtOutputGroup *group = &target->output_groups[target->output_group_count++];
    memset(group, 0, sizeof(*group));
    snprintf(group->name, sizeof(group->name), "%s", name);
    return group;
}

static BdtAppRule *find_or_add_app_rule(BdtTarget *target, const char *app_name) {
    for (size_t r = 0; r < target->app_rule_count; ++r) {
        if (!strcmp(target->app_rules[r].name, app_name)) return &target->app_rules[r];
    }
    if (target->app_rule_count >= BDT_MAX_ITEMS) return NULL;
    BdtAppRule *rule = &target->app_rules[target->app_rule_count++];
    memset(rule, 0, sizeof(*rule));
    snprintf(rule->name, sizeof(rule->name), "%s", app_name);
    rule->include_runtime = 1;
    return rule;
}

BdtPlugin *bdt_find_plugin(BdtProject *project, const char *name) {
    if (!name || !*name) return NULL;
    for (size_t i = 0; i < project->plugin_count; ++i) {
        if (!strcmp(project->plugins[i].name, name)) return &project->plugins[i];
    }
    return NULL;
}

static void load_plugins(BdtProject *project, const BdtConfig *config) {
    for (size_t i = 0; i < config->section_count; ++i) {
        const char *sname = config->sections[i].name;
        if (strncmp(sname, "plugin.", 7)) continue;
        if (project->plugin_count >= BDT_MAX_ITEMS) break;
        BdtPlugin *plugin = &project->plugins[project->plugin_count++];
        memset(plugin, 0, sizeof(*plugin));
        snprintf(plugin->name, sizeof(plugin->name), "%s", sname + 7);
        for (size_t j = 0; j < config->sections[i].item_count; ++j) {
            const BdtPair *p = &config->sections[i].items[j];
            if (!strcmp(p->key, "runner")) snprintf(plugin->runner, sizeof(plugin->runner), "%s", p->value);
            else if (!strcmp(p->key, "path")) snprintf(plugin->path, sizeof(plugin->path), "%s", p->value);
            else if (!strcmp(p->key, "command")) snprintf(plugin->command, sizeof(plugin->command), "%s", p->value);
        }
    }
}

static void load_targets(BdtProject *project, const BdtConfig *config) {
    for (size_t i = 0; i < config->section_count; ++i) {
        const char *sname = config->sections[i].name;
        if (strncmp(sname, "target.", 7)) continue;
        if (project->target_count >= BDT_MAX_ITEMS) break;
        BdtTarget *t = &project->targets[project->target_count++];
        memset(t, 0, sizeof(*t));
        snprintf(t->name, sizeof(t->name), "%s", sname + 7);
        t->cache = 1;
        snprintf(t->entry_suffix, sizeof(t->entry_suffix), "_main.c");
        t->secondary_entry_suffix[0] = 0;
        snprintf(t->secondary_output_group, sizeof(t->secondary_output_group), "secondary");
        BdtOutputGroup *default_group = find_or_add_output_group(t, "default");
        if (default_group) snprintf(default_group->linker_script, sizeof(default_group->linker_script), "%s", t->linker_script);
        for (size_t j = 0; j < config->sections[i].item_count; ++j) {
            const BdtPair *p = &config->sections[i].items[j];
            if (!strcmp(p->key, "type")) snprintf(t->type, sizeof(t->type), "%s", p->value);
            else if (!strncmp(p->key, "var.", 4)) add_project_var(project, p->key + 4, p->value);
            else if (!strcmp(p->key, "deps")) snprintf(t->deps, sizeof(t->deps), "%s", p->value);
            else if (!strcmp(p->key, "command")) snprintf(t->command, sizeof(t->command), "%s", p->value);
            else if (!strcmp(p->key, "steps")) snprintf(t->steps, sizeof(t->steps), "%s", p->value);
            else if (!strcmp(p->key, "inputs")) snprintf(t->inputs, sizeof(t->inputs), "%s", p->value);
            else if (!strcmp(p->key, "outputs")) snprintf(t->outputs, sizeof(t->outputs), "%s", p->value);
            else if (!strcmp(p->key, "sources")) snprintf(t->sources, sizeof(t->sources), "%s", p->value);
            else if (!strcmp(p->key, "source_dirs")) snprintf(t->source_dirs, sizeof(t->source_dirs), "%s", p->value);
            else if (!strcmp(p->key, "objects")) snprintf(t->objects, sizeof(t->objects), "%s", p->value);
            else if (!strcmp(p->key, "output")) snprintf(t->output, sizeof(t->output), "%s", p->value);
            else if (!strcmp(p->key, "flags")) snprintf(t->flags, sizeof(t->flags), "%s", p->value);
            else if (!strcmp(p->key, "cflags")) snprintf(t->cflags, sizeof(t->cflags), "%s", p->value);
            else if (!strcmp(p->key, "cxxflags")) snprintf(t->cxxflags, sizeof(t->cxxflags), "%s", p->value);
            else if (!strcmp(p->key, "asflags")) snprintf(t->asflags, sizeof(t->asflags), "%s", p->value);
            else if (!strcmp(p->key, "ldflags")) snprintf(t->ldflags, sizeof(t->ldflags), "%s", p->value);
            else if (!strcmp(p->key, "includes")) snprintf(t->includes, sizeof(t->includes), "%s", p->value);
            else if (!strcmp(p->key, "linker_script")) snprintf(t->linker_script, sizeof(t->linker_script), "%s", p->value);
            else if (!strcmp(p->key, "tool")) snprintf(t->tool, sizeof(t->tool), "%s", p->value);
            else if (!strcmp(p->key, "plugin")) snprintf(t->plugin, sizeof(t->plugin), "%s", p->value);
            else if (!strcmp(p->key, "common_dirs")) snprintf(t->common_dirs, sizeof(t->common_dirs), "%s", p->value);
            else if (!strcmp(p->key, "main_dir")) snprintf(t->main_dir, sizeof(t->main_dir), "%s", p->value);
            else if (!strcmp(p->key, "runtime_sources")) snprintf(t->runtime_sources, sizeof(t->runtime_sources), "%s", p->value);
            else if (!strcmp(p->key, "entry_suffix")) snprintf(t->entry_suffix, sizeof(t->entry_suffix), "%s", p->value);
            else if (!strcmp(p->key, "secondary_entry_suffix")) snprintf(t->secondary_entry_suffix, sizeof(t->secondary_entry_suffix), "%s", p->value);
            else if (!strcmp(p->key, "secondary_output_group")) snprintf(t->secondary_output_group, sizeof(t->secondary_output_group), "%s", p->value);
            else if (!strcmp(p->key, "runtime_exclude_apps")) snprintf(t->runtime_exclude_apps, sizeof(t->runtime_exclude_apps), "%s", p->value);
            else if (!strcmp(p->key, "skip_apps")) snprintf(t->skip_apps, sizeof(t->skip_apps), "%s", p->value);
            else if (!strncmp(p->key, "output_group.", 13)) {
                const char *name = p->key + 13;
                const char *field = strchr(name, '.');
                if (field && field > name) {
                    char group_name[96];
                    size_t len = (size_t)(field - name);
                    if (len >= sizeof(group_name)) len = sizeof(group_name) - 1;
                    memcpy(group_name, name, len);
                    group_name[len] = 0;
                    field++;
                    BdtOutputGroup *group = find_or_add_output_group(t, group_name);
                    if (group) {
                        if (!strcmp(field, "output")) snprintf(group->output, sizeof(group->output), "%s", p->value);
                        else if (!strcmp(field, "linker_script")) snprintf(group->linker_script, sizeof(group->linker_script), "%s", p->value);
                        else if (!strcmp(field, "apps")) snprintf(group->apps, sizeof(group->apps), "%s", p->value);
                    }
                }
            }
            else if (!strncmp(p->key, "app.", 4)) {
                const char *name = p->key + 4;
                const char *field = strchr(name, '.');
                if (field && field > name) {
                    char app_name[96];
                    size_t len = (size_t)(field - name);
                    if (len >= sizeof(app_name)) len = sizeof(app_name) - 1;
                    memcpy(app_name, name, len);
                    app_name[len] = 0;
                    field++;

                    BdtAppRule *rule = find_or_add_app_rule(t, app_name);
                    if (rule) {
                        if (!strcmp(field, "sources")) snprintf(rule->sources, sizeof(rule->sources), "%s", p->value);
                        else if (!strcmp(field, "source_dirs")) snprintf(rule->source_dirs, sizeof(rule->source_dirs), "%s", p->value);
                        else if (!strcmp(field, "exclude_sources")) snprintf(rule->exclude_sources, sizeof(rule->exclude_sources), "%s", p->value);
                        else if (!strcmp(field, "cflags")) snprintf(rule->cflags, sizeof(rule->cflags), "%s", p->value);
                        else if (!strcmp(field, "output_group")) snprintf(rule->output_group, sizeof(rule->output_group), "%s", p->value);
                        else if (!strcmp(field, "include_runtime")) rule->include_runtime = parse_bool(p->value, 1);
                    }
                }
            }
            else if (!strcmp(p->key, "always")) t->always = parse_bool(p->value, 0);
            else if (!strcmp(p->key, "cache")) t->cache = parse_bool(p->value, 0);
            else if (!strcmp(p->key, "parallel")) t->parallel = parse_bool(p->value, 0);
        }
        default_group = find_or_add_output_group(t, "default");
        if (default_group) {
            if (!default_group->output[0]) snprintf(default_group->output, sizeof(default_group->output), "%s", t->output);
            if (!default_group->linker_script[0]) snprintf(default_group->linker_script, sizeof(default_group->linker_script), "%s", t->linker_script);
        }
    }
}

int bdt_load_project(const char *root, const char *project_file, BdtProject *project) {
    memset(project, 0, sizeof(*project));
    snprintf(project->root, sizeof(project->root), "%s", root);
    BdtConfig *config = (BdtConfig *)calloc(1, sizeof(BdtConfig));
    if (!config) return -1;
    char path[1024];
    bdt_path_join(path, sizeof(path), root, project_file);
    if (bdt_read_config(path, config) != 0) {
        bdt_log(BDT_LOG_ERROR, "cannot read %s", path);
        free(config);
        return -1;
    }

    const char *name = bdt_config_get(config, "project", "name");
    const char *def = bdt_config_get(config, "project", "default");
    const char *build_dir = bdt_config_get(config, "project", "build_dir");
    const char *lang = bdt_config_get(config, "project", "language");
    const char *doctor_tools = bdt_config_get(config, "project", "doctor_tools");
    snprintf(project->name, sizeof(project->name), "%s", name ? name : "project");
    snprintf(project->default_target, sizeof(project->default_target), "%s", def ? def : "all");
    snprintf(project->build_dir, sizeof(project->build_dir), "%s", build_dir ? build_dir : ".bdt");
    snprintf(project->language, sizeof(project->language), "%s", lang ? lang : "en");
    snprintf(project->doctor_tools, sizeof(project->doctor_tools), "%s", doctor_tools ? doctor_tools : "");
    project->lang = bdt_lang_from_text(project->language);

    add_project_var(project, "project", project->name);
    add_project_var(project, "root", project->root);
    add_project_var(project, "build_dir", project->build_dir);
    bdt_load_toolchain(project, config);

    const char *subs = bdt_config_get(config, "subprojects", "paths");
    project->subproject_count = (size_t)bdt_split_list(subs, project->subprojects, BDT_MAX_ITEMS);
    const char *cache_path = bdt_config_get(config, "cache", "path");
    const char *cache_archive = bdt_config_get(config, "cache", "archive");
    const char *cache_pull = bdt_config_get(config, "cache", "pull_command");
    const char *cache_push = bdt_config_get(config, "cache", "push_command");
    snprintf(project->cache_config.path, sizeof(project->cache_config.path), "%s", cache_path ? cache_path : "{root}/{build_dir}/cache");
    snprintf(project->cache_config.archive, sizeof(project->cache_config.archive), "%s", cache_archive ? cache_archive : "{root}/{build_dir}/bdt-cache.tar");
    snprintf(project->cache_config.pull_command, sizeof(project->cache_config.pull_command), "%s", cache_pull ? cache_pull : "");
    snprintf(project->cache_config.push_command, sizeof(project->cache_config.push_command), "%s", cache_push ? cache_push : "");
    load_plugins(project, config);
    load_targets(project, config);
    free(config);
    return 0;
}

void bdt_print_targets(const BdtProject *project) {
    printf("%s targets:\n", project->name);
    for (size_t i = 0; i < project->target_count; ++i) {
        printf("  %s", project->targets[i].name);
        if (project->targets[i].deps[0]) printf("  deps=[%s]", project->targets[i].deps);
        putchar('\n');
    }
}

BdtTarget *bdt_find_target(BdtProject *project, const char *name) {
    for (size_t i = 0; i < project->target_count; ++i) {
        if (!strcmp(project->targets[i].name, name)) return &project->targets[i];
    }
    return NULL;
}

int bdt_expand_vars(const BdtProject *project, const char *input, char *out, size_t out_size) {
    size_t oi = 0;
    for (size_t i = 0; input && input[i] && oi + 1 < out_size; ++i) {
        if (input[i] == '{') {
            const char *end = strchr(input + i + 1, '}');
            if (end) {
                char key[96];
                size_t len = (size_t)(end - (input + i + 1));
                if (len >= sizeof(key)) len = sizeof(key) - 1;
                memcpy(key, input + i + 1, len);
                key[len] = 0;
                const char *value = getenv(key);
                for (size_t v = 0; !value && v < project->var_count; ++v) {
                    if (!strcmp(project->vars[v].key, key)) value = project->vars[v].value;
                }
                if (value) {
                    size_t vl = strlen(value);
                    if (oi + vl >= out_size) vl = out_size - oi - 1;
                    memcpy(out + oi, value, vl);
                    oi += vl;
                }
                i = (size_t)(end - input);
                continue;
            }
        }
        out[oi++] = input[i];
    }
    out[oi] = 0;
    return 0;
}

int bdt_expand_list_vars(const BdtProject *project, const char *input, char *out, size_t out_size) {
    char tmp[BDT_MAX_TEXT];
    char next[BDT_MAX_TEXT];
    bdt_expand_vars(project, input, tmp, sizeof(tmp));
    for (int pass = 0; pass < 4 && strchr(tmp, '{') != NULL; ++pass) {
        bdt_expand_vars(project, tmp, next, sizeof(next));
        if (!strcmp(tmp, next)) break;
        snprintf(tmp, sizeof(tmp), "%s", next);
    }
    size_t oi = 0;
    for (size_t i = 0; tmp[i] && oi + 1 < out_size; ++i) {
        out[oi++] = (tmp[i] == ',') ? ' ' : tmp[i];
    }
    out[oi] = 0;
    return 0;
}
