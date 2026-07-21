#if defined(__APPLE__)
#include <libproc.h>
#include <mach-o/dyld.h>
#endif
#include "a.h"
#include "shakti.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#if defined __has_include
#if __has_include("shakti_version.h")
#include "shakti_version.h"
#endif
#endif
#ifndef SHAKTI_PKG_VERSION
#define SHAKTI_PKG_VERSION "0.11.0"
#endif
extern int shakti_lang_main(int argc, char **argv);
static void shakti_print_banner(void) {
    char t0[80], t1[80], t2[80];
    snprintf(t0, sizeof t0, "   shakti engine v%s", SHAKTI_PKG_VERSION);
    snprintf(t1, sizeof t1, "   (c) shakti.com - %s", SHAKTI_PKG_VERSION);
    snprintf(t2, sizeof t2, "   \\d docs  \\v vars  \\w names  quit|exit");
    fprintf(stderr, "%s\n%s\n%s\n", t0, t1, t2);
}
static int shakti_flag_is(const char *arg, const char *name, const char *short_name) {
    return !strcmp(arg, name) || (short_name && !strcmp(arg, short_name));
}
static int shakti_parent_is_self(void) {
#if defined(__linux__)
    /* realpath() NUL-terminates and avoids the Level-5 readlink TOCTOU hit. */
    char *self = realpath("/proc/self/exe", NULL);
    char ppath[64];
    snprintf(ppath, sizeof(ppath), "/proc/%d/exe", (int)getppid());
    char *parent = realpath(ppath, NULL);
    int same = (self && parent && !strcmp(self, parent));
    free(self);
    free(parent);
    return same;
#elif defined(__APPLE__)
    char self[4096], parent[4096];
    uint32_t sz = (uint32_t)sizeof(self);
    if (_NSGetExecutablePath(self, &sz) != 0) return 0;
    int ppid = (int)getppid();
    if (proc_pidpath(ppid, parent, sizeof(parent)) <= 0) return 0;
    return !strcmp(self, parent);
#else
    (void)0;
    return 0;
#endif
}
static int shakti_wants_banner(int argc, char **argv) {
    int quiet = 0, force = 0, has_c = 0, interactive = 0, has_script = 0;
    int r = 1;
    W(r<argc&&argv[r][0]=='-',{
        if(shakti_flag_is(argv[r],"--quiet","-q"))quiet=1;
        else if(!strcmp(argv[r],"--banner"))force=1;
        else if(!strcmp(argv[r],"-c")&&r+1<argc){has_c=1;r+=2;continue;}
        else if(!strcmp(argv[r],"-i")){interactive=1;r++;continue;}
        r++;})
    if(r<argc)has_script=1;
    P(quiet,0)
    P(force,1)
    P(has_script||(has_c&&!interactive),0)
    if(getenv("SHAKTI_QUIET")) return 0;
    if(shakti_parent_is_self()) return 0;
    if(!isatty(STDIN_FILENO) && !has_c && !has_script) return 0;
    if(!isatty(STDERR_FILENO)) return 0;
    return 1;}
static void shakti_strip_banner_flags(int *argc, char **argv) {
    int w=1,r;
    for(r=1;r<*argc;r++){
        if(shakti_flag_is(argv[r],"--quiet","-q")||!strcmp(argv[r],"--banner"))continue;
        argv[w++]=argv[r];}
    *argc=w;}
int main(int argc, char **argv) {
    setenv("OMP_PROC_BIND", "true", 0);
    setenv("OMP_PLACES", "cores", 0);
    if(argc>=3&&!strcmp(argv[1],"run")){
        char**av=malloc(sizeof(char*)*(size_t)(argc-1));
        P(!av,1)
        av[0]=argv[0];
        for(int j=2;j<argc;j++)av[j-1]=argv[j];
        int sub_argc=argc-1;
        if(shakti_wants_banner(sub_argc,av))shakti_print_banner();
        shakti_strip_banner_flags(&sub_argc,av);
        int rc=shakti_lang_main(sub_argc,av);
        free(av);
        return rc;}
    if(shakti_wants_banner(argc,argv))shakti_print_banner();
    shakti_strip_banner_flags(&argc,argv);
    return shakti_lang_main(argc,argv);
}
