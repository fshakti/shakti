#include "a.h"
#include "shakti.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
extern int shakti_lang_main(int argc, char **argv);
static const char *shakti_banner_qr[] = {
"▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄",
"█ ▄▄▄▄▄ ██▄▄ ▀   ▀█ ▄▄▄▄▄ █",
"█ █   █ █▄█▀▀▄██▄██ █   █ █",
"█ █▄▄▄█ ██▀▀ █▄  ▀█ █▄▄▄█ █",
"█▄▄▄▄▄▄▄█ ▀▄▀▄▀ █▄█▄▄▄▄▄▄▄█",
"█▄  █ ▄▄ ▄▄█▄  ██   ███▄█▀█",
"█▀█▄█ ▄▄▄▀▀ ██ ▀█ ██▄██▄ ▄█",
"█▄▀ ▀▀▄▄█▄▀▄█▀ █▄▄▄ ██ █▄ █",
"█▄▀▀ ▀ ▄█▄▄▀█▀▀▀█▄ ▄▄▀█▄ ▄█",
"█▄█▄█▄█▄█▀ █▄▄▄█▀ ▄▄▄  █▀▀█",
"█ ▄▄▄▄▄ █  ▀█▄ ▀▄ █▄█  █ ▄█",
"█ █   █ █▄█▀█▀ ██▄▄▄   ▀▀ █",
"█ █▄▄▄█ █ ▀█▀█  ▄ ▀▀█ ▀█▀▄█",
"█▄▄▄▄▄▄▄█▄█▄█▄▄█▄▄█▄▄███▄▄█"
};
#define SHAKTI_BANNER_QR_N (sizeof shakti_banner_qr / sizeof shakti_banner_qr[0])
#define SHAKTI_BANNER_COL 41
#define SHAKTI_QR_ORANGE "\033[38;2;237;124;58m"
#define SHAKTI_QR_BLUE   "\033[48;2;42;71;245m"
#define SHAKTI_QR_RST    "\033[0m"
static const char *shakti_qr_next(const char *s) {
    unsigned char c = (unsigned char)*s;
    if (!c) return s;
    if (c < 0x80) return s + 1;
    if ((c & 0xE0) == 0xC0) return s + 2;
    if ((c & 0xF0) == 0xE0) return s + 3;
    if ((c & 0xF8) == 0xF0) return s + 4;
    return s + 1;
}
static void shakti_print_qr_line(const char *line,int row) {
    for (const char *p = line; *p; p = shakti_qr_next(p)) {
        if (*p == ' ') fprintf(stderr, SHAKTI_QR_BLUE " " SHAKTI_QR_RST);
        else {
            char ch[8];
            const char *q = shakti_qr_next(p);
            size_t n = (size_t)(q - p);
            if (n >= sizeof ch) n = sizeof ch - 1;
            memcpy(ch, p, n);
            ch[n] = 0;
            if(row)fprintf(stderr, SHAKTI_QR_ORANGE SHAKTI_QR_BLUE "%s" SHAKTI_QR_RST, ch);
            else fprintf(stderr, SHAKTI_QR_ORANGE "%s" SHAKTI_QR_RST, ch);
        }
    }
}
static void shakti_print_banner(void) {
static char buildstamp[11]={ __DATE__[7], __DATE__[8], __DATE__[9], __DATE__[10], '.',
__DATE__[0] == 'O' || __DATE__[0] == 'N' || __DATE__[0] == 'D' ? '1' : '0',
__DATE__[0] == 'F' || __DATE__[0] == 'D' ? '2' : __DATE__[0] == 'M' ? ( __DATE__[2] == 'r' ? '3' : '5') : __DATE__[0] == 'A' ? ( __DATE__[2] == 'g' ? '8' : '4') : __DATE__[0] == 'S' ? '9' : __DATE__[0] == 'O' ? '0' : __DATE__[0] == 'N' || __DATE__[1] == 'a' ? '1' : __DATE__[2] == 'l' ? '7' : '6',
'.', __DATE__[4] == ' ' ? '0' : __DATE__[4], __DATE__[5],0};

    char t0[80], t1[80], t2[80];

    snprintf(t0, sizeof t0, "   shakti engine %s", buildstamp);
    snprintf(t1, sizeof t1, "   (c) shakti.com and contributors");
    snprintf(t2, sizeof t2, "   \\d docs  \\v vars  \\w names  quit|exit");
    const char *txt[] = { t0, t1, t2 };
    int txt_n = 3;
    int rows = (int)SHAKTI_BANNER_QR_N > txt_n ? (int)SHAKTI_BANNER_QR_N : txt_n;
    for (int i = 0; i < rows; i++) {
        if (i < txt_n) fprintf(stderr, "%-*s", SHAKTI_BANNER_COL, txt[i]);
        else fprintf(stderr, "%*s", SHAKTI_BANNER_COL, "");
        if (i < (int)SHAKTI_BANNER_QR_N) {
            fputc(' ', stderr);
            shakti_print_qr_line(shakti_banner_qr[i],i);
        }
        fprintf(stderr, "\n");
    }
}
static int shakti_flag_is(const char *arg, const char *name, const char *short_name) {
    return !strcmp(arg, name) || (short_name && !strcmp(arg, short_name));
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
    P(has_script&&!has_c&&!interactive,0)
    return 1;}
static void shakti_strip_banner_flags(int *argc, char **argv) {
    int w=1,r;
    for(r=1;r<*argc;r++){
        if(shakti_flag_is(argv[r],"--quiet","-q")||!strcmp(argv[r],"--banner"))continue;
        argv[w++]=argv[r];}
    *argc=w;}
int start_dir;
int main(int argc, char **argv) {
    start_dir=open(".",O_RDONLY|O_DIRECTORY);
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
