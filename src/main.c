#include "bdt.h"

#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    BdtCli cli;
    bdt_parse_cli(argc, argv, &cli);

    char root[1024];
    if (bdt_abs_path(root, sizeof(root), ".") != 0) snprintf(root, sizeof(root), ".");

    BdtProject *project = (BdtProject *)calloc(1, sizeof(BdtProject));
    if (!project) return 2;
    if (bdt_load_project(root, cli.project_file, project) != 0) {
        free(project);
        return 2;
    }
    if (project->var_count < BDT_MAX_ITEMS) {
        snprintf(project->vars[project->var_count].key, sizeof(project->vars[project->var_count].key), "jobs");
        snprintf(project->vars[project->var_count].value, sizeof(project->vars[project->var_count].value), "%d", cli.jobs);
        project->var_count++;
    }
    bdt_log_init(project->lang, cli.verbose);

    if (cli.scan) {
        bdt_print_scan(project);
        free(project);
        return 0;
    }
    if (cli.list) {
        bdt_print_targets(project);
        free(project);
        return 0;
    }
    if (cli.graph) {
        bdt_print_graph(project);
        free(project);
        return 0;
    }

    const char *target = cli.target ? cli.target : project->default_target;
    int rc = bdt_run_target(project, target, cli.no_cache) == 0 ? 0 : 1;
    free(project);
    return rc;
}
