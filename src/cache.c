#include "bdt.h"

#include <inttypes.h>
#include <string.h>

static int cache_path(const BdtProject *project, const BdtTarget *target, char *out, size_t out_size) {
    char dir[1024];
    bdt_path_join(dir, sizeof(dir), project->root, project->build_dir);
    bdt_mkdirs(dir);
    char file[256];
    snprintf(file, sizeof(file), "%s.hash", target->name);
    return bdt_path_join(out, out_size, dir, file);
}

int bdt_cache_is_fresh(const BdtProject *project, const BdtTarget *target, uint64_t hash) {
    char path[1024];
    if (cache_path(project, target, path, sizeof(path)) != 0) return 0;
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    uint64_t old = 0;
    int ok = fscanf(f, "%" SCNu64, &old) == 1;
    fclose(f);
    return ok && old == hash;
}

int bdt_cache_store(const BdtProject *project, const BdtTarget *target, uint64_t hash) {
    char path[1024];
    if (cache_path(project, target, path, sizeof(path)) != 0) return -1;
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fprintf(f, "%" PRIu64 "\n", hash);
    fclose(f);
    return 0;
}
