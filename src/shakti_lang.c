#include "shakti_internal.h"
#if defined __has_include
#if __has_include("shakti_version.h")
#include "shakti_version.h"
#endif
#endif
#ifndef SHAKTI_PKG_VERSION
#define SHAKTI_PKG_VERSION "0.13.0"
#endif
#if defined(_WIN32) && defined(_MSC_VER)
#include <io.h>
#ifndef STDIN_FILENO
#define STDIN_FILENO 0
#endif
#else
#include <unistd.h>
#endif

#ifdef _WIN32
FILE *win_open_memstream(char **ptr, size_t *sizeloc) {
    if(ptr) *ptr = NULL;
    if(sizeloc) *sizeloc = 0;
    return tmpfile();
}
void win_close_memstream(FILE *fp, char **ptr, size_t *sizeloc) {
    Pv(!fp)
    if (ptr) *ptr = NULL;
    if (sizeloc) *sizeloc = 0;
    long sz = ftell(fp);
    if (sz < 0) {
        fclose(fp);
        return;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return;
    }
    char *buf = malloc((size_t)sz + 1);
    if (!buf) {
        fclose(fp);
        return;
    }
    size_t got = fread(buf, 1, (size_t)sz, fp);
    buf[got] = '\0';
    fclose(fp);
    if (ptr)
        *ptr = buf;
    else
        free(buf);
    if (sizeloc) *sizeloc = got;
}
#endif
static char *read_file(const char *path) {
    FILE *f = fopen(path, "r");
    if(!f) { fprintf(stderr, "cannot open %s\n", path); return NULL; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    /* Guard against ftell failure (e.g. path is a directory): a negative sz
     * would make buf[sz] a heap underflow and fread read (size_t)-1 bytes. */
    if (sz < 0) { fprintf(stderr, "cannot read %s\n", path); fclose(f); return NULL; }
    char *buf = malloc((size_t)sz + 2);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)sz, f);
    buf[got] = '\n'; buf[got+1] = 0;
    fclose(f);
    return buf;
}
#ifndef SHAKTI_NO_MAIN
static void shakti_print_usage(FILE *out) {
    fprintf(out,
        "Usage:\n"
        "  shakti [options] [script [args...]]\n"
        "  shakti [options] -c|--command <code> [-i|--interactive]\n"
        "  shakti\n"
        "\n"
        "Options:\n"
        "  -h, --help                     Show this help and exit\n"
        "  -V, --version                  Show version and exit\n"
        "  -q, --quiet                    Suppress startup banner\n"
        "  -b, --banner                   Force startup banner\n"
        "  -c, --command <code>           Evaluate a code string\n"
        "  -i, --interactive              Enter REPL after --command\n"
        "      --parse-dump               Dump parse AST and exit\n"
        "      --parse-profile            Microbench the parser and exit\n"
        "      --parse-profile-iters <n>  Iterations for --parse-profile\n"
        "      --                         End of options\n"
        "\n"
        "REPL (bare shakti / -i): \\d docs  \\v vars  \\w names  \\q [N]  quit|exit\n");
}
int shakti_lang_main(int argc, char **argv) {
#if defined(__linux__) && !defined(__EMSCRIPTEN__)
    {
        /* Prefer realpath over readlink to avoid Level-5 TOCTOU findings. */
        char *exe = realpath("/proc/self/exe", NULL);
        if (exe) {
            char *slash = strrchr(exe, '/');
            if (slash) {
                *slash = 0;
                /* Binaries under .build/ (or build/) load modules from the
                 * sibling ../lib next to that build dir — same layout as when
                 * the CLI lived at the repo root. */
                {
                    char *leaf_slash = strrchr(exe, '/');
                    const char *leaf = leaf_slash ? leaf_slash + 1 : exe;
                    int under_build = leaf_slash &&
                        (!strcmp(leaf, ".build") || !strcmp(leaf, "build"));
                    if (under_build)
                        snprintf(g_lib_path, sizeof(g_lib_path), "%.*s/lib",
                                 (int)(leaf_slash - exe), exe);
                    else
                        snprintf(g_lib_path, sizeof(g_lib_path), "%s/lib", exe);
                }
            }
            free(exe);
        }
    }
#endif
    Env *global = env_new(NULL);
    builtin_register(global);
    int i = 1;
    char *cmd = NULL;
    int interactive = 0;
    int parse_dump = 0;
    int parse_profile = 0;
    int parse_profile_iters = 100000;
    while(i < argc && argv[i][0] == '-') {
        if(!strcmp(argv[i], "--")) {
            i++;
            break;
        } else if(!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            shakti_print_usage(stdout);
            env_free(global);
            return 0;
        } else if(!strcmp(argv[i], "-V") || !strcmp(argv[i], "--version")) {
            printf("shakti %s\n", SHAKTI_PKG_VERSION);
            env_free(global);
            return 0;
        } else if(!strcmp(argv[i], "-c") || !strcmp(argv[i], "--command")) {
            if(i+1 >= argc) {
                fprintf(stderr, "shakti: option '%s' requires an argument\n", argv[i]);
                shakti_print_usage(stderr);
                env_free(global);
                return 2;
            }
            cmd = argv[++i];
        } else if(!strcmp(argv[i], "-i") || !strcmp(argv[i], "--interactive")) {
            interactive = 1;
        } else if(!strcmp(argv[i], "--parse-dump")) {
            parse_dump = 1;
        } else if(!strcmp(argv[i], "--parse-profile")) {
            parse_profile = 1;
        } else if(!strcmp(argv[i], "--parse-profile-iters")) {
            if(i+1 >= argc) {
                fprintf(stderr, "shakti: option '%s' requires an argument\n", argv[i]);
                shakti_print_usage(stderr);
                env_free(global);
                return 2;
            }
            parse_profile_iters = atoi(argv[++i]);
            if (parse_profile_iters < 1) parse_profile_iters = 1;
        } else if(!strcmp(argv[i], "-q") || !strcmp(argv[i], "--quiet") ||
                  !strcmp(argv[i], "-b") || !strcmp(argv[i], "--banner")) {
            /* Banner flags are handled in cli_main; ignore if still present. */
        } else {
            fprintf(stderr, "shakti: unknown option '%s'\n", argv[i]);
            shakti_print_usage(stderr);
            env_free(global);
            return 2;
        }
        i++;
    }

    if (parse_profile) {
        const char *src = cmd;
        char *file_buf = NULL;
        if (!src && i < argc) {
            file_buf = read_file(argv[i]);
            if (!file_buf) return 1;
            src = file_buf;
        }
        if (!src) src = "x = 1 2 3\ny = abs -1.2\n";
        clock_t t0 = clock();
        for (int k = 0; k < parse_profile_iters; k++) {
            Node *prog = parse(src);
            node_free(prog);
        }
        double sec = (double)(clock() - t0) / (double)CLOCKS_PER_SEC;
        printf("parse_profile: %d iters in %.3fs (%.0f parses/sec)\n",
               parse_profile_iters, sec, (double)parse_profile_iters / sec);
        free(file_buf);
        env_free(global);
        return 0;
    }

    if (parse_dump) {
        const char *src = cmd;
        char *file_buf = NULL;
        if (!src && i < argc) {
            file_buf = read_file(argv[i]);
            if (!file_buf) return 1;
            src = file_buf;
        }
        if (!src) {
            fprintf(stderr, "usage: shakti --parse-dump -c 'expr' | script.ie\n");
            env_free(global);
            return 1;
        }
        Node *prog = parse(src);
        if (prog && prog->nch > 0)
            node_sprint(prog->ch[prog->nch - 1], stdout);
        else
            node_sprint(prog, stdout);
        putchar('\n');
        node_free(prog);
        free(file_buf);
        env_free(global);
        return 0;
    }

    if(cmd) {
        Node *prog = parse(cmd);
        V *r = eval(prog, global);
        if(g_error && g_error_val) { fprintf(stderr, "Error: %s\n", g_error_val->s); v_free(g_error_val); g_error_val=NULL; }
        if(!shakti_prog_silent_last(prog) && r && r->t != T_NIL && r->t != T_ERR) {
            print_val(r, stdout, 1);
            putchar('\n');
        }
        v_free(r);
        node_free(prog);
        if(interactive) run_repl(global);
    } else if(i < argc) {
        strncpy(g_script_dir, argv[i], sizeof(g_script_dir)-1);
        g_script_dir[sizeof(g_script_dir)-1] = 0;
        char *slash = strrchr(g_script_dir, '/');
        if(slash) *slash = 0; else { g_script_dir[0] = '.'; g_script_dir[1] = 0; }
        {
            /* Script argv[0..]: script path then remaining CLI args. */
            int narg = argc - i;
            V *av = v_list(narg);
            for (int k = 0; k < narg; k++) av->L[k] = v_str(argv[i + k]);
            env_set(global, "argv", av);
            v_free(av);
        }
        char *src = read_file(argv[i]);
        P(!src,1)
        Node *prog = parse(src);
        V *r = eval(prog, global);
        int script_err = g_error || (r && r->t == T_ERR);
        if(g_error && g_error_val) { fprintf(stderr, "Error: %s\n", g_error_val->s); v_free(g_error_val); g_error_val=NULL; }
        if(r && r->t == T_ERR) fprintf(stderr, "Error: %s\n", r->s);
        v_free(r);
        node_free(prog);
        free(src);
        env_free(global);
        return script_err ? 1 : 0;
    } else {
        run_repl(global);
    }
    env_free(global);
    return 0;
}
#endif
