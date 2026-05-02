#include "bdt.h"

#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    BdtCli cli;
    bdt_parse_cli(argc, argv, &cli);

    if (cli.log_style_cmd) {
        return bdt_log_style_command(cli.log_style_value) == 0 ? 0 : 1;
    }

    char root[1024];
    if (bdt_abs_path(root, sizeof(root), ".") != 0) snprintf(root, sizeof(root), ".");

    BdtProject *project = (BdtProject *)calloc(1, sizeof(BdtProject));
    if (!project) return 2;
    if (bdt_load_project(root, cli.project_file, project) != 0) {
        free(project);
        return 2;
    }
    if (project->var_count < BDT_MAX_VARS) {
        snprintf(project->vars[project->var_count].key, sizeof(project->vars[project->var_count].key), "jobs");
        snprintf(project->vars[project->var_count].value, sizeof(project->vars[project->var_count].value), "%d", cli.jobs);
        project->var_count++;
    }
    bdt_log_init(project->lang, cli.verbose);

    if (cli.scan) {
        bdt_scan_build_files(project);
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
    if (cli.view) {
        bdt_scan_build_files(project);
        int rc = bdt_view_project(project);
        free(project);
        return rc == 0 ? 0 : 1;
    }
    if (cli.why) {
        int rc = bdt_why_query(project, cli.why_query);
        free(project);
        return rc == 0 ? 0 : 1;
    }
    if (cli.status) {
        const char *name = cli.status_target ? cli.status_target : NULL;
        int rc = bdt_status_project(project, name);
        free(project);
        return rc == 0 ? 0 : 1;
    }
    if (cli.explain) {
        const char *name = cli.explain_target ? cli.explain_target : (cli.target ? cli.target : project->default_target);
        int rc = bdt_explain_target(project, name);
        free(project);
        return rc == 0 ? 0 : 1;
    }
    if (cli.clean_target) {
        const char *name = cli.clean_name ? cli.clean_name : (cli.target ? cli.target : project->default_target);
        int rc = bdt_clean_named_target(project, name);
        free(project);
        return rc == 0 ? 0 : 1;
    }
    if (cli.doctor) {
        int rc = bdt_doctor(project);
        free(project);
        return rc == 0 ? 0 : 1;
    }
    if (cli.cache_cmd) {
        int rc = bdt_cache_command(project, cli.cache_action, cli.cache_arg);
        free(project);
        return rc == 0 ? 0 : 1;
    }

    const char *target = cli.target ? cli.target : project->default_target;
    if (cli.bench && cli.bench_target) target = cli.bench_target;
    if (cli.trace && cli.trace_target) target = cli.trace_target;
    if (cli.trace || cli.bench) {
        char default_trace[1024];
        const char *trace_path = cli.trace_path;
        if (!trace_path) {
            char build_root[1024];
            bdt_path_join(build_root, sizeof(build_root), project->root, project->build_dir);
            bdt_mkdirs(build_root);
            bdt_path_join(default_trace, sizeof(default_trace), build_root, cli.bench ? "bench-trace.json" : "trace.json");
            trace_path = default_trace;
        }
        if (bdt_trace_begin(project, trace_path) != 0) {
            free(project);
            return 1;
        }
        if (cli.bench && project->var_count < BDT_MAX_VARS) {
            snprintf(project->vars[project->var_count].key, sizeof(project->vars[project->var_count].key), "bench_trace");
            snprintf(project->vars[project->var_count].value, sizeof(project->vars[project->var_count].value), "%s", trace_path);
            project->var_count++;
        }
    }
    int rc = bdt_run_target(project, target, cli.no_cache) == 0 ? 0 : 1;
    if (cli.trace || cli.bench) {
        const char *trace_path = cli.trace_path;
        char default_trace[1024];
        if (!trace_path) {
            char build_root[1024];
            bdt_path_join(build_root, sizeof(build_root), project->root, project->build_dir);
            bdt_path_join(default_trace, sizeof(default_trace), build_root, cli.bench ? "bench-trace.json" : "trace.json");
            trace_path = default_trace;
        }
        bdt_trace_end(rc);
        if (cli.bench) bdt_bench_report(trace_path);
    }
    free(project);
    return rc;
}
