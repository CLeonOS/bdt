#include "bdt.h"

#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#endif

static void scan_dir(BdtProject *project, const char *dir) {
    char build_file[1024];
    bdt_path_join(build_file, sizeof(build_file), dir, "build.bdt");
    if (bdt_file_exists(build_file) && project->build_file_count < BDT_MAX_ITEMS) {
        snprintf(project->build_files[project->build_file_count++], 1024, "%s", build_file);
        bdt_log(BDT_LOG_DEBUG, "found %s", build_file);
    }

#ifdef _WIN32
    char pattern[1024];
    bdt_path_join(pattern, sizeof(pattern), dir, "*");
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..") || !strcmp(fd.cFileName, ".git") || !strcmp(fd.cFileName, "build")) continue;
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
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..") || !strcmp(e->d_name, ".git") || !strcmp(e->d_name, "build")) continue;
        char next[1024];
        bdt_path_join(next, sizeof(next), dir, e->d_name);
        if (bdt_dir_exists(next)) scan_dir(project, next);
    }
    closedir(d);
#endif
}

int bdt_scan_build_files(BdtProject *project) {
    project->build_file_count = 0;
    scan_dir(project, project->root);
    return 0;
}

void bdt_print_scan(const BdtProject *project) {
    printf("%s:\n", bdt_msg(project->lang, "scan"));
    for (size_t i = 0; i < project->build_file_count; ++i) {
        printf("  %s\n", project->build_files[i]);
    }
}
