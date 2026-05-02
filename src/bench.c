#include "bdt.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    char target[128];
    char detail[1024];
    long duration_ms;
} BenchItem;

static int json_get_string(const char *line, const char *key, char *out, size_t out_size) {
    char needle[64];
    snprintf(needle, sizeof(needle), "\"%s\":\"", key);
    const char *p = strstr(line, needle);
    if (!p) return 0;
    p += strlen(needle);
    size_t oi = 0;
    while (*p && oi + 1 < out_size) {
        if (*p == '"' && (p == line || p[-1] != '\\')) break;
        if (*p == '\\' && p[1]) p++;
        out[oi++] = *p++;
    }
    out[oi] = 0;
    return 1;
}

static int json_get_long(const char *line, const char *key, long *out) {
    char needle[64];
    snprintf(needle, sizeof(needle), "\"%s\":", key);
    const char *p = strstr(line, needle);
    if (!p) return 0;
    p += strlen(needle);
    *out = strtol(p, NULL, 10);
    return 1;
}

static void add_item(BenchItem *items, size_t *count, size_t max, const char *target, const char *detail, long duration_ms) {
    if (*count >= max) return;
    snprintf(items[*count].target, sizeof(items[*count].target), "%s", target ? target : "");
    snprintf(items[*count].detail, sizeof(items[*count].detail), "%s", detail ? detail : "");
    items[*count].duration_ms = duration_ms;
    (*count)++;
}

static int cmp_duration_desc(const void *a, const void *b) {
    const BenchItem *ia = (const BenchItem *)a;
    const BenchItem *ib = (const BenchItem *)b;
    if (ia->duration_ms < ib->duration_ms) return 1;
    if (ia->duration_ms > ib->duration_ms) return -1;
    return 0;
}

static void print_items(const char *title, BenchItem *items, size_t count, size_t limit) {
    qsort(items, count, sizeof(items[0]), cmp_duration_desc);
    printf("%s\n", title);
    if (count == 0) {
        printf("  none\n");
        return;
    }
    if (limit > count) limit = count;
    for (size_t i = 0; i < limit; ++i) {
        if (items[i].target[0]) printf("  %ld ms  [%s] %s\n", items[i].duration_ms, items[i].target, items[i].detail);
        else printf("  %ld ms  %s\n", items[i].duration_ms, items[i].detail);
    }
}

int bdt_bench_report(const char *trace_path) {
    FILE *f = fopen(trace_path, "r");
    if (!f) {
        bdt_log(BDT_LOG_ERROR, "cannot read bench trace: %s", trace_path);
        return -1;
    }

    BenchItem targets[BDT_MAX_ITEMS];
    BenchItem compiles[BDT_MAX_ITEMS * 4];
    BenchItem commands[BDT_MAX_ITEMS];
    size_t target_count = 0, compile_count = 0, command_count = 0;
    long total_ms = 0;
    char line[BDT_MAX_TEXT * 2];

    while (fgets(line, sizeof(line), f)) {
        char kind[96], target[128], detail[1024];
        long duration = 0;
        if (!json_get_string(line, "kind", kind, sizeof(kind))) continue;
        json_get_string(line, "target", target, sizeof(target));
        json_get_string(line, "detail", detail, sizeof(detail));
        json_get_long(line, "duration_ms", &duration);
        if (!strcmp(kind, "target-done")) {
            add_item(targets, &target_count, BDT_MAX_ITEMS, target, detail, duration);
            total_ms += duration;
        } else if (!strcmp(kind, "compile")) {
            add_item(compiles, &compile_count, BDT_MAX_ITEMS * 4, target, detail, duration);
        } else if (!strcmp(kind, "command") || !strcmp(kind, "link")) {
            add_item(commands, &command_count, BDT_MAX_ITEMS, target, detail, duration);
        }
    }
    fclose(f);

    printf("\nbdt bench: %s\n", trace_path);
    printf("total target time: %ld ms\n", total_ms);
    print_items("slow targets:", targets, target_count, 10);
    print_items("slow compiles:", compiles, compile_count, 20);
    print_items("slow commands/links:", commands, command_count, 10);
    return 0;
}
