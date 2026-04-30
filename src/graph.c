#include "bdt.h"

#include <string.h>

static int run_one(BdtProject *project, BdtTarget *target, int no_cache) {
    if (target->done) return 0;
    if (target->active) {
        bdt_log(BDT_LOG_ERROR, "dependency cycle at target '%s'", target->name);
        return -1;
    }
    target->active = 1;
    char deps[BDT_MAX_ITEMS][512];
    int dep_count = bdt_split_list(target->deps, deps, BDT_MAX_ITEMS);
    for (int i = 0; i < dep_count; ++i) {
        BdtTarget *dep = bdt_find_target(project, deps[i]);
        if (!dep) {
            bdt_log(BDT_LOG_ERROR, "unknown dependency '%s' of target '%s'", deps[i], target->name);
            return -1;
        }
        int rc = run_one(project, dep, no_cache);
        if (rc != 0) return rc;
    }
    int rc = bdt_run_command_target(project, target, no_cache);
    target->active = 0;
    target->done = rc == 0;
    return rc;
}

int bdt_run_target(BdtProject *project, const char *name, int no_cache) {
    BdtTarget *target = bdt_find_target(project, name);
    if (!target) {
        bdt_log(BDT_LOG_ERROR, "unknown target '%s'", name);
        return -1;
    }
    bdt_log(BDT_LOG_INFO, "%s: %s", bdt_msg(project->lang, "build_start"), name);
    int rc = run_one(project, target, no_cache);
    if (rc == 0) bdt_log(BDT_LOG_INFO, "%s: %s", bdt_msg(project->lang, "build_done"), name);
    return rc;
}

void bdt_print_graph(const BdtProject *project) {
    printf("digraph bdt {\n");
    for (size_t i = 0; i < project->target_count; ++i) {
        char deps[BDT_MAX_ITEMS][512];
        int dep_count = bdt_split_list(project->targets[i].deps, deps, BDT_MAX_ITEMS);
        if (dep_count == 0) printf("  \"%s\";\n", project->targets[i].name);
        for (int d = 0; d < dep_count; ++d) printf("  \"%s\" -> \"%s\";\n", deps[d], project->targets[i].name);
    }
    printf("}\n");
}
