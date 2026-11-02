#define NOB_IMPLEMENTATION
#include "nob.h"

#define BUILD_DIR "build/"
#define BIN_NAME  "tally"

// Everything except main.c.
static const char *lib_sources[] = {
    "src/nob_impl.c",
    "src/money.c",
    "src/date.c",
    "src/db.c",
};

static const char *test_names[] = {
    "test_money",
    "test_db",
    "test_date",
};

static void append_base_flags(Cmd *cmd, bool debug)
{
    nob_cc(cmd);
    nob_cc_flags(cmd);   // -Wall -Wextra
    cmd_append(cmd, "-std=c11", "-Wpedantic");
    cmd_append(cmd, "-D_POSIX_C_SOURCE=200809L");
    // Keep nob.h names prefixed (nob_*, Nob_*) inside the application code.
    cmd_append(cmd, "-DNOB_UNSTRIP_PREFIX");
    cmd_append(cmd, "-I.", "-Isrc");
    cmd_append(cmd, "-I/usr/include/postgresql"); // libpq-fe.h lives here on Ubuntu

    if (debug) {
        cmd_append(cmd, "-g", "-O0", "-fsanitize=address,undefined");
    } else {
        cmd_append(cmd, "-O2");
    }
}

// Adds the library sources
static void append_lib_inputs(Cmd *cmd)
{
    for (size_t i = 0; i < ARRAY_LEN(lib_sources); ++i) {
        cmd_append(cmd, lib_sources[i]);
    }
    cmd_append(cmd, "-lpq");
}

static bool build(bool debug)
{
    if (!mkdir_if_not_exists("build")) return false;

    Cmd cmd = {0};
    append_base_flags(&cmd, debug);
    nob_cc_output(&cmd, BUILD_DIR BIN_NAME);
    cmd_append(&cmd, "src/main.c");
    append_lib_inputs(&cmd);
    return cmd_run(&cmd);
}

static bool run_test(const char *name)
{
    const char *bin = temp_sprintf(BUILD_DIR "%s", name);
    const char *src = temp_sprintf("tests/%s.c", name);

    Cmd cmd = {0};
    append_base_flags(&cmd, true);   // tests run with sanitizers
    nob_cc_output(&cmd, bin);
    cmd_append(&cmd, src);
    append_lib_inputs(&cmd);
    if (!cmd_run(&cmd)) return false;

    cmd_append(&cmd, bin);
    return cmd_run(&cmd);
}

static bool run_tests(void)
{
    if (!mkdir_if_not_exists("build")) return false;

    for (size_t i = 0; i < ARRAY_LEN(test_names); ++i) {
        if (!run_test(test_names[i])) return false;
    }
    return true;
}

int main(int argc, char **argv)
{
    GO_REBUILD_URSELF(argc, argv);

    const char *program = shift(argv, argc);
    UNUSED(program);

    const char *mode = (argc > 0) ? shift(argv, argc) : "release";

    if (strcmp(mode, "release") == 0) return build(false) ? 0 : 1;
    if (strcmp(mode, "debug")   == 0) return build(true)  ? 0 : 1;
    if (strcmp(mode, "test")    == 0) return run_tests()  ? 0 : 1;

    nob_log(ERROR, "unknown mode: %s (use: release | debug | test)", mode);
    return 1;
}