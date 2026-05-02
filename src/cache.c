#include "bdt.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

#define BDT_MAX_QUICK_MANIFESTS 8192

typedef struct {
    char target[96];
    char file[1200];
} BdtQuickManifest;

static BdtQuickManifest g_quick_manifests[BDT_MAX_QUICK_MANIFESTS];
static size_t g_quick_manifest_count = 0;
static char g_quick_loaded_target[96] = "";

static int quick_manifest_cmp(const void *a, const void *b) {
    const BdtQuickManifest *ma = (const BdtQuickManifest *)a;
    const BdtQuickManifest *mb = (const BdtQuickManifest *)b;
    return strcmp(ma->file, mb->file);
}

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

static uint64_t mix_hash(uint64_t h, uint64_t v) {
    return h ^ (v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2));
}

static void sanitize_name(const char *text, char *out, size_t out_size) {
    size_t oi = 0;
    for (size_t i = 0; text && text[i] && oi + 1 < out_size; ++i) {
        char c = text[i];
        out[oi++] = (c == '/' || c == '\\' || c == ':' || c == ' ' || c == '.') ? '_' : c;
    }
    out[oi] = 0;
}

static int manifest_path_ex(const BdtProject *project, const BdtTarget *target, const char *obj, const char *suffix,
                            char *out, size_t out_size, int create_dirs) {
    char clean[1024], file[1200], target_dir[1024];
    char dir[1024], cache_dir[1024];
    bdt_path_join(dir, sizeof(dir), project->root, project->build_dir);
    bdt_path_join(cache_dir, sizeof(cache_dir), dir, "cache");
    bdt_path_join(target_dir, sizeof(target_dir), cache_dir, target->name);
    if (create_dirs) bdt_mkdirs(target_dir);
    sanitize_name(obj, clean, sizeof(clean));
    snprintf(file, sizeof(file), "%s.%s", clean, suffix);
    return bdt_path_join(out, out_size, target_dir, file);
}

static int target_cache_dir(const BdtProject *project, const BdtTarget *target, char *out, size_t out_size, int create_dirs) {
    char dir[1024], cache_dir[1024];
    bdt_path_join(dir, sizeof(dir), project->root, project->build_dir);
    bdt_path_join(cache_dir, sizeof(cache_dir), dir, "cache");
    bdt_path_join(out, out_size, cache_dir, target->name);
    if (create_dirs) bdt_mkdirs(out);
    return 0;
}

static void manifest_file_name(const char *obj, const char *suffix, char *out, size_t out_size) {
    char clean[1024];
    sanitize_name(obj, clean, sizeof(clean));
    snprintf(out, out_size, "%s.%s", clean, suffix);
}

static int manifest_path(const BdtProject *project, const BdtTarget *target, const char *obj, const char *suffix,
                         char *out, size_t out_size) {
    return manifest_path_ex(project, target, obj, suffix, out, out_size, 1);
}

static int manifest_path_readonly(const BdtProject *project, const BdtTarget *target, const char *obj, const char *suffix,
                                  char *out, size_t out_size) {
    return manifest_path_ex(project, target, obj, suffix, out, out_size, 0);
}

static int read_manifest_u64s(const char *path, const char **keys, uint64_t *values, size_t count) {
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    size_t found = 0;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        char k[96];
        uint64_t v = 0;
        if (sscanf(line, "%95[^=]=%" SCNu64, k, &v) != 2) continue;
        for (size_t i = 0; i < count; ++i) {
            if (!strcmp(k, keys[i])) {
                values[i] = v;
                found++;
                break;
            }
        }
        if (found >= count) break;
    }
    fclose(f);
    return found > 0;
}

static void quick_manifest_load_file(const BdtProject *project, const BdtTarget *target, const char *file_name) {
    (void)project;
    if (g_quick_manifest_count >= BDT_MAX_QUICK_MANIFESTS) return;
    BdtQuickManifest *m = &g_quick_manifests[g_quick_manifest_count++];
    snprintf(m->target, sizeof(m->target), "%s", target->name);
    snprintf(m->file, sizeof(m->file), "%s", file_name);
}

static void quick_manifest_load_target(const BdtProject *project, const BdtTarget *target) {
    if (!strcmp(g_quick_loaded_target, target->name)) return;
    g_quick_manifest_count = 0;
    snprintf(g_quick_loaded_target, sizeof(g_quick_loaded_target), "%s", target->name);
    char dir[1024];
    target_cache_dir(project, target, dir, sizeof(dir), 0);
#ifdef _WIN32
    char pattern[1200];
    bdt_path_join(pattern, sizeof(pattern), dir, "*");
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (strstr(fd.cFileName, ".compile") || strstr(fd.cFileName, ".link")) quick_manifest_load_file(project, target, fd.cFileName);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (strstr(e->d_name, ".compile") || strstr(e->d_name, ".link")) quick_manifest_load_file(project, target, e->d_name);
    }
    closedir(d);
#endif
    if (g_quick_manifest_count > 1) {
        qsort(g_quick_manifests, g_quick_manifest_count, sizeof(g_quick_manifests[0]), quick_manifest_cmp);
    }
}

static const BdtQuickManifest *quick_manifest_find(const BdtProject *project, const BdtTarget *target,
                                                   const char *obj, const char *suffix) {
    quick_manifest_load_target(project, target);
    char file[1200];
    manifest_file_name(obj, suffix, file, sizeof(file));
    size_t lo = 0;
    size_t hi = g_quick_manifest_count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        int cmp = strcmp(g_quick_manifests[mid].file, file);
        if (cmp == 0) return &g_quick_manifests[mid];
        if (cmp < 0) lo = mid + 1;
        else hi = mid;
    }
    return NULL;
}

int bdt_target_cache_has_manifests(const BdtProject *project, const BdtTarget *target) {
    quick_manifest_load_target(project, target);
    return g_quick_manifest_count > 0;
}

static int quick_manifest_read_values(const BdtProject *project, const BdtTarget *target, const BdtQuickManifest *manifest,
                                      const char **keys, uint64_t *values, size_t count) {
    if (!manifest) return 0;
    char dir[1024], path[1600];
    target_cache_dir(project, target, dir, sizeof(dir), 0);
    bdt_path_join(path, sizeof(path), dir, manifest->file);
    return read_manifest_u64s(path, keys, values, count);
}

static void set_reason(char *reason, size_t reason_size, const char *text) {
    if (reason && reason_size) snprintf(reason, reason_size, "%s", text);
}

static int file_newer_than(const char *a, const char *b) {
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA da, db;
    if (!GetFileAttributesExA(a, GetFileExInfoStandard, &da)) return 0;
    if (!GetFileAttributesExA(b, GetFileExInfoStandard, &db)) return 1;
    return CompareFileTime(&da.ftLastWriteTime, &db.ftLastWriteTime) > 0;
#else
    struct stat sa, sb;
    if (stat(a, &sa) != 0) return 0;
    if (stat(b, &sb) != 0) return 1;
    return sa.st_mtime > sb.st_mtime;
#endif
}

static uint64_t compile_signature(const char *src, const char *dep, const char *tool, const char *flags,
                                  uint64_t *src_hash, uint64_t *dep_hash, uint64_t *tool_hash,
                                  uint64_t *flags_hash) {
    *src_hash = bdt_hash_file(src);
    *dep_hash = bdt_hash_depfile(NULL, dep);
    *tool_hash = bdt_hash_tool_version(tool);
    *flags_hash = bdt_hash_text(flags);
    uint64_t h = 1469598103934665603ULL;
    h = mix_hash(h, *src_hash);
    h = mix_hash(h, *dep_hash);
    h = mix_hash(h, *tool_hash);
    h = mix_hash(h, *flags_hash);
    return h;
}

int bdt_compile_cache_fresh(const BdtProject *project, const BdtTarget *target, const char *src, const char *obj,
                            const char *dep, const char *tool, const char *flags, char *reason, size_t reason_size) {
    if (!bdt_file_exists(obj)) {
        set_reason(reason, reason_size, "object missing");
        return 0;
    }
    if (!bdt_file_exists(dep)) {
        set_reason(reason, reason_size, "dependency file missing");
        return 0;
    }
    char path[1024];
    if (manifest_path_readonly(project, target, obj, "compile", path, sizeof(path)) != 0 || !bdt_file_exists(path)) {
        set_reason(reason, reason_size, "manifest missing");
        return 0;
    }
    uint64_t src_hash, dep_hash, tool_hash, flags_hash, sig;
    sig = compile_signature(src, dep, tool, flags, &src_hash, &dep_hash, &tool_hash, &flags_hash);
    const char *keys[] = {"signature", "src_hash", "dep_hash", "tool_hash", "flags_hash"};
    uint64_t values[] = {0, 0, 0, 0, 0};
    read_manifest_u64s(path, keys, values, 5);
    if (values[0] != sig) {
        uint64_t old_src = values[1], old_dep = values[2], old_tool = values[3], old_flags = values[4];
        if (old_src && old_src != src_hash) set_reason(reason, reason_size, "source changed");
        else if (old_dep && old_dep != dep_hash) set_reason(reason, reason_size, "headers changed");
        else if (old_tool && old_tool != tool_hash) set_reason(reason, reason_size, "tool version changed");
        else if (old_flags && old_flags != flags_hash) set_reason(reason, reason_size, "flags changed");
        else set_reason(reason, reason_size, "cache signature changed");
        return 0;
    }
    set_reason(reason, reason_size, "fresh");
    return 1;
}

int bdt_compile_cache_quick_fresh(const BdtProject *project, const BdtTarget *target, const char *src, const char *obj,
                                  const char *dep, const char *tool, const char *flags, char *reason, size_t reason_size) {
    const BdtQuickManifest *manifest = quick_manifest_find(project, target, obj, "compile");
    if (!manifest) {
        set_reason(reason, reason_size, "manifest missing");
        return 0;
    }
    if (!bdt_file_exists(obj)) {
        set_reason(reason, reason_size, "object missing");
        return 0;
    }
    if (!bdt_file_exists(dep)) {
        set_reason(reason, reason_size, "dependency file missing");
        return 0;
    }
    const char *keys[] = {"tool_hash", "flags_hash"};
    uint64_t values[] = {0, 0};
    quick_manifest_read_values(project, target, manifest, keys, values, 2);
    uint64_t tool_hash = bdt_hash_tool_version(tool);
    uint64_t flags_hash = bdt_hash_text(flags);
    uint64_t old_tool = values[0], old_flags = values[1];
    if (old_tool != tool_hash) {
        set_reason(reason, reason_size, "tool version changed");
        return 0;
    }
    if (old_flags != flags_hash) {
        set_reason(reason, reason_size, "flags changed");
        return 0;
    }
    if (file_newer_than(src, obj)) {
        set_reason(reason, reason_size, "source changed");
        return 0;
    }
    set_reason(reason, reason_size, "fresh");
    return 1;
}

int bdt_compile_cache_store(const BdtProject *project, const BdtTarget *target, const char *src, const char *obj,
                            const char *dep, const char *tool, const char *flags) {
    char path[1024];
    if (manifest_path(project, target, obj, "compile", path, sizeof(path)) != 0) return -1;
    uint64_t src_hash, dep_hash, tool_hash, flags_hash;
    uint64_t sig = compile_signature(src, dep, tool, flags, &src_hash, &dep_hash, &tool_hash, &flags_hash);
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fprintf(f, "signature=%" PRIu64 "\n", sig);
    fprintf(f, "src_hash=%" PRIu64 "\n", src_hash);
    fprintf(f, "dep_hash=%" PRIu64 "\n", dep_hash);
    fprintf(f, "tool_hash=%" PRIu64 "\n", tool_hash);
    fprintf(f, "flags_hash=%" PRIu64 "\n", flags_hash);
    fprintf(f, "src=%s\nobj=%s\ndep=%s\ntool=%s\nflags=%s\n", src, obj, dep, tool, flags);
    fclose(f);
    return 0;
}

static uint64_t hash_object_list(const char *objects) {
    uint64_t h = bdt_hash_text(objects);
    const char *p = objects;
    while (p && *p) {
        while (*p == ' ' || *p == '\t' || *p == '"') p++;
        if (!*p) break;
        char path[1024];
        size_t n = 0;
        while (*p && *p != '"' && *p != ' ' && *p != '\t' && n + 1 < sizeof(path)) path[n++] = *p++;
        path[n] = 0;
        if (path[0]) h = mix_hash(h, bdt_hash_file(path));
        while (*p && *p != ' ' && *p != '\t') p++;
    }
    return h;
}

static uint64_t link_signature(const char *objects, const char *tool, const char *flags, const char *script,
                               uint64_t *objects_hash, uint64_t *tool_hash, uint64_t *flags_hash,
                               uint64_t *script_hash) {
    *objects_hash = hash_object_list(objects);
    *tool_hash = bdt_hash_tool_version(tool);
    *flags_hash = bdt_hash_text(flags);
    *script_hash = script && script[0] ? bdt_hash_file(script) : 0;
    uint64_t h = 1469598103934665603ULL;
    h = mix_hash(h, *objects_hash);
    h = mix_hash(h, *tool_hash);
    h = mix_hash(h, *flags_hash);
    h = mix_hash(h, *script_hash);
    return h;
}

int bdt_link_cache_fresh(const BdtProject *project, const BdtTarget *target, const char *out, const char *objects,
                         const char *tool, const char *flags, const char *script, char *reason, size_t reason_size) {
    if (!bdt_file_exists(out)) {
        set_reason(reason, reason_size, "output missing");
        return 0;
    }
    char path[1024];
    if (manifest_path_readonly(project, target, out, "link", path, sizeof(path)) != 0 || !bdt_file_exists(path)) {
        set_reason(reason, reason_size, "manifest missing");
        return 0;
    }
    uint64_t objects_hash, tool_hash, flags_hash, script_hash;
    uint64_t sig = link_signature(objects, tool, flags, script, &objects_hash, &tool_hash, &flags_hash, &script_hash);
    const char *keys[] = {"signature"};
    uint64_t values[] = {0};
    read_manifest_u64s(path, keys, values, 1);
    if (values[0] != sig) {
        set_reason(reason, reason_size, "objects, linker script, flags, or tool changed");
        return 0;
    }
    set_reason(reason, reason_size, "fresh");
    return 1;
}

int bdt_link_cache_quick_fresh(const BdtProject *project, const BdtTarget *target, const char *out, const char *objects,
                               const char *tool, const char *flags, const char *script, char *reason, size_t reason_size) {
    (void)objects;
    const BdtQuickManifest *manifest = quick_manifest_find(project, target, out, "link");
    if (!manifest) {
        set_reason(reason, reason_size, "manifest missing");
        return 0;
    }
    if (!bdt_file_exists(out)) {
        set_reason(reason, reason_size, "output missing");
        return 0;
    }
    const char *keys[] = {"tool_hash", "flags_hash", "script_hash"};
    uint64_t values[] = {0, 0, 0};
    quick_manifest_read_values(project, target, manifest, keys, values, 3);
    uint64_t tool_hash = bdt_hash_tool_version(tool);
    uint64_t flags_hash = bdt_hash_text(flags);
    uint64_t script_hash = script && script[0] ? bdt_hash_file(script) : 0;
    uint64_t old_tool = values[0], old_flags = values[1], old_script = values[2];
    if (old_tool != tool_hash || old_flags != flags_hash || old_script != script_hash) {
        set_reason(reason, reason_size, "linker script, flags, or tool changed");
        return 0;
    }
    set_reason(reason, reason_size, "fresh");
    return 1;
}

int bdt_link_cache_store(const BdtProject *project, const BdtTarget *target, const char *out, const char *objects,
                         const char *tool, const char *flags, const char *script) {
    char path[1024];
    if (manifest_path(project, target, out, "link", path, sizeof(path)) != 0) return -1;
    uint64_t objects_hash, tool_hash, flags_hash, script_hash;
    uint64_t sig = link_signature(objects, tool, flags, script, &objects_hash, &tool_hash, &flags_hash, &script_hash);
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fprintf(f, "signature=%" PRIu64 "\n", sig);
    fprintf(f, "objects_hash=%" PRIu64 "\n", objects_hash);
    fprintf(f, "tool_hash=%" PRIu64 "\n", tool_hash);
    fprintf(f, "flags_hash=%" PRIu64 "\n", flags_hash);
    fprintf(f, "script_hash=%" PRIu64 "\n", script_hash);
    fprintf(f, "out=%s\ntool=%s\nflags=%s\nscript=%s\nobjects=%s\n", out, tool, flags, script ? script : "", objects);
    fclose(f);
    return 0;
}
