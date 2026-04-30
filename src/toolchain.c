#include "bdt.h"

#include <stdlib.h>
#include <string.h>

static void add_var(BdtProject *project, const char *key, const char *value) {
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

void bdt_load_toolchain(BdtProject *project, const BdtConfig *config) {
    const char *defaults[][2] = {
        {"cc", "cc"}, {"cxx", "c++"}, {"make", "make"},
        {"python", "python3"}, {NULL, NULL}
    };
    for (int i = 0; defaults[i][0]; ++i) add_var(project, defaults[i][0], defaults[i][1]);

    for (size_t i = 0; i < config->section_count; ++i) {
        const char *name = config->sections[i].name;
        if (strcmp(name, "vars") && strcmp(name, "tools") && strcmp(name, "config")) continue;
        for (size_t j = 0; j < config->sections[i].item_count; ++j) {
            const char *env = getenv(config->sections[i].items[j].key);
            add_var(project, config->sections[i].items[j].key, env ? env : config->sections[i].items[j].value);
        }
    }
}
