#include "bdt.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
int bdt_view_project(const BdtProject *project) {
    (void)project;
    bdt_log(BDT_LOG_ERROR, "bdt view requires ncurses and is currently available on Unix-like systems");
    return -1;
}
#else
#include <dlfcn.h>
#include <sys/ioctl.h>
#include <unistd.h>

typedef struct {
    void *handle;
    void *stdscr;
    void *(*initscr)(void);
    int (*endwin)(void);
    int (*cbreak)(void);
    int (*noecho)(void);
    int (*keypad)(void *, int);
    int (*curs_set)(int);
    int (*start_color)(void);
    int (*use_default_colors)(void);
    int (*init_pair)(short, short, short);
    int (*clear)(void);
    int (*refresh)(void);
    int (*getch)(void);
    int (*attron)(int);
    int (*attroff)(int);
    int (*mvprintw)(int, int, const char *, ...);
} CursesApi;

typedef struct {
    const char *text;
    const BdtTarget *target;
    int section;
} ViewLine;

#define BDT_VIEW_MAX_LINES 2048
#define BDT_KEY_UP 259
#define BDT_KEY_DOWN 258
#define BDT_KEY_NPAGE 338
#define BDT_KEY_PPAGE 339
#define BDT_A_BOLD 0x200000
#define BDT_COLOR_PAIR(n) ((n) << 8)

static void *sym(CursesApi *api, const char *name) {
    return dlsym(api->handle, name);
}

static int load_curses(CursesApi *api) {
    memset(api, 0, sizeof(*api));
    const char *libs[] = {
        "libncursesw.so.6",
        "libncursesw.so",
        "libncurses.so.6",
        "libncurses.so",
        NULL
    };
    for (int i = 0; libs[i]; ++i) {
        api->handle = dlopen(libs[i], RTLD_NOW | RTLD_GLOBAL);
        if (api->handle) break;
    }
    if (!api->handle) return -1;

    api->initscr = (void *(*)(void))sym(api, "initscr");
    api->endwin = (int (*)(void))sym(api, "endwin");
    api->cbreak = (int (*)(void))sym(api, "cbreak");
    api->noecho = (int (*)(void))sym(api, "noecho");
    api->keypad = (int (*)(void *, int))sym(api, "keypad");
    api->curs_set = (int (*)(int))sym(api, "curs_set");
    api->start_color = (int (*)(void))sym(api, "start_color");
    api->use_default_colors = (int (*)(void))sym(api, "use_default_colors");
    api->init_pair = (int (*)(short, short, short))sym(api, "init_pair");
    api->clear = (int (*)(void))sym(api, "clear");
    api->refresh = (int (*)(void))sym(api, "refresh");
    api->getch = (int (*)(void))sym(api, "getch");
    api->attron = (int (*)(int))sym(api, "attron");
    api->attroff = (int (*)(int))sym(api, "attroff");
    api->mvprintw = (int (*)(int, int, const char *, ...))sym(api, "mvprintw");

    if (!api->initscr || !api->endwin || !api->cbreak || !api->noecho || !api->keypad ||
        !api->clear || !api->refresh || !api->getch || !api->mvprintw) {
        dlclose(api->handle);
        memset(api, 0, sizeof(*api));
        return -1;
    }
    return 0;
}

static void unload_curses(CursesApi *api) {
    if (api->handle) dlclose(api->handle);
    memset(api, 0, sizeof(*api));
}

enum {
    VIEW_SECTION_PROJECT = 0,
    VIEW_SECTION_BUILD_FILES,
    VIEW_SECTION_SUBPROJECTS,
    VIEW_SECTION_PLUGINS,
    VIEW_SECTION_TARGETS
};

static void add_line(ViewLine *lines, size_t *count, const char *text, int section, const BdtTarget *target) {
    if (*count >= BDT_VIEW_MAX_LINES) return;
    lines[*count].text = text;
    lines[*count].target = target;
    lines[*count].section = section;
    (*count)++;
}

static void add_target_lines(const BdtProject *project, ViewLine *lines, size_t *count) {
    add_line(lines, count, "Targets", VIEW_SECTION_TARGETS, NULL);
    for (size_t i = 0; i < project->target_count; ++i) {
        add_line(lines, count, project->targets[i].name, VIEW_SECTION_TARGETS, &project->targets[i]);
    }
}

static void print_clamped(CursesApi *c, int y, int x, int width, const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (width > 3 && (int)strlen(buf) > width) {
        buf[width - 3] = '.';
        buf[width - 2] = '.';
        buf[width - 1] = '.';
        buf[width] = 0;
    }
    c->mvprintw(y, x, "%s", buf);
}

static void draw_target_detail(CursesApi *c, const BdtProject *project, const ViewLine *line, int x, int y, int w, int h) {
    const BdtTarget *target = line->target;
    int row = y;
    if (!target) {
        if (line->section == VIEW_SECTION_BUILD_FILES) {
            print_clamped(c, row++, x, w, "Build files: %zu", project->build_file_count);
            for (size_t i = 0; i < project->build_file_count && row < y + h; ++i) {
                print_clamped(c, row++, x + 2, w - 2, "%s", project->build_files[i]);
            }
            return;
        }
        if (line->section == VIEW_SECTION_SUBPROJECTS) {
            print_clamped(c, row++, x, w, "Subprojects: %zu", project->subproject_count);
            for (size_t i = 0; i < project->subproject_count && row < y + h; ++i) {
                print_clamped(c, row++, x + 2, w - 2, "%s", project->subprojects[i]);
            }
            return;
        }
        if (line->section == VIEW_SECTION_PLUGINS) {
            print_clamped(c, row++, x, w, "Plugins: %zu", project->plugin_count);
            for (size_t i = 0; i < project->plugin_count && row < y + h; ++i) {
                const BdtPlugin *plugin = &project->plugins[i];
                print_clamped(c, row++, x + 2, w - 2, "%s", plugin->name);
                if (plugin->runner[0] && row < y + h) print_clamped(c, row++, x + 4, w - 4, "runner: %s", plugin->runner);
                if (plugin->path[0] && row < y + h) print_clamped(c, row++, x + 4, w - 4, "path: %s", plugin->path);
                if (plugin->command[0] && row < y + h) print_clamped(c, row++, x + 4, w - 4, "command: %s", plugin->command);
            }
            return;
        }
        print_clamped(c, row++, x, w, "Project: %s", project->name);
        print_clamped(c, row++, x, w, "Root: %s", project->root);
        print_clamped(c, row++, x, w, "Default target: %s", project->default_target);
        print_clamped(c, row++, x, w, "Build dir: %s", project->build_dir);
        print_clamped(c, row++, x, w, "Language: %s", project->language);
        row++;
        print_clamped(c, row++, x, w, "Targets: %zu", project->target_count);
        print_clamped(c, row++, x, w, "Subprojects: %zu", project->subproject_count);
        print_clamped(c, row++, x, w, "Build files: %zu", project->build_file_count);
        print_clamped(c, row++, x, w, "Plugins: %zu", project->plugin_count);
        if (project->cache_config.path[0]) print_clamped(c, row++, x, w, "Cache: %s", project->cache_config.path);
        return;
    }

    print_clamped(c, row++, x, w, "Target: %s", target->name);
    print_clamped(c, row++, x, w, "Type: %s", target->type[0] ? target->type : "command");
    if (target->deps[0]) print_clamped(c, row++, x, w, "Deps: %s", target->deps);
    if (target->tool[0]) print_clamped(c, row++, x, w, "Tool: %s", target->tool);
    if (target->plugin[0]) print_clamped(c, row++, x, w, "Plugin: %s", target->plugin);
    if (target->cache) print_clamped(c, row++, x, w, "Cache: enabled");
    if (target->always) print_clamped(c, row++, x, w, "Always: true");
    if (target->parallel) print_clamped(c, row++, x, w, "Parallel: true");
    if (target->inputs[0]) print_clamped(c, row++, x, w, "Inputs: %s", target->inputs);
    if (target->sources[0]) print_clamped(c, row++, x, w, "Sources: %s", target->sources);
    if (target->source_dirs[0]) print_clamped(c, row++, x, w, "Source dirs: %s", target->source_dirs);
    if (target->objects[0]) print_clamped(c, row++, x, w, "Objects: %s", target->objects);
    if (target->output[0]) print_clamped(c, row++, x, w, "Output: %s", target->output);
    if (target->linker_script[0]) print_clamped(c, row++, x, w, "Linker script: %s", target->linker_script);
    if (target->flags[0]) print_clamped(c, row++, x, w, "Flags: %s", target->flags);
    if (target->cflags[0]) print_clamped(c, row++, x, w, "C flags: %s", target->cflags);
    if (target->ldflags[0]) print_clamped(c, row++, x, w, "LD flags: %s", target->ldflags);

    if (target->output_group_count && row < y + h - 1) {
        row++;
        print_clamped(c, row++, x, w, "Output groups:");
        for (size_t i = 0; i < target->output_group_count && row < y + h; ++i) {
            print_clamped(c, row++, x + 2, w - 2, "%s -> %s", target->output_groups[i].name, target->output_groups[i].output);
        }
    }
    if (target->app_rule_count && row < y + h - 1) {
        row++;
        print_clamped(c, row++, x, w, "App rules:");
        for (size_t i = 0; i < target->app_rule_count && row < y + h; ++i) {
            print_clamped(c, row++, x + 2, w - 2, "%s", target->app_rules[i].name);
        }
    }
}

int bdt_view_project(const BdtProject *project) {
    CursesApi c;
    if (load_curses(&c) != 0) {
        bdt_log(BDT_LOG_ERROR, "bdt view requires ncurses at runtime (install libncurses)");
        return -1;
    }

    ViewLine lines[BDT_VIEW_MAX_LINES];
    size_t line_count = 0;
    add_line(lines, &line_count, "Project", VIEW_SECTION_PROJECT, NULL);
    add_line(lines, &line_count, "Build files", VIEW_SECTION_BUILD_FILES, NULL);
    add_line(lines, &line_count, "Subprojects", VIEW_SECTION_SUBPROJECTS, NULL);
    add_line(lines, &line_count, "Plugins", VIEW_SECTION_PLUGINS, NULL);
    add_target_lines(project, lines, &line_count);

    c.stdscr = c.initscr();
    c.cbreak();
    c.noecho();
    c.keypad(c.stdscr, 1);
    if (c.curs_set) c.curs_set(0);
    if (c.start_color) {
        c.start_color();
        if (c.use_default_colors) c.use_default_colors();
        if (c.init_pair) {
            c.init_pair(1, 6, -1);
            c.init_pair(2, 2, -1);
            c.init_pair(3, 3, -1);
        }
    }

    int selected = 0;
    int top = 0;
    int running = 1;
    while (running) {
        int rows = 24, cols = 80;
        struct winsize ws;
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
            if (ws.ws_row > 0) rows = ws.ws_row;
            if (ws.ws_col > 0) cols = ws.ws_col;
        }
        const char *lines_env = getenv("LINES");
        const char *cols_env = getenv("COLUMNS");
        if (lines_env && atoi(lines_env) > 0) rows = atoi(lines_env);
        if (cols_env && atoi(cols_env) > 0) cols = atoi(cols_env);
        if (rows < 12) rows = 12;
        if (cols < 60) cols = 60;
        int left_w = cols / 3;
        int detail_w = cols - left_w - 3;
        int list_h = rows - 4;
        if (selected < top) top = selected;
        if (selected >= top + list_h) top = selected - list_h + 1;

        c.clear();
        if (c.attron) c.attron(BDT_A_BOLD | BDT_COLOR_PAIR(1));
        print_clamped(&c, 0, 0, cols, "bdt project view: %s", project->name);
        if (c.attroff) c.attroff(BDT_A_BOLD | BDT_COLOR_PAIR(1));
        print_clamped(&c, 1, 0, cols, "q quit | up/down select | page up/down jump");

        for (int i = 0; i < list_h && top + i < (int)line_count; ++i) {
            int idx = top + i;
            int y = i + 3;
            if (idx == selected && c.attron) c.attron(BDT_A_BOLD | BDT_COLOR_PAIR(2));
            const char *prefix = lines[idx].target ? "  " : "";
            print_clamped(&c, y, 0, left_w, "%s%s", prefix, lines[idx].text);
            if (idx == selected && c.attroff) c.attroff(BDT_A_BOLD | BDT_COLOR_PAIR(2));
        }

        for (int y = 2; y < rows; ++y) c.mvprintw(y, left_w + 1, "|");
        draw_target_detail(&c, project, &lines[selected], left_w + 3, 3, detail_w, rows - 4);
        c.refresh();

        int ch = c.getch();
        if (ch == 'q' || ch == 'Q' || ch == 27) running = 0;
        else if ((ch == BDT_KEY_UP || ch == 'k') && selected > 0) selected--;
        else if ((ch == BDT_KEY_DOWN || ch == 'j') && selected + 1 < (int)line_count) selected++;
        else if (ch == BDT_KEY_NPAGE) {
            selected += list_h;
            if (selected >= (int)line_count) selected = (int)line_count - 1;
        } else if (ch == BDT_KEY_PPAGE) {
            selected -= list_h;
            if (selected < 0) selected = 0;
        }
    }

    c.endwin();
    unload_curses(&c);
    return 0;
}
#endif
