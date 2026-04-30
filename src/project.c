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

static void load_targets(BdtProject *project, const BdtConfig *config) {
    for (size_t i = 0; i < config->section_count; ++i) {
        const char *sname = config->sections[i].name;
        if (strncmp(sname, "target.", 7)) continue;
        if (project->target_count >= BDT_MAX_ITEMS) break;
        BdtTarget *t = &project->targets[project->target_count++];
        memset(t, 0, sizeof(*t));
        snprintf(t->name, sizeof(t->name), "%s", sname + 7);
        t->cache = 1;
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
            else if (!strcmp(p->key, "common_dirs")) snprintf(t->common_dirs, sizeof(t->common_dirs), "%s", p->value);
            else if (!strcmp(p->key, "main_dir")) snprintf(t->main_dir, sizeof(t->main_dir), "%s", p->value);
            else if (!strcmp(p->key, "runtime_sources")) snprintf(t->runtime_sources, sizeof(t->runtime_sources), "%s", p->value);
            else if (!strcmp(p->key, "system_output")) snprintf(t->system_output, sizeof(t->system_output), "%s", p->value);
            else if (!strcmp(p->key, "system_linker_script")) snprintf(t->system_linker_script, sizeof(t->system_linker_script), "%s", p->value);
            else if (!strcmp(p->key, "uwm_output")) snprintf(t->uwm_output, sizeof(t->uwm_output), "%s", p->value);
            else if (!strcmp(p->key, "uwm_apps")) snprintf(t->uwm_apps, sizeof(t->uwm_apps), "%s", p->value);
            else if (!strcmp(p->key, "skip_apps")) snprintf(t->skip_apps, sizeof(t->skip_apps), "%s", p->value);
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

                    BdtAppRule *rule = NULL;
                    for (size_t r = 0; r < t->app_rule_count; ++r) {
                        if (!strcmp(t->app_rules[r].name, app_name)) {
                            rule = &t->app_rules[r];
                            break;
                        }
                    }
                    if (!rule && t->app_rule_count < BDT_MAX_ITEMS) {
                        rule = &t->app_rules[t->app_rule_count++];
                        memset(rule, 0, sizeof(*rule));
                        snprintf(rule->name, sizeof(rule->name), "%s", app_name);
                    }
                    if (rule) {
                        if (!strcmp(field, "sources")) snprintf(rule->sources, sizeof(rule->sources), "%s", p->value);
                        else if (!strcmp(field, "source_dirs")) snprintf(rule->source_dirs, sizeof(rule->source_dirs), "%s", p->value);
                        else if (!strcmp(field, "exclude_sources")) snprintf(rule->exclude_sources, sizeof(rule->exclude_sources), "%s", p->value);
                        else if (!strcmp(field, "cflags")) snprintf(rule->cflags, sizeof(rule->cflags), "%s", p->value);
                    }
                }
            }
            else if (!strcmp(p->key, "always")) t->always = !strcmp(p->value, "true") || !strcmp(p->value, "1") || !strcmp(p->value, "yes");
            else if (!strcmp(p->key, "cache")) t->cache = !strcmp(p->value, "true") || !strcmp(p->value, "1") || !strcmp(p->value, "yes");
            else if (!strcmp(p->key, "parallel")) t->parallel = !strcmp(p->value, "true") || !strcmp(p->value, "1") || !strcmp(p->value, "yes");
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
    snprintf(project->name, sizeof(project->name), "%s", name ? name : "project");
    snprintf(project->default_target, sizeof(project->default_target), "%s", def ? def : "all");
    snprintf(project->build_dir, sizeof(project->build_dir), "%s", build_dir ? build_dir : ".bdt");
    snprintf(project->language, sizeof(project->language), "%s", lang ? lang : "en");
    project->lang = bdt_lang_from_text(project->language);

    add_project_var(project, "project", project->name);
    add_project_var(project, "root", project->root);
    add_project_var(project, "build_dir", project->build_dir);
    bdt_load_toolchain(project, config);

    const char *subs = bdt_config_get(config, "subprojects", "paths");
    project->subproject_count = (size_t)bdt_split_list(subs, project->subprojects, BDT_MAX_ITEMS);
    load_targets(project, config);
    bdt_scan_build_files(project);
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
