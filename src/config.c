#include "bdt.h"

#include <string.h>

static BdtSection *find_or_add_section(BdtConfig *config, const char *name) {
    for (size_t i = 0; i < config->section_count; ++i) {
        if (!strcmp(config->sections[i].name, name)) return &config->sections[i];
    }
    if (config->section_count >= BDT_MAX_ITEMS) return NULL;
    BdtSection *s = &config->sections[config->section_count++];
    memset(s, 0, sizeof(*s));
    snprintf(s->name, sizeof(s->name), "%s", name);
    return s;
}

int bdt_read_config(const char *path, BdtConfig *config) {
    memset(config, 0, sizeof(*config));
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    BdtSection *section = find_or_add_section(config, "root");
    char line[BDT_MAX_TEXT];
    while (fgets(line, sizeof(line), f)) {
        char *p = bdt_trim(line);
        if (!*p || *p == '#' || *p == ';') continue;
        if (*p == '[') {
            char *end = strchr(p, ']');
            if (!end) continue;
            *end = 0;
            section = find_or_add_section(config, bdt_trim(p + 1));
            continue;
        }
        char *eq = strchr(p, '=');
        if (!eq || !section || section->item_count >= BDT_MAX_ITEMS) continue;
        *eq = 0;
        char *key = bdt_trim(p);
        char *val = bdt_trim(eq + 1);
        if ((*val == '"' || *val == '\'') && val[strlen(val) - 1] == *val) {
            val[strlen(val) - 1] = 0;
            val++;
        }
        BdtPair *item = &section->items[section->item_count++];
        snprintf(item->key, sizeof(item->key), "%s", key);
        snprintf(item->value, sizeof(item->value), "%s", val);
    }
    fclose(f);
    return 0;
}

const char *bdt_config_get(const BdtConfig *config, const char *section, const char *key) {
    for (size_t i = 0; i < config->section_count; ++i) {
        if (strcmp(config->sections[i].name, section)) continue;
        for (size_t j = 0; j < config->sections[i].item_count; ++j) {
            if (!strcmp(config->sections[i].items[j].key, key)) return config->sections[i].items[j].value;
        }
    }
    return NULL;
}
