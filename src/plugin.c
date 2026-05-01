#include "bdt.h"

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
int _putenv_s(const char *name, const char *value);
#else
int setenv(const char *name, const char *value, int overwrite);
#endif

static void expand_field(BdtProject *project, const char *in, char *out, size_t out_size) {
    if (!in || !in[0]) {
        if (out_size) out[0] = 0;
        return;
    }
    bdt_expand_vars(project, in, out, out_size);
}

static int run_shell(BdtProject *project, const char *raw) {
    char cmd[BDT_MAX_TEXT * 8];
    bdt_expand_vars(project, raw, cmd, sizeof(cmd));
    bdt_log(BDT_LOG_DEBUG, "%s", cmd);
    int rc = system(cmd);
    return rc == 0 ? 0 : rc;
}

static void set_env_value(const char *name, const char *value) {
#ifdef _WIN32
    _putenv_s(name, value ? value : "");
#else
    setenv(name, value ? value : "", 1);
#endif
}

int bdt_run_plugin_target(BdtProject *project, BdtTarget *target) {
    char plugin_name[128];
    if (target->plugin[0]) snprintf(plugin_name, sizeof(plugin_name), "%s", target->plugin);
    else if (target->type[0] && strncmp(target->type, "plugin:", 7) == 0) snprintf(plugin_name, sizeof(plugin_name), "%s", target->type + 7);
    else {
        bdt_log(BDT_LOG_ERROR, "plugin target '%s' has no plugin", target->name);
        return -1;
    }

    BdtPlugin *plugin = bdt_find_plugin(project, plugin_name);
    if (!plugin) {
        bdt_log(BDT_LOG_ERROR, "unknown plugin '%s' for target '%s'", plugin_name, target->name);
        return -1;
    }

    char runner[1024], path[1024], inputs[BDT_MAX_TEXT], outputs[BDT_MAX_TEXT], sources[BDT_MAX_TEXT];
    char flags[BDT_MAX_TEXT], output[1024], command[BDT_MAX_TEXT * 2];
    expand_field(project, plugin->runner, runner, sizeof(runner));
    expand_field(project, plugin->path, path, sizeof(path));
    expand_field(project, target->inputs, inputs, sizeof(inputs));
    expand_field(project, target->outputs, outputs, sizeof(outputs));
    expand_field(project, target->sources, sources, sizeof(sources));
    expand_field(project, target->flags, flags, sizeof(flags));
    expand_field(project, target->output, output, sizeof(output));

    set_env_value("BDT_PROJECT_ROOT", project->root);
    set_env_value("BDT_PROJECT_NAME", project->name);
    set_env_value("BDT_TARGET", target->name);
    set_env_value("BDT_PLUGIN", plugin->name);
    set_env_value("BDT_INPUTS", inputs);
    set_env_value("BDT_OUTPUTS", outputs);
    set_env_value("BDT_SOURCES", sources);
    set_env_value("BDT_OUTPUT", output);
    set_env_value("BDT_FLAGS", flags);

    if (plugin->command[0]) {
        expand_field(project, plugin->command, command, sizeof(command));
    } else if (runner[0]) {
        if (path[0]) snprintf(command, sizeof(command), "\"%s\" \"%s\"", runner, path);
        else snprintf(command, sizeof(command), "\"%s\"", runner);
    } else {
        bdt_log(BDT_LOG_ERROR, "plugin '%s' has no command or runner", plugin->name);
        return -1;
    }

    return run_shell(project, command);
}
