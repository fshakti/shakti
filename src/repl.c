/* shakti/src/repl.c — interactive loop, highlighting, docs */
#include "shakti_internal.h"
#include "input.h"
#include <errno.h>
#include <limits.h>
#include <unistd.h>
#if defined __has_include
#if __has_include("shakti_version.h")
#include "shakti_version.h"
#endif
#endif
#ifndef SHAKTI_PKG_VERSION
#define SHAKTI_PKG_VERSION "0.13.0"
#endif

static int shakti_stmt_silent(Node *s) {
    P(!s,1)
    switch (s->type) {
    case N_ASSIGN:
    case N_AUGASSIGN:
    case N_DEL:
    case N_IMPORT:
    case N_PASS:
    case N_GLOBAL:
    case N_DEF:
    case N_CLASS:
    case N_BREAK:
    case N_CONTINUE:
    case N_RETURN:
    case N_RAISE:
        return 1;
    case N_BLOCK:
        return s->nch > 0 ? shakti_stmt_silent(s->ch[s->nch - 1]) : 1;
    default:
        return 0;
    }
}
int shakti_prog_silent_last(Node *prog) {
    P(!prog || prog->type != N_BLOCK || prog->nch <= 0,0)
    return shakti_stmt_silent(prog->ch[prog->nch - 1]);
}

static int needs_more(const char *line) {
    size_t len = strlen(line);
    P(len == 0,0)
    P(line[len-1] == ':',1)
    P(line[len-1] == '\\',1)
    int parens=0, brackets=0, braces=0;
    for(size_t i=0;i<len;i++) {
        if(line[i]=='(') parens++; else if(line[i]==')') parens--;
        if(line[i]=='[') brackets++; else if(line[i]==']') brackets--;
        if(line[i]=='{') braces++; else if(line[i]=='}') braces--;
    }
    return (parens>0 || brackets>0 || braces>0);
}
static int repl_line_blank(const char *s) {
    while (*s == ' ' || *s == '\t') s++;
    return *s == 0;
}
static int repl_line_indent(const char *s) {
    int n = 0;
    while (*s == ' ') { n++; s++; }
    while (*s == '\t') { n += 4; s++; }
    return n;
}
static int repl_last_line(const char *buf, size_t n, const char **start, size_t *ln) {
    const char *end;
    P(n == 0,0)
    end = buf + n;
    if (end[-1] == '\n') end--;
    *start = end;
    while (*start > buf && (*start)[-1] != '\n') (*start)--;
    *ln = (size_t)(end - *start);
    return 1;
}
static int repl_buffer_needs_more(const char *buf, size_t n) {
    const char *s;
    size_t ln;
    char tmp[65536];
    P(!repl_last_line(buf, n, &s, &ln),0)
    if (ln >= sizeof tmp) ln = sizeof tmp - 1;
    memcpy(tmp, s, ln);
    tmp[ln] = 0;
    P(repl_line_blank(tmp),0)
    P(needs_more(tmp),1)
    return repl_line_indent(tmp) > 0;
}
static int repl_prefill_spaces(const char *buf, size_t n) {
    const char *s;
    size_t ln;
    int ind;
    P(!repl_last_line(buf, n, &s, &ln) || ln == 0,0)
    ind = repl_line_indent(s);
    if (s[ln - 1] == ':') return ind + 4;
    return ind;
}
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
#include <termios.h>
#include <unistd.h>
#ifdef SHAKTI_HAVE_SYNTH
#include "synth.h"
#include <sys/select.h>
#endif
#define SHAKTI_HL 1
#else
#define SHAKTI_HL 0
#endif
#define HL_RST  "\033[0m"
#define HL_KW   "\033[1;34m"
#define HL_BI   "\033[36m"
#define HL_STR  "\033[32m"
#define HL_NUM  "\033[33m"
#define HL_CMT  "\033[90m"
#define HL_CON  "\033[35m"
#define HL_DEC  "\033[33m"
#define HL_QRY  "\033[1;33m"
static const char *HL_KWS[] = {
    "def","return","if","elif","else","while","for","in",
    "break","continue","and","or","not","import",
    "try","except","finally","as","lambda","pass",
    "class","global","del","raise","with","yield",NULL
};
static const char *HL_QRYS[] = {
    "select", "update", "delete", "from", "where", "by",
    "create", "table", "insert", "into", "values", NULL
};
static const char *HL_CONS[] = {"True","False","None",NULL};
static const char *HL_BIS[] = {
    "print","len","range","type","int","float","str","list","bool",
    "sum","avg","min","max","dot","mmul","abs","sqrt",
    "sort","reverse","zip","enumerate","map","filter",
    "table","ktable","columns","shape","head","tail",
    "append","pop","keys","values",
    "load","save","input","repr","clock","timer","eval","exit",
    "read","write","readlines",
    "listdir","walk","stat",
    "path_join","path_exists","path_isdir","path_isfile",
    "path_basename","path_dirname","path_splitext",
    "getcwd","mkdir","getenv","sh","machine",
    "re_findall","re_sub","re_match","re_split",
    "json_loads","json_dumps","json_load","json_dump",
    "sorted","any","all","isinstance","hasattr","getattr",
    "chr","ord","hex","dict","set","next","assert",
    "int64","float64",
    NULL
};
static int hl_in(const char *w, const char **t) {
    for(int i=0;t[i];i++) if(!strcmp(w,t[i])) return 1;
    return 0;
}
static void hl_render(const char *s, int len) {
    int i=0;
    while(i<len) {
        char c=s[i];
        if(c=='#') { printf(HL_CMT); while(i<len)putchar(s[i++]); printf(HL_RST); return; }
        if((c=='f'||c=='F'||c=='r'||c=='R'||c=='b'||c=='B')&&i+1<len&&(s[i+1]=='"'||s[i+1]=='\'')) {
            char q=s[i+1]; printf(HL_STR); putchar(s[i++]); putchar(s[i++]);
            while(i<len&&s[i]!=q){if(s[i]=='\\'&&i+1<len)putchar(s[i++]);putchar(s[i++]);}
            if(i<len) putchar(s[i++]);
            printf(HL_RST); continue;
        }
        if(c=='"'||c=='\'') {
            char q=c; printf(HL_STR);
            if(i+2<len&&s[i+1]==q&&s[i+2]==q) {
                putchar(s[i++]);putchar(s[i++]);putchar(s[i++]);
                while(i<len){
                    if(i+2<len&&s[i]==q&&s[i+1]==q&&s[i+2]==q){
                        putchar(s[i++]);putchar(s[i++]);putchar(s[i++]);break;
                    }
                    if(s[i]=='\\'&&i+1<len) putchar(s[i++]);
                    putchar(s[i++]);
                }
            } else {
                putchar(s[i++]);
                while(i<len&&s[i]!=q){if(s[i]=='\\'&&i+1<len)putchar(s[i++]);putchar(s[i++]);}
                if(i<len)putchar(s[i++]);
            }
            printf(HL_RST); continue;
        }
        if(isdigit((unsigned char)c)||(c=='.'&&i+1<len&&isdigit((unsigned char)s[i+1]))) {
            printf(HL_NUM);
            for(;;) {
                if(i>=len) break;
                char d=s[i];
                if(isdigit((unsigned char)d)||(d=='.'&&i+1<len&&isdigit((unsigned char)s[i+1]))) {
                    if(d=='0'&&i+1<len&&(s[i+1]=='x'||s[i+1]=='X')){putchar(s[i++]);putchar(s[i++]);while(i<len&&isxdigit((unsigned char)s[i]))putchar(s[i++]);}
                    else{while(i<len&&(isdigit((unsigned char)s[i])||s[i]=='_'))putchar(s[i++]);
                        if(i<len&&s[i]=='.'){putchar(s[i++]);while(i<len&&isdigit((unsigned char)s[i]))putchar(s[i++]);}
                        if(i<len&&(s[i]=='e'||s[i]=='E')){putchar(s[i++]);if(i<len&&(s[i]=='+'||s[i]=='-'))putchar(s[i++]);while(i<len&&isdigit((unsigned char)s[i]))putchar(s[i++]);}}
                } else break;
                int j=i;
                W(j<len&&(s[j]==' '||s[j]=='\t'),j++)
                if(j>=len) break;
                char nc=s[j];
                if(isdigit((unsigned char)nc)||(nc=='.'&&j+1<len&&isdigit((unsigned char)s[j+1]))){
                    W(i<j,putchar(s[i++]))
                    continue;
                }
                break;
            }
            printf(HL_RST); continue;
        }
        if(isalpha(c)||c=='_') {
            int p0=i; while(i<len&&(isalnum(s[i])||s[i]=='_'))i++;
            char w[256]; int wl=i-p0; if(wl>=(int)sizeof(w))wl=sizeof(w)-1;
            memcpy(w,s+p0,wl); w[wl]=0;
            if(hl_in(w,HL_KWS))       printf(HL_KW "%s" HL_RST,w);
            else if(hl_in(w,HL_QRYS))  printf(HL_QRY "%s" HL_RST,w);
            else if(hl_in(w,HL_CONS))  printf(HL_CON "%s" HL_RST,w);
            else if(hl_in(w,HL_BIS))   printf(HL_BI "%s" HL_RST,w);
            else printf("%s",w);
            continue;
        }
        if(c=='@'){
            /* Leading decorator vs expression each. */
            int j = i;
            while (j > 0 && (s[j - 1] == ' ' || s[j - 1] == '\t')) j--;
            int line_start = (j == 0 || s[j - 1] == '\n' || s[j - 1] == ';');
            if (line_start && i + 1 < len && (isalpha((unsigned char)s[i + 1]) || s[i + 1] == '_')) {
                printf(HL_DEC);
                putchar(s[i++]);
                while (i < len && (isalnum((unsigned char)s[i]) || s[i] == '_' || s[i] == '.'))
                    putchar(s[i++]);
                printf(HL_RST);
            } else {
                putchar(s[i++]);
            }
            continue;
        }
        putchar(s[i++]);
    }
}
#if SHAKTI_HL
#define HL_HMAX 512
static char *hl_hist[HL_HMAX];
static int   hl_hlen = 0;
static void hl_hadd(const char *s) {
    Pv(!s[0])
    Pv(hl_hlen>0 && !strcmp(hl_hist[hl_hlen-1],s))
    if(hl_hlen>=HL_HMAX){free(hl_hist[0]);memmove(hl_hist,hl_hist+1,(HL_HMAX-1)*sizeof(char*));hl_hlen--;}
    hl_hist[hl_hlen++]=strdup(s);
}
static struct termios hl_orig;
static int hl_raw=0;
static void hl_raw_off(void){if(hl_raw){tcsetattr(STDIN_FILENO,TCSAFLUSH,&hl_orig);hl_raw=0;}}
static void hl_raw_on(void){
    if(!isatty(STDIN_FILENO))return;
    tcgetattr(STDIN_FILENO,&hl_orig);
    struct termios r=hl_orig;
    r.c_iflag &= ~(BRKINT|ICRNL|INPCK|ISTRIP|IXON);
    r.c_oflag |= OPOST;
    r.c_cflag |= CS8;
    r.c_lflag &= ~(ECHO|ICANON|IEXTEN|ISIG);
    r.c_cc[VMIN]=1; r.c_cc[VTIME]=0;
    tcsetattr(STDIN_FILENO,TCSAFLUSH,&r);
    hl_raw=1;
}
static void hl_redraw(const char *prompt, const char *buf, int len, int pos) {
    int plen=(int)strlen(prompt);
    printf("\r\033[K%s",prompt);
    hl_render(buf,len);
    if(plen+pos>0) printf("\r\033[%dC",plen+pos);
    else printf("\r");
    fflush(stdout);
}
static int hl_read_char(char *c) {
    return input_hub_read_char(c);
}
static char *hl_readline(const char *prompt, int nsp) {
    static char buf[65536];
    int len=0, pos=0, hidx=hl_hlen;
    if(nsp < 0) nsp = 0;
    if(nsp > 64) nsp = 64;
    if(!isatty(STDIN_FILENO)){
        printf("%s",prompt); fflush(stdout);
        if(!fgets(buf,sizeof(buf),stdin))return NULL;
        size_t l=strlen(buf); if(l>0&&buf[l-1]=='\n')buf[l-1]=0;
        return buf;
    }
    hl_raw_on();
    if(nsp > 0){
        memset(buf, ' ', (size_t)nsp);
        len = pos = nsp;
        buf[len] = 0;
        hl_redraw(prompt, buf, len, pos);
    } else {
        printf("\r%s",prompt); fflush(stdout);
    }
    for(;;) {
        char c; if(!hl_read_char(&c)){hl_raw_off();return NULL;}
        if(c=='\r'||c=='\n'){buf[len]=0;printf("\r\n");fflush(stdout);hl_raw_off();hl_hadd(buf);return buf;}
        if(c==3){len=pos=0;printf("\r\n%s",prompt);fflush(stdout);continue;}
        if(c==4){if(len==0){printf("\r\n");hl_raw_off();return NULL;}continue;}
        if(c==127||c==8){if(pos>0){memmove(buf+pos-1,buf+pos,len-pos);pos--;len--;}}
        else if(c==1){pos=0;}
        else if(c==5){pos=len;}
        else if(c==21){memmove(buf,buf+pos,len-pos);len-=pos;pos=0;}
        else if(c==11){len=pos;}
        else if(c==12){printf("\033[2J\033[H");}
        else if(c==23){
            int e=pos; while(pos>0&&buf[pos-1]==' ')pos--;
            W(pos>0&&buf[pos-1]!=' ',pos--)
            memmove(buf+pos,buf+e,len-e);len-=(e-pos);
        }
        else if(c==9){
            if(len+4<(int)sizeof(buf)){memmove(buf+pos+4,buf+pos,len-pos);memset(buf+pos,' ',4);pos+=4;len+=4;}
        }
        else if(c==27){
            char sq[3]; if(!hl_read_char(&sq[0]))continue;
            if(!hl_read_char(&sq[1]))continue;
            if(sq[0]=='['){
                switch(sq[1]){
                case 'A':if(hidx>0){hidx--;strncpy(buf,hl_hist[hidx],sizeof(buf)-1);len=(int)strlen(buf);pos=len;}break;
                case 'B':if(hidx<hl_hlen-1){hidx++;strncpy(buf,hl_hist[hidx],sizeof(buf)-1);len=(int)strlen(buf);pos=len;}
                         else{hidx=hl_hlen;buf[0]=0;len=pos=0;}break;
                case 'C':if(pos<len)pos++;break;
                case 'D':if(pos>0)pos--;break;
                case 'H':pos=0;break;
                case 'F':pos=len;break;
                case '3':hl_read_char(&sq[2]);if(pos<len){memmove(buf+pos,buf+pos+1,len-pos-1);len--;}break;
                case '1':case '7':hl_read_char(&sq[2]);pos=0;break;
                case '4':case '8':hl_read_char(&sq[2]);pos=len;break;
                }
            }
        }
        else if(c>=32&&len+1<(int)sizeof(buf)){memmove(buf+pos+1,buf+pos,len-pos);buf[pos]=c;pos++;len++;}
        else continue;
        buf[len]=0;
        hl_redraw(prompt,buf,len,pos);
    }
}
#endif
#if !SHAKTI_HL
static char *read_line(const char *prompt) {
    static char buf[65536];
    (void)prompt;
    if (!fgets(buf, (int)sizeof buf, stdin)) return NULL;
    size_t n = strlen(buf);
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = 0;
    return buf;
}
#endif
/* Open named root doc (e.g. IE.txt). SHAKTI_DOC overrides only when basename matches. */
static int repl_doc_path_from_lib(char *path, size_t path_cap, const char *lib, const char *name) {
    size_t n = strlen(lib);
    size_t strip = 0;
    if (n > 4 && !strcmp(lib + n - 4, "/lib")) strip = 4;
    if (!strip) return -1;
    if (n - strip + 1 + strlen(name) + 1 >= path_cap) return -1;
    memcpy(path, lib, n - strip);
    path[n - strip] = 0;
    snprintf(path + n - strip, path_cap - (n - strip), "/%s", name);
    return 0;
}
static FILE *repl_open_doc_from_libdir(const char *lib, const char *name) {
    char path[4096];
    char open_err[256];
    if (repl_doc_path_from_lib(path, sizeof path, lib, name) == 0) {
        open_err[0] = 0;
        FILE *f = fopen_regular(path, open_err, sizeof open_err);
        if (f) return f;
    }
    snprintf(path, sizeof path, "%s/../%s", lib, name);
    open_err[0] = 0;
    return fopen_regular(path, open_err, sizeof open_err);
}
static FILE *repl_open_doc(const char *name) {
    char path[4096];
    char open_err[256];
    const char *doc = getenv("SHAKTI_DOC");
    if (doc && doc[0]) {
        const char *base = strrchr(doc, '/');
        const char *base_bs = strrchr(doc, '\\');
        if (!base || (base_bs && base_bs > base)) base = base_bs;
        base = base ? base + 1 : doc;
        if (!strcmp(base, name)) {
            open_err[0] = 0;
            FILE *f = fopen_regular(doc, open_err, sizeof open_err);
            if (f) return f;
        }
    }
    if (g_lib_path[0]) {
        FILE *f = repl_open_doc_from_libdir(g_lib_path, name);
        if (f) return f;
    }
    const char *lib = getenv("SHAKTI_LIB");
    if (lib && lib[0]) {
        FILE *f = repl_open_doc_from_libdir(lib, name);
        if (f) return f;
    }
#if defined(__linux__) && !defined(__EMSCRIPTEN__)
    {
        char *exe = realpath("/proc/self/exe", NULL);
        if (exe) {
            char *slash = strrchr(exe, '/');
            if (slash) {
                *slash = 0;
                snprintf(path, sizeof path, "%s/%s", exe, name);
                open_err[0] = 0;
                FILE *f = fopen_regular(path, open_err, sizeof open_err);
                if (f) { free(exe); return f; }
                /* Binary often lives in .build/; card is at repo root. */
                slash = strrchr(exe, '/');
                if (slash) {
                    *slash = 0;
                    snprintf(path, sizeof path, "%s/%s", exe, name);
                    open_err[0] = 0;
                    f = fopen_regular(path, open_err, sizeof open_err);
                    if (f) { free(exe); return f; }
                }
            }
            free(exe);
        }
    }
#endif
    snprintf(path, sizeof path, "%s", name);
    open_err[0] = 0;
    return fopen_regular(path, open_err, sizeof open_err);
}
/* Fixed-width grammar card with REPL syntax highlighting (IE.txt). */
static void repl_print_hl_doc(const char *name) {
    FILE *f = repl_open_doc(name);
    if (!f) {
        fprintf(stderr,
            "Cannot open %s (set SHAKTI_DOC to a path whose basename is %s)\n",
            name, name);
        return;
    }
    char line[4096];
    int first = 1;
    while (fgets(line, (int)sizeof line, f)) {
        size_t n = strlen(line);
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0;
        /* First line product/version header — rewrite live pkg version when present. */
        if (first) {
            if (!strncmp(line, "shakti ", 7)) {
                char hdr[128];
                snprintf(hdr, sizeof hdr, "shakti %s", SHAKTI_PKG_VERSION);
                n = strlen(hdr);
                if (n >= sizeof line) n = sizeof line - 1;
                memcpy(line, hdr, n);
                line[n] = 0;
            }
            first = 0;
        }
#if SHAKTI_HL
        hl_render(line, (int)n);
        putchar('\n');
#else
        puts(line);
#endif
    }
    fclose(f);
}
void run_repl(Env *e) {

#if SHAKTI_HL
    atexit(hl_raw_off);
#endif
    enum { REPL_INPUT_CAP = 262144 };
    char input[REPL_INPUT_CAP];
    atexit(input_hub_shutdown);
    for (;;) {
#if SHAKTI_HL
        char *line = hl_readline("> ", 0);
#else
        char *line = read_line("> ");
#endif
        if (!line) break;
        if (strcmp(line, "quit") == 0 || strcmp(line, "exit") == 0) break;
        if (line[0] == 0) continue;
        size_t inlen = strlen(line);
        if (inlen + 1 >= REPL_INPUT_CAP) {
            fprintf(stderr, "REPL input too long (max %d bytes)\n", REPL_INPUT_CAP - 1);
            continue;
        }
        memcpy(input, line, inlen);
        input[inlen++] = '\n';
        input[inlen] = 0;
        if (strcmp(line, "\\v") == 0) {
            for(int i=0; i<e->len; i++) {
                printf("%-15s", e->names[i]);
                print_val(e->vals[i], stdout, 1);
                printf("\n");
            }
            continue;
        }
        if (strcmp(line, "\\w") == 0) {
            for(int i=0; i<e->len; i++) {
                printf("%s\n", e->names[i]);
            }
            continue;
        }
        if (strcmp(line, "\\d") == 0 || strcmp(line, "\\help") == 0 || strcmp(line, "help") == 0) {
            repl_print_hl_doc("IE.txt");
            continue;
        }
        /* \q / \q N — process exit (optional status), same as Isolde */
        if (strncmp(line, "\\q", 2) == 0 &&
            (line[2] == 0 || line[2] == ' ' || line[2] == '\t')) {
            const char *p = line + 2;
            while (*p == ' ' || *p == '\t')
                p++;
            if (*p == 0) {
                exit(0);
            }
            char *end = NULL;
            errno = 0;
            long code = strtol(p, &end, 10);
            if (end == p || errno == ERANGE || code < INT_MIN || code > INT_MAX) {
                fprintf(stderr, "usage: \\q [N]\n");
                continue;
            }
            while (*end == ' ' || *end == '\t')
                end++;
            if (*end != 0) {
                fprintf(stderr, "usage: \\q [N]\n");
                continue;
            }
            exit((int)code);
        }
        if (line[0] == '\\') {
            fprintf(stderr, "Unknown REPL command: %s (try \\d for help)\n", line);
            continue;
        }
        while (repl_buffer_needs_more(input, inlen)) {
#if SHAKTI_HL
            line = hl_readline("| ", isatty(STDIN_FILENO) ? repl_prefill_spaces(input, inlen) : 0);
#else
            line = read_line("| ");
#endif
            if (!line) break;
            if (repl_line_blank(line)) break;
            size_t ll = strlen(line);
            if (inlen + ll + 1 >= REPL_INPUT_CAP) {
                fprintf(stderr, "REPL input too long (max %d bytes)\n", REPL_INPUT_CAP - 1);
                inlen = 0;
                input[0] = 0;
                break;
            }
            memcpy(input + inlen, line, ll);
            inlen += ll;
            input[inlen++] = '\n';
            input[inlen] = 0;
        }
        Node *prog = parse(input);
        if(!prog) continue;
        V *result = eval(prog, e);
        if(g_error) {
            if(g_error_val) { fprintf(stderr, "Error: %s\n", g_error_val->s); v_free(g_error_val); g_error_val=NULL; }
            g_error = 0;
        } else if(!shakti_prog_silent_last(prog) && result && result->t != T_NIL && result->t != T_ERR) {
            print_val(result, stdout, 1);
            putchar('\n');
        }
        if(result && result->t == T_ERR) {
            fprintf(stderr, "Error: %s\n", result->s);
        }
        v_free(result);
        node_free(prog);
    }
}
