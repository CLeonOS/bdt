#include "bdt.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#ifdef BDT_PLATFORM_CLEONOS
#include <cleonos_syscall.h>
#endif
#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#define mkdir_one(p) _mkdir(p)
#else
#include <sys/stat.h>
#include <unistd.h>
#define mkdir_one(p) mkdir((p), 0755)
#endif

typedef enum {
    BDT_LOG_STYLE_DEFAULT = 0,
    BDT_LOG_STYLE_KERNEL = 1
} BdtLogStyle;

static BdtLanguage g_lang = BDT_LANG_EN;
static int g_verbose = 0;
static int g_color = 1;
static BdtLogStyle g_style = BDT_LOG_STYLE_DEFAULT;

static const char *style_name(BdtLogStyle style) {
    return style == BDT_LOG_STYLE_KERNEL ? "kernel" : "default";
}

static BdtLogStyle parse_style(const char *text, int *ok) {
    if (ok) *ok = 1;
    if (!text || !*text || !strcmp(text, "default")) return BDT_LOG_STYLE_DEFAULT;
    if (!strcmp(text, "kernel") || !strcmp(text, "linux-kernel") || !strcmp(text, "linux")) return BDT_LOG_STYLE_KERNEL;
    if (ok) *ok = 0;
    return BDT_LOG_STYLE_DEFAULT;
}

#ifdef BDT_PLATFORM_CLEONOS
static const char *cleonos_user_home(void) {
    static char home[CLEONOS_USER_HOME_MAX];
    cleonos_user_info info;

    memset(&info, 0, sizeof(info));
    if (cleonos_sys_user_current(&info) != 0ULL && info.home[0] == '/') {
        snprintf(home, sizeof(home), "%s", info.home);
        return home;
    }

    return "/";
}
#endif

static int user_config_dir(char *out, size_t out_size) {
    const char *base = getenv("BDT_CONFIG_HOME");
    if (base && *base) return bdt_path_join(out, out_size, base, "bdt");
#ifdef BDT_PLATFORM_CLEONOS
    {
        char config_home[1024];
        if (bdt_path_join(config_home, sizeof(config_home), cleonos_user_home(), ".config") != 0) return -1;
        return bdt_path_join(out, out_size, config_home, "bdt");
    }
#elif defined(_WIN32)
    if (!base || !*base) base = getenv("APPDATA");
    if (base && *base) return bdt_path_join(out, out_size, base, "bdt");
    base = getenv("USERPROFILE");
    if (base && *base) return bdt_path_join(out, out_size, base, ".bdt");
#else
    if (!base || !*base) base = getenv("XDG_CONFIG_HOME");
    if (base && *base) return bdt_path_join(out, out_size, base, "bdt");
    base = getenv("HOME");
    if (base && *base) {
        char config_home[1024];
        if (bdt_path_join(config_home, sizeof(config_home), base, ".config") != 0) return -1;
        return bdt_path_join(out, out_size, config_home, "bdt");
    }
#endif
    return -1;
}

static int user_config_path(char *out, size_t out_size, int create_dir) {
    char dir[1024];
    if (user_config_dir(dir, sizeof(dir)) != 0) return -1;
    if (create_dir) bdt_mkdirs(dir);
    return bdt_path_join(out, out_size, dir, "config");
}

static BdtLogStyle load_user_style(void) {
    char path[1024];
    if (user_config_path(path, sizeof(path), 0) != 0) return BDT_LOG_STYLE_DEFAULT;
    FILE *f = fopen(path, "r");
    if (!f) return BDT_LOG_STYLE_DEFAULT;
    BdtLogStyle style = BDT_LOG_STYLE_DEFAULT;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char *p = bdt_trim(line);
        if (!strncmp(p, "log_style=", 10)) {
            int ok = 0;
            style = parse_style(bdt_trim(p + 10), &ok);
            if (!ok) style = BDT_LOG_STYLE_DEFAULT;
            break;
        }
    }
    fclose(f);
    return style;
}

void bdt_log_init(BdtLanguage lang, int verbose) {
    g_lang = lang;
    g_verbose = verbose;
    g_color = getenv("NO_COLOR") == NULL;
    g_style = load_user_style();
}

const char *bdt_msg(BdtLanguage lang, const char *key) {
    if (lang == BDT_LANG_ZH) {
        if (key && !strcmp(key, "build_start")) return "开始构建";
        if (key && !strcmp(key, "build_done")) return "构建完成";
        if (key && !strcmp(key, "cache_hit")) return "缓存命中";
        if (key && !strcmp(key, "target")) return "目标";
        if (key && !strcmp(key, "scan")) return "扫描构建描述";
    }
    if (key && !strcmp(key, "build_start")) return "build start";
    if (key && !strcmp(key, "build_done")) return "build done";
    if (key && !strcmp(key, "cache_hit")) return "cache hit";
    if (key && !strcmp(key, "target")) return "target";
    if (key && !strcmp(key, "scan")) return "scan build descriptors";
    return key ? key : "";
}

void bdt_log(BdtLogLevel level, const char *fmt, ...) {
    if (level == BDT_LOG_DEBUG && !g_verbose) return;
    const char *tag = "INFO";
    const char *color = "\033[1;36m";
    FILE *stream = stdout;
    if (level == BDT_LOG_DEBUG) {
        tag = "DEBUG";
        color = "\033[2;37m";
    }
    if (level == BDT_LOG_STEP) {
        tag = "STEP";
        color = "\033[1;34m";
    }
    if (level == BDT_LOG_WARN) {
        tag = "WARN";
        color = "\033[1;33m";
    }
    if (level == BDT_LOG_ERROR) {
        tag = "ERROR";
        color = "\033[1;31m";
        stream = stderr;
    }

    if (g_style == BDT_LOG_STYLE_KERNEL) {
        const char *kernel_tag = "  BDT";
        if (level == BDT_LOG_DEBUG) kernel_tag = "  DBG";
        else if (level == BDT_LOG_STEP) kernel_tag = "   CC";
        else if (level == BDT_LOG_WARN) kernel_tag = " WARN";
        else if (level == BDT_LOG_ERROR) kernel_tag = "  ERR";
        if (g_color) fputs(color, stream);
        fputs(kernel_tag, stream);
        if (g_color) fputs("\033[0m", stream);
        fputc(' ', stream);
        va_list ap;
        va_start(ap, fmt);
        vfprintf(stream, fmt, ap);
        va_end(ap);
        fputc('\n', stream);
        fflush(stream);
        return;
    }

    time_t now = time(NULL);
    struct tm *tmv = localtime(&now);
    if (g_color) fputs("\033[2;37m", stream);
    if (tmv) {
        fprintf(stream, "[%02d:%02d:%02d] ", tmv->tm_hour, tmv->tm_min, tmv->tm_sec);
    } else {
        fputs("[--:--:--] ", stream);
    }
    if (g_color) fputs(color, stream);
    fprintf(stream, "[%s]", tag);
    if (g_color) fputs("\033[0m", stream);
    fputc(' ', stream);

    va_list ap;
    va_start(ap, fmt);
    vfprintf(stream, fmt, ap);
    va_end(ap);
    fputc('\n', stream);
    fflush(stream);
}

void bdt_log_progress(size_t current, size_t total, const char *label) {
    if (g_style == BDT_LOG_STYLE_KERNEL) {
        (void)current;
        (void)total;
        if (g_color) fputs("\033[1;32m", stdout);
        fprintf(stdout, "   CC");
        if (g_color) fputs("\033[0m", stdout);
        fprintf(stdout, " %s\n", label ? label : "");
        fflush(stdout);
        return;
    }
    const size_t width = 28;
    size_t filled = 0;
    if (total > 0) filled = (current * width) / total;
    if (filled > width) filled = width;

    if (g_color) fputs("\033[1;34m", stdout);
    fputs("[BUILD]", stdout);
    if (g_color) fputs("\033[0m", stdout);
    fputs(" [", stdout);
    if (g_color) fputs("\033[1;32m", stdout);
    for (size_t i = 0; i < filled; ++i) fputc('=', stdout);
    if (g_color) fputs("\033[2;37m", stdout);
    for (size_t i = filled; i < width; ++i) fputc('-', stdout);
    if (g_color) fputs("\033[0m", stdout);
    fprintf(stdout, "] %zu/%zu %s\n", current, total, label ? label : "");
    fflush(stdout);
}

int bdt_log_style_command(const char *style) {
    if (!style || !*style) {
        printf("log style: %s\n", style_name(load_user_style()));
        printf("available: default, kernel\n");
        return 0;
    }

    int ok = 0;
    BdtLogStyle parsed = parse_style(style, &ok);
    if (!ok) {
        fprintf(stderr, "unknown log style: %s\n", style);
        fprintf(stderr, "available: default, kernel\n");
        return -1;
    }

    char path[1024];
    if (user_config_path(path, sizeof(path), 1) != 0) {
        fprintf(stderr, "cannot locate user config directory\n");
        return -1;
    }
    FILE *f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "cannot write %s\n", path);
        return -1;
    }
    fprintf(f, "log_style=%s\n", style_name(parsed));
    fclose(f);
    printf("log style: %s\n", style_name(parsed));
    printf("config: %s\n", path);
    return 0;
}
