#include "bdt.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

static BdtLanguage g_lang = BDT_LANG_EN;
static int g_verbose = 0;
static int g_color = 1;

void bdt_log_init(BdtLanguage lang, int verbose) {
    g_lang = lang;
    g_verbose = verbose;
    g_color = getenv("NO_COLOR") == NULL;
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
