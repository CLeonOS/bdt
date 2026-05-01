#include "bdt.h"

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
int _putenv_s(const char *name, const char *value);
#else
int setenv(const char *name, const char *value, int overwrite);
#endif

static int run_shell(BdtProject *project, const char *raw) {
    char cmd[BDT_MAX_TEXT * 8];
    bdt_expand_vars(project, raw, cmd, sizeof(cmd));
    bdt_log(BDT_LOG_DEBUG, "%s", cmd);
    int rc = system(cmd);
    return rc == 0 ? 0 : rc;
}

static void cache_path(BdtProject *project, char *out, size_t out_size) {
    bdt_expand_vars(project, project->cache_config.path, out, out_size);
}

static void archive_path(BdtProject *project, const char *arg, char *out, size_t out_size) {
    if (arg && arg[0]) bdt_expand_vars(project, arg, out, out_size);
    else bdt_expand_vars(project, project->cache_config.archive, out, out_size);
}

static int ensure_parent_dir(const char *path) {
    char dir[1024];
    snprintf(dir, sizeof(dir), "%s", path);
    char *cut = strrchr(dir, '/');
    char *cut2 = strrchr(dir, '\\');
    if (!cut || cut2 > cut) cut = cut2;
    if (!cut) return 0;
    *cut = 0;
    return bdt_mkdirs(dir);
}

static int cache_export(BdtProject *project, const char *arg) {
    char cache[1024], archive[1024], cmd[BDT_MAX_TEXT * 2];
    cache_path(project, cache, sizeof(cache));
    archive_path(project, arg, archive, sizeof(archive));
    if (!bdt_dir_exists(cache)) {
        bdt_log(BDT_LOG_WARN, "cache directory missing: %s", cache);
        return 0;
    }
    ensure_parent_dir(archive);
#ifdef _WIN32
    snprintf(cmd, sizeof(cmd), "tar -cf \"%s\" -C \"%s\" .", archive, cache);
#else
    snprintf(cmd, sizeof(cmd), "tar -cf \"%s\" -C \"%s\" .", archive, cache);
#endif
    int rc = run_shell(project, cmd);
    if (rc == 0) bdt_log(BDT_LOG_INFO, "cache exported: %s", archive);
    return rc;
}

static int cache_import(BdtProject *project, const char *arg) {
    char cache[1024], archive[1024], cmd[BDT_MAX_TEXT * 2];
    cache_path(project, cache, sizeof(cache));
    archive_path(project, arg, archive, sizeof(archive));
    if (!bdt_file_exists(archive)) {
        bdt_log(BDT_LOG_WARN, "cache archive missing: %s", archive);
        return 0;
    }
    bdt_mkdirs(cache);
    snprintf(cmd, sizeof(cmd), "tar -xf \"%s\" -C \"%s\"", archive, cache);
    int rc = run_shell(project, cmd);
    if (rc == 0) bdt_log(BDT_LOG_INFO, "cache imported: %s", archive);
    return rc;
}

static int cache_hook(BdtProject *project, const char *action) {
    const char *cmd = NULL;
    if (!strcmp(action, "pull")) cmd = project->cache_config.pull_command;
    if (!strcmp(action, "push")) cmd = project->cache_config.push_command;
    if (!cmd || !cmd[0]) {
        bdt_log(BDT_LOG_WARN, "cache %s command is not configured", action);
        return 0;
    }
    char cache[1024], archive[1024];
    cache_path(project, cache, sizeof(cache));
    archive_path(project, NULL, archive, sizeof(archive));
#ifdef _WIN32
    _putenv_s("BDT_CACHE_PATH", cache);
    _putenv_s("BDT_CACHE_ARCHIVE", archive);
#else
    setenv("BDT_CACHE_PATH", cache, 1);
    setenv("BDT_CACHE_ARCHIVE", archive, 1);
#endif
    return run_shell(project, cmd);
}

int bdt_cache_command(BdtProject *project, const char *action, const char *arg) {
    if (!action || !*action) {
        bdt_log(BDT_LOG_ERROR, "cache action required: export, import, pull, or push");
        return -1;
    }
    if (!strcmp(action, "export")) return cache_export(project, arg);
    if (!strcmp(action, "import")) return cache_import(project, arg);
    if (!strcmp(action, "pull") || !strcmp(action, "push")) return cache_hook(project, action);
    bdt_log(BDT_LOG_ERROR, "unknown cache action '%s'", action);
    return -1;
}
