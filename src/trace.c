#include "bdt.h"

#include <stdarg.h>
#include <string.h>

static FILE *g_trace = NULL;
static int g_first = 1;

static void json_escape(FILE *f, const char *s) {
    for (const unsigned char *p = (const unsigned char *)(s ? s : ""); *p; ++p) {
        if (*p == '\\' || *p == '"') fprintf(f, "\\%c", *p);
        else if (*p == '\n') fputs("\\n", f);
        else if (*p == '\r') fputs("\\r", f);
        else if (*p == '\t') fputs("\\t", f);
        else if (*p < 32) fprintf(f, "\\u%04x", *p);
        else fputc(*p, f);
    }
}

int bdt_trace_begin(const BdtProject *project, const char *path) {
    g_trace = fopen(path, "w");
    if (!g_trace) {
        bdt_log(BDT_LOG_ERROR, "cannot write trace file: %s", path);
        return -1;
    }
    g_first = 1;
    fprintf(g_trace, "{\n  \"project\": \"");
    json_escape(g_trace, project->name);
    fprintf(g_trace, "\",\n  \"root\": \"");
    json_escape(g_trace, project->root);
    fprintf(g_trace, "\",\n  \"events\": [\n");
    fflush(g_trace);
    return 0;
}

void bdt_trace_end(int rc) {
    if (!g_trace) return;
    fprintf(g_trace, "\n  ],\n  \"result\": %d\n}\n", rc);
    fclose(g_trace);
    g_trace = NULL;
}

int bdt_trace_enabled(void) {
    return g_trace != NULL;
}

void bdt_trace_event(const char *kind, const char *target, const char *detail, int rc, long duration_ms) {
    if (!g_trace) return;
    if (!g_first) fputs(",\n", g_trace);
    g_first = 0;
    fputs("    {\"kind\":\"", g_trace);
    json_escape(g_trace, kind);
    fputs("\",\"target\":\"", g_trace);
    json_escape(g_trace, target);
    fputs("\",\"detail\":\"", g_trace);
    json_escape(g_trace, detail);
    fprintf(g_trace, "\",\"rc\":%d,\"duration_ms\":%ld}", rc, duration_ms);
    fflush(g_trace);
}
