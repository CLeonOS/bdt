#include "bdt.h"

#include <stdlib.h>
#include <string.h>

static int run_shell(BdtProject *project, const char *raw) {
    char cmd[BDT_MAX_TEXT * 8];
    bdt_expand_vars(project, raw, cmd, sizeof(cmd));
    bdt_log(BDT_LOG_DEBUG, "%s", cmd);
    int rc = system(cmd);
    return rc == 0 ? 0 : rc;
}

static int target_cache_check(BdtProject *project, BdtTarget *target, int no_cache, uint64_t *hash_out) {
    uint64_t h = bdt_hash_target_inputs(project, target);
    if (hash_out) *hash_out = h;
    if (!no_cache && target->cache && !target->always && bdt_cache_is_fresh(project, target, h)) {
        bdt_log(BDT_LOG_INFO, "%s: %s", bdt_msg(project->lang, "cache_hit"), target->name);
        return 1;
    }
    return 0;
}

static int run_command(BdtProject *project, BdtTarget *target) {
    if (!target->command[0]) return 0;
    return run_shell(project, target->command);
}

static int run_group(BdtProject *project, BdtTarget *target) {
    char steps[BDT_MAX_ITEMS][512];
    int count = bdt_split_delim(target->steps, '|', steps, BDT_MAX_ITEMS);
    for (int i = 0; i < count; ++i) {
        char expanded[BDT_MAX_TEXT];
        bdt_expand_vars(project, steps[i], expanded, sizeof(expanded));
        if (run_shell(project, expanded) != 0) return -1;
    }
    return 0;
}

static int run_mkdir(BdtProject *project, BdtTarget *target) {
    char paths[BDT_MAX_ITEMS][512];
    int count = bdt_split_list(target->outputs[0] ? target->outputs : target->output, paths, BDT_MAX_ITEMS);
    for (int i = 0; i < count; ++i) {
        char path[1024];
        bdt_expand_vars(project, paths[i], path, sizeof(path));
        if (bdt_mkdirs(path) != 0) return -1;
    }
    return 0;
}

static int run_remove(BdtProject *project, BdtTarget *target) {
    char paths[BDT_MAX_ITEMS][512];
    int count = bdt_split_list(target->inputs[0] ? target->inputs : target->output, paths, BDT_MAX_ITEMS);
    for (int i = 0; i < count; ++i) {
        char path[1024], cmd[BDT_MAX_TEXT];
        bdt_expand_vars(project, paths[i], path, sizeof(path));
#ifdef _WIN32
        snprintf(cmd, sizeof(cmd), "cmd /C if exist \"%s\" (if exist \"%s\\*\" rmdir /S /Q \"%s\" else del /F /Q \"%s\")", path, path, path, path);
#else
        snprintf(cmd, sizeof(cmd), "rm -rf \"%s\"", path);
#endif
        if (run_shell(project, cmd) != 0) return -1;
    }
    return 0;
}

static int run_copy(BdtProject *project, BdtTarget *target) {
    char srcs[BDT_MAX_ITEMS][512];
    int count = bdt_split_list(target->inputs, srcs, BDT_MAX_ITEMS);
    char output[1024];
    bdt_expand_vars(project, target->output, output, sizeof(output));
    if (count > 1) bdt_mkdirs(output);
    for (int i = 0; i < count; ++i) {
        char src[1024], dst[1024], cmd[BDT_MAX_TEXT];
        bdt_expand_vars(project, srcs[i], src, sizeof(src));
        if (count == 1) {
            snprintf(dst, sizeof(dst), "%s", output);
        } else {
            const char *base = strrchr(src, '/');
            const char *base2 = strrchr(src, '\\');
            if (!base || base2 > base) base = base2;
            base = base ? base + 1 : src;
            bdt_path_join(dst, sizeof(dst), output, base);
        }
#ifdef _WIN32
        snprintf(cmd, sizeof(cmd), "cmd /C copy /Y \"%s\" \"%s\" >NUL", src, dst);
#else
        snprintf(cmd, sizeof(cmd), "cp -f \"%s\" \"%s\"", src, dst);
#endif
        if (run_shell(project, cmd) != 0) return -1;
    }
    return 0;
}

static int run_tar(BdtProject *project, BdtTarget *target) {
    char dir[1024], out[1024], cmd[BDT_MAX_TEXT];
    const char *tar = target->tool[0] ? target->tool : "{TAR}";
    bdt_expand_vars(project, target->inputs, dir, sizeof(dir));
    bdt_expand_vars(project, target->output, out, sizeof(out));
    snprintf(cmd, sizeof(cmd), "%s -C \"%s\" -cf \"%s\" .", tar, dir, out);
    return run_shell(project, cmd);
}

static int run_truncate(BdtProject *project, BdtTarget *target) {
    char out[1024], cmd[BDT_MAX_TEXT];
    bdt_expand_vars(project, target->output, out, sizeof(out));
    if (bdt_file_exists(out)) return 0;
#ifdef _WIN32
    snprintf(cmd, sizeof(cmd), "fsutil file createnew \"%s\" %s", out, target->flags[0] ? target->flags : "67108864");
#else
    snprintf(cmd, sizeof(cmd), "dd if=/dev/zero of=\"%s\" bs=1M count=%s", out, target->flags[0] ? target->flags : "64");
#endif
    return run_shell(project, cmd);
}

static int run_compile(BdtProject *project, BdtTarget *target) {
    char srcs[BDT_MAX_ITEMS][512];
    int count = bdt_split_list(target->sources, srcs, BDT_MAX_ITEMS);
    char out_dir[1024];
    bdt_expand_vars(project, target->output, out_dir, sizeof(out_dir));
    bdt_mkdirs(out_dir);
    for (int i = 0; i < count; ++i) {
        char src[1024], obj[1024], rel[512], flags[BDT_MAX_TEXT], cmd[BDT_MAX_TEXT * 2];
        char expanded_src[1024];
        bdt_expand_vars(project, srcs[i], expanded_src, sizeof(expanded_src));
        if (strchr(expanded_src, ':') || expanded_src[0] == '/' || expanded_src[0] == '\\') {
            snprintf(src, sizeof(src), "%s", expanded_src);
        } else {
            bdt_path_join(src, sizeof(src), project->root, expanded_src);
        }
        snprintf(rel, sizeof(rel), "%s", srcs[i]);
        for (char *p = rel; *p; ++p) {
            if (*p == '\\') *p = '/';
        }
        char *dot = strrchr(rel, '.');
        if (dot) strcpy(dot, ".o");
        bdt_path_join(obj, sizeof(obj), out_dir, rel);
        char obj_dir[1024];
        snprintf(obj_dir, sizeof(obj_dir), "%s", obj);
        char *cut = strrchr(obj_dir, '/');
        char *cut2 = strrchr(obj_dir, '\\');
        if (!cut || cut2 > cut) cut = cut2;
        if (cut) {
            *cut = 0;
            bdt_mkdirs(obj_dir);
        }
        const char *tool = target->tool[0] ? target->tool : "{CC}";
        const char *ext = strrchr(src, '.');
        const char *base_flags = target->flags;
        if (ext && !strcmp(ext, ".cpp") && target->cxxflags[0]) base_flags = target->cxxflags;
        else if (ext && !strcmp(ext, ".S") && target->asflags[0]) base_flags = target->asflags;
        else if (target->cflags[0]) base_flags = target->cflags;
        bdt_expand_list_vars(project, base_flags, flags, sizeof(flags));
        snprintf(cmd, sizeof(cmd), "%s %s -c \"%s\" -o \"%s\"", tool, flags, src, obj);
        bdt_log_progress((size_t)i + 1, (size_t)count, srcs[i]);
        if (run_shell(project, cmd) != 0) return -1;
    }
    return 0;
}

static int collect_objects_from_dir(char *out, size_t out_size, const char *dir);

static int run_rust_staticlib(BdtProject *project, BdtTarget *target) {
    char src[1024], out[1024], flags[BDT_MAX_TEXT], cmd[BDT_MAX_TEXT * 2];
    const char *tool = target->tool[0] ? target->tool : "{RUSTC}";
    bdt_expand_vars(project, target->sources, src, sizeof(src));
    bdt_expand_vars(project, target->output, out, sizeof(out));
    bdt_expand_list_vars(project, target->flags[0] ? target->flags : "--crate-type staticlib,-C,panic=abort,-O", flags, sizeof(flags));
    snprintf(cmd, sizeof(cmd), "%s %s \"%s\" -o \"%s\"", tool, flags, src, out);
    return run_shell(project, cmd);
}

static int run_link(BdtProject *project, BdtTarget *target) {
    char objs[BDT_MAX_TEXT * 4], out[1024], flags[BDT_MAX_TEXT], script[1024], cmd[BDT_MAX_TEXT * 8];
    objs[0] = 0;
    if (target->inputs[0]) {
        char dir[1024];
        bdt_expand_vars(project, target->inputs, dir, sizeof(dir));
        collect_objects_from_dir(objs, sizeof(objs), dir);
    }
    if (target->objects[0]) {
        char explicit_objs[BDT_MAX_TEXT * 2];
        bdt_expand_list_vars(project, target->objects, explicit_objs, sizeof(explicit_objs));
        strncat(objs, " ", sizeof(objs) - strlen(objs) - 1);
        strncat(objs, explicit_objs, sizeof(objs) - strlen(objs) - 1);
    }
    bdt_expand_vars(project, target->output, out, sizeof(out));
    bdt_expand_list_vars(project, target->ldflags[0] ? target->ldflags : target->flags, flags, sizeof(flags));
    bdt_expand_vars(project, target->linker_script, script, sizeof(script));
    const char *tool = target->tool[0] ? target->tool : "{LD}";
    if (script[0]) {
        snprintf(cmd, sizeof(cmd), "%s %s -T \"%s\" -o \"%s\" %s", tool, flags, script, out, objs);
    } else {
        snprintf(cmd, sizeof(cmd), "%s %s -o \"%s\" %s", tool, flags, out, objs);
    }
    return run_shell(project, cmd);
}

#ifdef _WIN32
#include <windows.h>
static int collect_objects_from_dir(char *out, size_t out_size, const char *dir) {
    char pattern[1024];
    bdt_path_join(pattern, sizeof(pattern), dir, "*");
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..")) continue;
        char path[1024];
        bdt_path_join(path, sizeof(path), dir, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            collect_objects_from_dir(out, out_size, path);
        } else if (strstr(fd.cFileName, ".o")) {
            strncat(out, " \"", out_size - strlen(out) - 1);
            strncat(out, path, out_size - strlen(out) - 1);
            strncat(out, "\"", out_size - strlen(out) - 1);
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return 0;
}
#else
#include <dirent.h>
static int collect_objects_from_dir(char *out, size_t out_size, const char *dir) {
    DIR *d = opendir(dir);
    if (!d) return 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        char path[1024];
        bdt_path_join(path, sizeof(path), dir, e->d_name);
        if (bdt_dir_exists(path)) {
            collect_objects_from_dir(out, out_size, path);
        } else if (strstr(e->d_name, ".o")) {
            strncat(out, " \"", out_size - strlen(out) - 1);
            strncat(out, path, out_size - strlen(out) - 1);
            strncat(out, "\"", out_size - strlen(out) - 1);
        }
    }
    closedir(d);
    return 0;
}
#endif

int bdt_run_command_target(BdtProject *project, BdtTarget *target, int no_cache) {
    uint64_t h = 0;
    if (target_cache_check(project, target, no_cache, &h)) return 0;

    const char *type = target->type[0] ? target->type : "command";
    bdt_log(BDT_LOG_STEP, "%s: %s (%s)", bdt_msg(project->lang, "target"), target->name, type);

    int rc = 0;
    if (!strcmp(type, "command")) rc = run_command(project, target);
    else if (!strcmp(type, "group")) rc = run_group(project, target);
    else if (!strcmp(type, "mkdir")) rc = run_mkdir(project, target);
    else if (!strcmp(type, "remove")) rc = run_remove(project, target);
    else if (!strcmp(type, "copy")) rc = run_copy(project, target);
    else if (!strcmp(type, "tar")) rc = run_tar(project, target);
    else if (!strcmp(type, "truncate")) rc = run_truncate(project, target);
    else if (!strcmp(type, "compile")) rc = run_compile(project, target);
    else if (!strcmp(type, "rust-staticlib")) rc = run_rust_staticlib(project, target);
    else if (!strcmp(type, "link")) rc = run_link(project, target);
    else if (!strcmp(type, "c-apps")) rc = bdt_run_c_apps_target(project, target);
    else {
        bdt_log(BDT_LOG_ERROR, "unknown target type '%s' for '%s'", type, target->name);
        return -1;
    }

    if (rc != 0) {
        bdt_log(BDT_LOG_ERROR, "target '%s' failed: %d", target->name, rc);
        return rc;
    }
    if (target->cache) bdt_cache_store(project, target, h);
    return 0;
}
