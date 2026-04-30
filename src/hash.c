#include "bdt.h"

#include <string.h>

static uint64_t fnv1a_update(uint64_t h, const unsigned char *data, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        h ^= data[i];
        h *= 1099511628211ULL;
    }
    return h;
}

uint64_t bdt_hash_text(const char *text) {
    uint64_t h = 1469598103934665603ULL;
    return fnv1a_update(h, (const unsigned char *)(text ? text : ""), text ? strlen(text) : 0);
}

uint64_t bdt_hash_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    uint64_t h = 1469598103934665603ULL;
    unsigned char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) h = fnv1a_update(h, buf, n);
    fclose(f);
    return h;
}

uint64_t bdt_hash_target_inputs(const BdtProject *project, const BdtTarget *target) {
    uint64_t h = bdt_hash_text(target->command);
    h ^= bdt_hash_text(target->deps) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    for (size_t i = 0; i < project->build_file_count; ++i) {
        uint64_t fh = bdt_hash_file(project->build_files[i]);
        h ^= fh + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    }
    char items[BDT_MAX_ITEMS][512];
    int count = bdt_split_list(target->inputs, items, BDT_MAX_ITEMS);
    for (int i = 0; i < count; ++i) {
        char path[1024];
        bdt_path_join(path, sizeof(path), project->root, items[i]);
        uint64_t fh = bdt_hash_file(path);
        h ^= fh + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    }
    return h;
}
