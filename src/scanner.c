#include "bdt.h"

#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

static int skip_dir_name(const char *name) {
    return !strcmp(name, ".") || !strcmp(name, "..") || !strcmp(name, ".git") || !strcmp(name, "build") ||
           !strcmp(name, ".bdt") || !strcmp(name, "node_modules") || !strcmp(name, "target") ||
           !strcmp(name, "out") || !strcmp(name, "dist") || !strcmp(name, ".cache") ||
           !strcmp(name, "__pycache__");
}

static void record_build_file(BdtProject *project, const char *dir) {
    char build_file[1024];
    bdt_path_join(build_file, sizeof(build_file), dir, "build.bdt");
    if (bdt_file_exists(build_file) && project->build_file_count < BDT_MAX_BUILD_FILES) {
        snprintf(project->build_files[project->build_file_count++], 1024, "%s", build_file);
        bdt_log(BDT_LOG_DEBUG, "found %s", build_file);
    }
}

static void scan_dir(BdtProject *project, const char *dir) {
    record_build_file(project, dir);
    if (strcmp(dir, project->root)) {
        char git_marker[1024];
        bdt_path_join(git_marker, sizeof(git_marker), dir, ".git");
        if (bdt_file_exists(git_marker) || bdt_dir_exists(git_marker)) return;
    }

#ifdef _WIN32
    char pattern[1024];
    bdt_path_join(pattern, sizeof(pattern), dir, "*");
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (skip_dir_name(fd.cFileName)) continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            char next[1024];
            bdt_path_join(next, sizeof(next), dir, fd.cFileName);
            scan_dir(project, next);
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (skip_dir_name(e->d_name)) continue;
        char next[1024];
        bdt_path_join(next, sizeof(next), dir, e->d_name);
        int is_dir = 0;
#ifdef DT_DIR
        if (e->d_type == DT_DIR) is_dir = 1;
        else if (e->d_type == DT_UNKNOWN)
#endif
        {
            struct stat st;
            is_dir = stat(next, &st) == 0 && S_ISDIR(st.st_mode);
        }
        if (is_dir) scan_dir(project, next);
    }
    closedir(d);
#endif
}

int bdt_scan_build_files(BdtProject *project) {
    project->build_file_count = 0;
    if (project->subproject_count > 0) {
        record_build_file(project, project->root);
        for (size_t i = 0; i < project->subproject_count; ++i) {
            char path[1024];
            bdt_path_join(path, sizeof(path), project->root, project->subprojects[i]);
            record_build_file(project, path);
        }
        return 0;
    }
    scan_dir(project, project->root);
    return 0;
}

void bdt_print_scan(const BdtProject *project) {
    printf("%s:\n", bdt_msg(project->lang, "scan"));
    for (size_t i = 0; i < project->build_file_count; ++i) {
        printf("  %s\n", project->build_files[i]);
    }
}
