#include "bdt.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#define getcwd _getcwd
#define mkdir_one(p) _mkdir(p)
#else
#include <sys/stat.h>
#include <unistd.h>
#define mkdir_one(p) mkdir((p), 0755)
#endif

int bdt_file_exists(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

int bdt_dir_exists(const char *path) {
#ifdef _WIN32
    DWORD attr = GetFileAttributesA(path);
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

int bdt_path_join(char *out, size_t out_size, const char *a, const char *b) {
    const char sep =
#ifdef _WIN32
        '\\';
#else
        '/';
#endif
    if (!a || !*a) return snprintf(out, out_size, "%s", b ? b : "") < (int)out_size ? 0 : -1;
    size_t n = strlen(a);
    int need_sep = n > 0 && a[n - 1] != '/' && a[n - 1] != '\\';
    return snprintf(out, out_size, "%s%s%s", a, need_sep ? (char[]){sep, 0} : "", b ? b : "") < (int)out_size ? 0 : -1;
}

int bdt_abs_path(char *out, size_t out_size, const char *path) {
#ifdef _WIN32
    DWORD n = GetFullPathNameA(path, (DWORD)out_size, out, NULL);
    return n > 0 && n < out_size ? 0 : -1;
#else
    (void)out_size;
    char *r = realpath(path, out);
    return r ? 0 : -1;
#endif
}

char *bdt_trim(char *s) {
    if (!s) return s;
    while (isspace((unsigned char)*s)) s++;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) *--end = 0;
    return s;
}

int bdt_mkdirs(const char *path) {
    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; ++p) {
        if (*p == '/' || *p == '\\') {
            char old = *p;
            *p = 0;
            if (*tmp && !bdt_dir_exists(tmp)) mkdir_one(tmp);
            *p = old;
        }
    }
    if (*tmp && !bdt_dir_exists(tmp) && mkdir_one(tmp) != 0) return -1;
    return 0;
}

int bdt_split_list(const char *text, char items[][512], size_t max_items) {
    if (!text || !*text) return 0;
    char buf[BDT_MAX_TEXT];
    snprintf(buf, sizeof(buf), "%s", text);
    size_t count = 0;
    char *p = buf;
    while (p && *p && count < max_items) {
        char *comma = strchr(p, ',');
        if (comma) *comma = 0;
        char *item = bdt_trim(p);
        if (*item) snprintf(items[count++], 512, "%s", item);
        p = comma ? comma + 1 : NULL;
    }
    return (int)count;
}

int bdt_split_delim(const char *text, char delim, char items[][512], size_t max_items) {
    if (!text || !*text) return 0;
    char buf[BDT_MAX_TEXT];
    snprintf(buf, sizeof(buf), "%s", text);
    size_t count = 0;
    char *p = buf;
    while (p && *p && count < max_items) {
        char *sep = strchr(p, delim);
        if (sep) *sep = 0;
        char *item = bdt_trim(p);
        if (*item) snprintf(items[count++], 512, "%s", item);
        p = sep ? sep + 1 : NULL;
    }
    return (int)count;
}

int bdt_command_exists(const char *cmd) {
    if (!cmd || !*cmd) return 0;
    char probe[512];
#ifdef _WIN32
    snprintf(probe, sizeof(probe), "where %s >nul 2>nul", cmd);
#else
    snprintf(probe, sizeof(probe), "command -v %s >/dev/null 2>&1", cmd);
#endif
    return system(probe) == 0;
}

int bdt_remove_path(const char *path) {
    if (!path || !*path) return 0;
    char cmd[BDT_MAX_TEXT];
#ifdef _WIN32
    snprintf(cmd, sizeof(cmd), "cmd /C if exist \"%s\" (if exist \"%s\\*\" rmdir /S /Q \"%s\" else del /F /Q \"%s\")", path, path, path, path);
#else
    snprintf(cmd, sizeof(cmd), "rm -rf \"%s\"", path);
#endif
    return system(cmd) == 0 ? 0 : -1;
}

int bdt_parse_cli(int argc, char **argv, BdtCli *cli) {
    memset(cli, 0, sizeof(*cli));
    cli->argc = argc;
    cli->argv = argv;
    cli->project_file = "project.bdt";
    cli->jobs = 1;

    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i];
        if (!strcmp(a, "--project") && i + 1 < argc) cli->project_file = argv[++i];
        else if (!strcmp(a, "-j") && i + 1 < argc) cli->jobs = atoi(argv[++i]);
        else if (!strncmp(a, "-j", 2) && a[2]) cli->jobs = atoi(a + 2);
        else if (!strcmp(a, "--list")) cli->list = 1;
        else if (!strcmp(a, "--scan")) cli->scan = 1;
        else if (!strcmp(a, "--graph")) cli->graph = 1;
        else if (!strcmp(a, "view") || !strcmp(a, "--view")) cli->view = 1;
        else if (!strcmp(a, "explain")) {
            cli->explain = 1;
            if (i + 1 < argc && argv[i + 1][0] != '-') cli->explain_target = argv[++i];
        }
        else if (!strcmp(a, "clean")) {
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                cli->clean_target = 1;
                cli->clean_name = argv[++i];
            } else {
                cli->target = a;
            }
        }
        else if (!strcmp(a, "doctor")) cli->doctor = 1;
        else if (!strcmp(a, "cache")) {
            cli->cache_cmd = 1;
            if (i + 1 < argc) cli->cache_action = argv[++i];
            if (i + 1 < argc && argv[i + 1][0] != '-') cli->cache_arg = argv[++i];
        }
        else if (!strcmp(a, "--no-cache")) cli->no_cache = 1;
        else if (!strcmp(a, "-v") || !strcmp(a, "--verbose")) cli->verbose = 1;
        else if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
            printf("bdt [target] [--project file] [-j N] [--list] [--scan] [--graph] [--no-cache]\n");
            printf("bdt view\n");
            printf("bdt explain <target>\n");
            printf("bdt clean <target>\n");
            printf("bdt doctor\n");
            printf("bdt cache export [archive]\n");
            printf("bdt cache import [archive]\n");
            printf("bdt cache pull|push\n");
            exit(0);
        } else if (a[0] != '-') {
            cli->target = a;
        }
    }
    if (cli->jobs < 1) cli->jobs = 1;
    return 0;
}
