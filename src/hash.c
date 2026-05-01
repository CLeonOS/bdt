#include "bdt.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#else
int mkstemp(char *template);
#include <unistd.h>
#endif

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

uint64_t bdt_hash_depfile(const BdtProject *project, const char *depfile) {
    (void)project;
    FILE *f = fopen(depfile, "rb");
    if (!f) return 0;
    uint64_t h = 1469598103934665603ULL;
    char token[1024];
    size_t n = 0;
    int c;
    while ((c = fgetc(f)) != EOF) {
        if (c == '\\') {
            int next = fgetc(f);
            if (next == '\n' || next == '\r') continue;
            if (next != EOF) ungetc(next, f);
        }
        if (isspace(c) || c == ':') {
            if (n > 0) {
                token[n] = 0;
                if (strcmp(token, "\\") && !strstr(token, ".o")) {
                    h ^= bdt_hash_file(token) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
                }
                n = 0;
            }
        } else if (n + 1 < sizeof(token)) {
            token[n++] = (char)c;
        }
    }
    if (n > 0) {
        token[n] = 0;
        if (strcmp(token, "\\") && !strstr(token, ".o")) {
            h ^= bdt_hash_file(token) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
        }
    }
    fclose(f);
    return h;
}

uint64_t bdt_hash_tool_version(const char *tool) {
    char cmd[512];
#ifdef _WIN32
    char tmp[L_tmpnam];
    if (!tmpnam(tmp)) return bdt_hash_text(tool);
    snprintf(cmd, sizeof(cmd), "%s --version > \"%s\" 2>NUL", tool && *tool ? tool : "cc", tmp);
#else
    char tmp[] = "/tmp/bdt-tool-version-XXXXXX";
    int fd = mkstemp(tmp);
    if (fd < 0) return bdt_hash_text(tool);
    close(fd);
    snprintf(cmd, sizeof(cmd), "%s --version > \"%s\" 2>/dev/null", tool && *tool ? tool : "cc", tmp);
#endif
    int rc = system(cmd);
    uint64_t h = rc == 0 ? bdt_hash_file(tmp) : bdt_hash_text(tool);
    remove(tmp);
    return h;
}
