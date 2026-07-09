#include "shakti.h"
#include "vec_kernels.h"
#include "input.h"
#include <time.h>
#include <sys/time.h>
#include <sys/select.h>
#include <dirent.h>
#include <unistd.h>
extern volatile int g_sigint;
extern Node*parse_sql(const char*src);
extern Node*parse_sql(const char*src);
extern void v_serialize(V *v, FILE *fp);
extern V *v_deserialize(FILE *fp);
extern V*load_module(const char *name);
extern char *read_file(const char *path);
extern V*table_xml_load(const char*path,V*columns_opt);
extern V *bi_fread(V**,in);
extern V *bi_fwrite(V**,in);
extern V *bi_readlines(V**,in);
extern V *bi_listdir(V**,in);
extern V *bi_walk(V**,in);
extern V *bi_stat(V**,in);
extern V *bi_path_join(V**,in);
extern V *bi_path_exists(V**,in);
extern V *bi_path_isdir(V**,in);
extern V *bi_path_isfile(V**,in);
extern V *bi_path_basename(V**,in);
extern V *bi_path_dirname(V**,in);
extern V *bi_path_splitext(V**,in);
extern V *bi_getcwd(V**,in);
extern V *bi_mkdir(V**,in);
extern V *bi_getenv(V**,in);
extern V *bi_machine(V**,in);
extern V *bi_sh(V**,in);
extern V *bi_re_findall(V**,in);
extern V *bi_re_sub(V**,in);
extern V *bi_re_match(V**,in);
extern V *bi_re_split(V**,in);
extern V *bi_json_loads(V**,in);
extern V *bi_json_dumps(V**,in);
extern V *bi_json_load(V**,in);
extern V *bi_json_dump(V**,in);
extern V *bi_sorted(V**,in,V**,V**,int nkw,Env*);
extern V *bi_any(V**,in);
extern V *bi_all(V**,in);
extern V *bi_isinstance(V**,in);
extern V *bi_hasattr(V**,in);
extern V *bi_getattr(V**,in);
extern V *bi_chr(V**,in);
extern V *bi_ord(V**,in);
extern V *bi_hex(V**,in);
extern V *bi_dict(V**,int nargs,V**,V**,int nkw);
extern V *bi_ktable(V**,int nargs,V**,V**,int nkw);
extern V *bi_set(V**,in);
extern V *vec_cmp(V*,V*,int);
extern V *bi_talk_listen(V**,in);
extern V *bi_talk_set_locale(V**,in);
extern V *bi_talk_set_model(V**,in);
extern V *bi_synth_open(V**,in);
extern V *bi_synth_close(V**,in);
extern V *bi_synth_alive(V**,in);
extern V *bi_synth_tick(V**,in);
extern V *bi_synth_set_steps(V**,in);
extern V *bi_synth_steps(V**,in);
extern V *bi_synth_set_metro(V**,in);
extern V *bi_synth_metro_on(V**,in);
extern V *bi_synth_set_metro_sound(V**,in);
extern V *bi_synth_metro_sound(V**,in);
extern V *bi_synth_set_mute(V**,in);
extern V *bi_synth_mute_on(V**,in);
extern V *bi_synth_note_on(V**,in);
extern V *bi_synth_note_off(V**,in);
extern V *bi_synth_set_bpm(V**,in);
extern V *bi_synth_bpm(V**,in);
extern V *bi_synth_set_tuning(V**,in);
extern V *bi_synth_tuning(V**,in);
extern V *bi_synth_set_level(V**,in);
extern V *bi_synth_level(V**,in);
extern V *bi_synth_set_cutoff(V**,in);
extern V *bi_synth_cutoff(V**,in);
extern V *bi_synth_set_reso(V**,in);
extern V *bi_synth_reso(V**,in);
extern V *bi_synth_set_seq_row(V**,in);
extern V *bi_synth_play(V**,in);
extern V *bi_synth_playing(V**,in);
extern V *bi_synth_mouse_press(V**,in);
extern V *bi_synth_mouse_release(V**,in);
extern V *bi_synth_set_viz(V**,in);
extern V *bi_synth_viz_mode(V**,in);
extern V *bi_synth_load_sample(V**,in);
extern V *bi_synth_sample_loaded(V**,in);
extern V *bi_synth_sample_name(V**,in);
extern V *bi_synth_set_row_note(V**,in);
extern V *bi_synth_row_note(V**,in);
extern V *bi_synth_looper_rec(V**,in);
extern V *bi_synth_looper_play(V**,in);
extern V *bi_synth_looper_clear(V**,in);
extern V *bi_synth_looper_overdub(V**,in);
extern V *bi_synth_looper_rec_on(V**,in);
extern V *bi_synth_looper_play_on(V**,in);
extern V *bi_synth_looper_has_loop(V**,in);
extern V *bi_ipc_accept(V**,in);
extern V *bi_ipc_close(V**,in);
extern V *bi_ipc_connect(V**,in);
extern V *bi_ipc_listen(V**,in);
extern V *bi_ipc_poll(V**,in);
extern V *bi_ipc_recv(V**,in);
extern V *bi_ipc_recv_nowait(V**,in);
extern V *bi_ipc_rdma_available(V**,in);
extern V *bi_ipc_send(V**,in);
extern V *bi_ipc_set_nonblock(V**,in);
extern V *bi_ipc_shm_close(V**,in);
extern V *bi_ipc_shm_open(V**,in);
extern V *bi_graph_create(V**,in);
extern V *bi_graph_add(V**,in);
extern V *bi_graph_query(V**,in);
extern V *bi_graph_neighbors(V**,in);
extern V *bi_graph_path(V**,in);
extern V *bi_graph_from_table(V**,in);
extern V *bi_graph_to_table(V**,in);
extern V *bi_graph_count(V**,in);
extern V *bi_graph_clear(V**,in);

static V *kw_get(V**kwn,V**kwv,int nkw,const char*name){
    i(nkw,{P(kwn[i]->t==T_STR&&!strcmp(kwn[i]->s,name),kwv[i])})return NULL;}
static V *bi_print(V**a,in,V**kwn,V**kwv,int nkw){
    V *sep_v=kw_get(kwn,kwv,nkw,"sep");
    V *end_v=kw_get(kwn,kwv,nkw,"end");
    const char *sep=sep_v&&sep_v->t==T_STR?sep_v->s:" ";
    const char *end=end_v&&end_v->t==T_STR?end_v->s:"\n";
    for(int i=0;i<n;i++){if(i)printf("%s",sep);v_print(a[i],0);}
    printf("%s",end);fflush(stdout);return v_nil();}
static V *bi_len(V**a,in){
    P(n<1,v_err("len()"))V*v=a[0];
    P(v->t==T_STR,v_int(strlen(v->s)))
    P((v->t>=T_IVEC&&v->t<=T_LIST)||(v->t>=T_IMAT&&v->t<=T_BMAT),v_int(v->n))
    P(v->t==T_TABLE,v_int(v->n))
    if(v->t==T_DICT){if(v->n&&v->keys->L[0]->t==T_NIL)return v_int(1);return v_int(v->n);}
    return v_err("no len()");}
static V *bi_range(V**a,in){
    int64_t start=0,stop=0,step=1;
    if(n==1)stop=a[0]->j;else if(n==2){start=a[0]->j;stop=a[1]->j;}
    else if(n>=3){start=a[0]->j;stop=a[1]->j;step=a[2]->j;}
    P(!step,v_err("step=0"))
    int64_t cnt=0;
    if(step>0&&start<stop)cnt=(stop-start+step-1)/step;
    else if(step<0&&start>stop)cnt=(start-stop-step-1)/(-step);
    if(cnt<0)cnt=0;
    V*r=v_ivec(cnt);
    if(step==1 && cnt>0){
        if(start==0)
            for(int64_t i=0;i<cnt;i++)r->J[i]=i;
        else
            for(int64_t i=0,v=start;i<cnt;i++,v++)r->J[i]=v;
    }else for(int64_t i=0;i<cnt;i++)r->J[i]=start+i*step;
    return r;}
static V *bi_type(V**a,in){
    static V *cache[32];
    int t = n > 0 ? a[0]->t : T_NIL;
    if (t >= 0 && t < 32) {
        if (!cache[t]) cache[t] = v_str(n > 0 ? type_name(a[0]->t) : "NoneType");
        return v_ref(cache[t]);
    }
    return v_str(n > 0 ? type_name(a[0]->t) : "NoneType");
}
static V *bi_int(V**a,in){
    P(n<1,v_int(0))V*v=a[0];
    P(v->t==T_INT||v->t==T_CHAR,v_int(v->j))P(v->t==T_FLOAT,v_int((int64_t)v->f))
    P(v->t==T_BOOL,v_int(v->b))P(v->t==T_STR,v_int(strtoll(v->s,NULL,0)))
    return v_err("cannot convert to int");}
static V *bi_float(V**a,in){
    P(n<1,v_float(0))V*v=a[0];
    P(v->t==T_FLOAT,v_float(v->f))P(v->t==T_INT||v->t==T_CHAR,v_float((double)v->j))
    P(v->t==T_BOOL,v_float(v->b))P(v->t==T_STR,v_float(strtod(v->s,NULL)))
    return v_err("cannot convert to float");}
static V *bi_str(V**a,in){
    P(n<1,v_str(""))
    P(a[0]->t==T_STR,v_str(a[0]->s))
    char *s=v_to_str(a[0]);V*r=v_str(s);free(s);return r;}
static V *bi_list(V**a,in){
    P(n<1,v_list(0))V*v=a[0];
    if(v->t==T_CVEC){V*r=v_list(v->n);for(int64_t i=0;i<v->n;i++)r->L[i]=v_char(v->B[i]);return r;}
    if(v->t==T_IVEC){V*r=v_list(v->n);for(int64_t i=0;i<v->n;i++)r->L[i]=v_int(v->J[i]);return r;}
    if(v->t==T_FVEC){V*r=v_list(v->n);for(int64_t i=0;i<v->n;i++)r->L[i]=v_float(v->F[i]);return r;}
    P(v->t==T_LIST,v_copy(v))
    if(v->t==T_STR){int64_t sl=strlen(v->s);V*r=v_list(sl);for(int64_t i=0;i<sl;i++){char b[2]={v->s[i],0};r->L[i]=v_str(b);}return r;}
    if(v->t==T_DICT){V*r=v_copy(v->keys);return r;}
    return v_err("cannot convert to list");}
static V *bi_bool(V**a,in){
    P(n<1,v_bool(0))V*v=a[0];
    P(v->t==T_BOOL,v_bool(v->b))P(v->t==T_INT||v->t==T_CHAR,v_bool(v->j!=0))
    P(v->t==T_FLOAT,v_bool(v->f!=0))P(v->t==T_STR,v_bool(v->s[0]!=0))
    P(v->t==T_NIL,v_bool(0))return v_bool(1);}
static int bi_numvec(V *v) { return v->t == T_IVEC || v->t == T_FVEC || v->t == T_IMAT || v->t == T_FMAT || v->t == T_CVEC || v->t == T_CMAT; }
static int64_t mat_nelem(V *v) { return v->n * mat_cols(v); }
static V *vec_reduce_sum(V *v) {
    if (v->t == T_IMAT) {
        int64_t s = 0;
        int64_t ne = mat_nelem(v);
        for (int64_t i = 0; i < ne; i++) s += v->J[i];
        return v_int(s);
    }
    if (v->t == T_CMAT) {
        int64_t s = 0;
        int64_t ne = mat_nelem(v);
        for (int64_t i = 0; i < ne; i++) s += v->B[i];
        return v_int(s);
    }
    if (v->t == T_FMAT) {
        double s = 0;
        int64_t ne = mat_nelem(v);
        for (int64_t i = 0; i < ne; i++) s += v->F[i];
        return v_float(s);
    }
    if (v->t == T_IVEC) {
        int64_t s = 0;
        for (int64_t i = 0; i < v->n; i++) s += v->J[i];
        return v_int(s);
    }
    if (v->t == T_CVEC) {
        int64_t s = 0;
        for (int64_t i = 0; i < v->n; i++) s += v->B[i];
        return v_int(s);
    }
    if (v->t == T_FVEC) {
        if (v->n >= ISL_OMP_VEC_MIN)
            return v_float(shakti_sum_f64(v->F, v->n));
        double s = 0;
        for (int64_t i = 0; i < v->n; i++) s += v->F[i];
        return v_float(s);
    }
    if (v->t == T_LIST) {
        double s = 0;
        int all_int = 1;
        for (int64_t i = 0; i < v->n; i++) {
            V *e = v->L[i];
            if (e->t == T_INT) s += (double)e->j;
            else if (e->t == T_FLOAT) { s += e->f; all_int = 0; }
            else return v_err("sum: bad list element");
        }
        return all_int ? v_int((int64_t)s) : v_float(s);
    }
    return v_err("sum: need vector");
}
static V *vec_reduce_avg(V *v) {
    V *s = vec_reduce_sum(v);
    P(s->t == T_ERR,s)
    int64_t cnt = v->t >= T_IMAT && v->t <= T_BMAT ? mat_nelem(v) : v->n;
    P(cnt == 0,v_float(0))
    P(s->t == T_INT,v_float((double)s->j / (double)cnt))
    return v_float(s->f / (double)cnt);
}
static V *vec_reduce_min(V *v) {
    if (v->t == T_IMAT) {
        P(v->n == 0 || mat_cols(v) == 0,v_nil())
        int64_t m = v->J[0];
        int64_t ne = mat_nelem(v);
        for (int64_t i = 1; i < ne; i++) if (v->J[i] < m) m = v->J[i];
        return v_int(m);
    }
    if (v->t == T_CMAT) {
        P(v->n == 0 || mat_cols(v) == 0,v_nil())
        int64_t m = v->B[0];
        int64_t ne = mat_nelem(v);
        for (int64_t i = 1; i < ne; i++) if (v->B[i] < m) m = v->B[i];
        return v_char(m);
    }
    if (v->t == T_FMAT) {
        P(v->n == 0 || mat_cols(v) == 0,v_nil())
        double m = v->F[0];
        int64_t ne = mat_nelem(v);
        for (int64_t i = 1; i < ne; i++) if (v->F[i] < m) m = v->F[i];
        return v_float(m);
    }
    if (v->t == T_IVEC) {
        P(v->n == 0,v_nil())
        int64_t m = v->J[0];
        for (int64_t i = 1; i < v->n; i++) if (v->J[i] < m) m = v->J[i];
        return v_int(m);
    }
    if (v->t == T_CVEC) {
        P(v->n == 0,v_nil())
        int64_t m = v->B[0];
        for (int64_t i = 1; i < v->n; i++) if (v->B[i] < m) m = v->B[i];
        return v_char(m);
    }
    if (v->t == T_FVEC) {
        P(v->n == 0,v_nil())
        double m = v->F[0];
        for (int64_t i = 1; i < v->n; i++) if (v->F[i] < m) m = v->F[i];
        return v_float(m);
    }
    if (v->t == T_LIST) {
        P(v->n == 0,v_nil())
        V *m = v_ref(v->L[0]);
        for (int64_t i = 1; i < v->n; i++) {
            V *c = vec_cmp(m, v->L[i], OP_GT);
            int lt = c->t == T_BOOL && c->b;
            v_free(c);
            if (lt) { v_free(m); m = v_ref(v->L[i]); }
        }
        return m;
    }
    return v_err("min: need vector");
}
static V *vec_reduce_max(V *v) {
    if (v->t == T_IMAT) {
        P(v->n == 0 || mat_cols(v) == 0,v_nil())
        int64_t m = v->J[0];
        int64_t ne = mat_nelem(v);
        for (int64_t i = 1; i < ne; i++) if (v->J[i] > m) m = v->J[i];
        return v_int(m);
    }
    if (v->t == T_CMAT) {
        P(v->n == 0 || mat_cols(v) == 0,v_nil())
        unsigned char m = v->B[0];
        int64_t ne = mat_nelem(v);
        for (int64_t i = 1; i < ne; i++) if (v->B[i] > m) m = v->B[i];
        return v_char(m);
    }
    if (v->t == T_FMAT) {
        P(v->n == 0 || mat_cols(v) == 0,v_nil())
        double m = v->F[0];
        int64_t ne = mat_nelem(v);
        for (int64_t i = 1; i < ne; i++) if (v->F[i] > m) m = v->F[i];
        return v_float(m);
    }
    if (v->t == T_IVEC) {
        P(v->n == 0,v_nil())
        int64_t m = v->J[0];
        for (int64_t i = 1; i < v->n; i++) if (v->J[i] > m) m = v->J[i];
        return v_int(m);
    }
    if (v->t == T_CVEC) {
        P(v->n == 0,v_nil())
        unsigned char m = v->B[0];
        for (int64_t i = 1; i < v->n; i++) if (v->B[i] > m) m = v->B[i];
        return v_char(m);
    }
    if (v->t == T_FVEC) {
        P(v->n == 0,v_nil())
        double m = v->F[0];
        for (int64_t i = 1; i < v->n; i++) if (v->F[i] > m) m = v->F[i];
        return v_float(m);
    }
    if (v->t == T_LIST) {
        P(v->n == 0,v_nil())
        V *m = v_ref(v->L[0]);
        for (int64_t i = 1; i < v->n; i++) {
            V *c = vec_cmp(m, v->L[i], OP_LE);
            int gt = c->t == T_BOOL && c->b;
            v_free(c);
            if (gt) { v_free(m); m = v_ref(v->L[i]); }
        }
        return m;
    }
    return v_err("max: need vector");
}
static V *vec_unary_int(V *v, int64_t (*fn)(int64_t)) {
    if (v->t == T_IMAT) {
        V *r = v_imat(v->n, mat_cols(v));
        int64_t ne = mat_nelem(v);
        for (int64_t i = 0; i < ne; i++) r->J[i] = fn(v->J[i]);
        return r;
    }
    P(v->t != T_IVEC, v_err("need int vector"))
    V *r = v_ivec(v->n);
#ifdef _OPENMP
    if (v->n >= ISL_OMP_VEC_MIN)
        #pragma omp parallel for
        for (int64_t i = 0; i < v->n; i++) r->J[i] = fn(v->J[i]);
    else
#endif
        for (int64_t i = 0; i < v->n; i++) r->J[i] = fn(v->J[i]);
    return r;
}
static V *vec_unary_double(V *v, double (*fn)(double)) {
    if (v->t == T_IMAT) {
        V *r = v_fmat(v->n, mat_cols(v));
        int64_t ne = mat_nelem(v);
        for (int64_t i = 0; i < ne; i++) r->F[i] = fn((double)v->J[i]);
        return r;
    }
    if (v->t == T_FMAT) {
        V *r = v_fmat(v->n, mat_cols(v));
        int64_t ne = mat_nelem(v);
        for (int64_t i = 0; i < ne; i++) r->F[i] = fn(v->F[i]);
        return r;
    }
    if (v->t == T_IVEC) {
        V *r = v_fvec(v->n);
#ifdef _OPENMP
        if (v->n >= ISL_OMP_VEC_MIN)
            #pragma omp parallel for
            for (int64_t i = 0; i < v->n; i++) r->F[i] = fn((double)v->J[i]);
        else
#endif
            for (int64_t i = 0; i < v->n; i++) r->F[i] = fn((double)v->J[i]);
        return r;
    }
    if (v->t == T_FVEC) {
        V *r = v_fvec(v->n);
#ifdef _OPENMP
        if (v->n >= ISL_OMP_VEC_MIN)
            #pragma omp parallel for
            for (int64_t i = 0; i < v->n; i++) r->F[i] = fn(v->F[i]);
        else
#endif
            for (int64_t i = 0; i < v->n; i++) r->F[i] = fn(v->F[i]);
        return r;
    }
    return v_err("need numeric vector");
}
static int64_t iabs64(int64_t x) { return x < 0 ? -x : x; }
static V *bi_sum(V **a, in) {
    P(n < 1,v_int(0))
    if (n == 1) {
        V *v = a[0];
        P(v->t >= T_IVEC && v->t <= T_LIST,vec_reduce_sum(v))
        P(v->t >= T_IMAT && v->t <= T_FMAT,vec_reduce_sum(v))
        P(v->t == T_FLOAT,v_float(v->f))
        P(v->t == T_INT,v_int(v->j))
        return v_int(0);
    }
    double s = 0;
    int all_int = 1;
    for (int i = 0; i < n; i++) {
        if (a[i]->t == T_INT) s += (double)a[i]->j;
        else if (a[i]->t == T_FLOAT) { s += a[i]->f; all_int = 0; }
        else if (a[i]->t >= T_IVEC && a[i]->t <= T_LIST) {
            V *p = vec_reduce_sum(a[i]);
            P(p->t == T_ERR,p)
            if (p->t == T_FLOAT) { s += p->f; all_int = 0; }
            else s += (double)p->j;
            v_free(p);
        } else if (a[i]->t >= T_IMAT && a[i]->t <= T_FMAT) {
            V *p = vec_reduce_sum(a[i]);
            P(p->t == T_ERR,p)
            if (p->t == T_FLOAT) { s += p->f; all_int = 0; }
            else s += (double)p->j;
            v_free(p);
        } else return v_err("sum: bad arg");
    }
    return all_int ? v_int((int64_t)s) : v_float(s);
}
static V *bi_dot(V **a, in) {
    P(n < 2, v_err("dot(x, y)"))
    if (a[0]->n != a[1]->n) return v_err("dot: length mismatch");
    if (a[0]->t == T_FVEC && a[1]->t == T_FVEC)
        return v_float(shakti_dot_f64(a[0]->F, a[1]->F, a[0]->n));
    if ((a[0]->t == T_INT || a[0]->t == T_FLOAT) &&
        (a[1]->t == T_INT || a[1]->t == T_FLOAT)) {
        double x = a[0]->t == T_INT ? (double)a[0]->j : a[0]->f;
        double y = a[1]->t == T_INT ? (double)a[1]->j : a[1]->f;
        return v_float(x * y);
    }
    if ((a[0]->t == T_IVEC || a[0]->t == T_FVEC) &&
        (a[1]->t == T_IVEC || a[1]->t == T_FVEC)) {
        int a_f = a[0]->t == T_FVEC, b_f = a[1]->t == T_FVEC;
        return v_float(shakti_dot_numeric(
            a_f ? NULL : a[0]->J, a_f ? a[0]->F : NULL, a_f,
            b_f ? NULL : a[1]->J, b_f ? a[1]->F : NULL, b_f,
            a[0]->n));
    }
    return v_err("dot: need numeric vectors or scalars");
}
static V *bi_avg(V **a, in) {
    P(n < 1,v_float(0))
    P((a[0]->t >= T_IVEC && a[0]->t <= T_LIST) || (a[0]->t >= T_IMAT && a[0]->t <= T_FMAT),vec_reduce_avg(a[0]))
    return v_float(0);
}
static V *bi_min(V **a, in) {
    P(n < 1,v_nil())
    P((a[0]->t >= T_IVEC && a[0]->t <= T_LIST) || (a[0]->t >= T_IMAT && a[0]->t <= T_FMAT),vec_reduce_min(a[0]))
    if (n == 2) {
        double x = a[0]->t == T_FLOAT ? a[0]->f : (double)a[0]->j;
        double y = a[1]->t == T_FLOAT ? a[1]->f : (double)a[1]->j;
        return x < y ? v_float(x) : v_float(y);
    }
    return v_nil();
}
static V *bi_max(V **a, in) {
    P(n < 1,v_nil())
    P((a[0]->t >= T_IVEC && a[0]->t <= T_LIST) || (a[0]->t >= T_IMAT && a[0]->t <= T_FMAT),vec_reduce_max(a[0]))
    if (n == 2) {
        double x = a[0]->t == T_FLOAT ? a[0]->f : (double)a[0]->j;
        double y = a[1]->t == T_FLOAT ? a[1]->f : (double)a[1]->j;
        return x > y ? v_float(x) : v_float(y);
    }
    return v_nil();
}
static V *bi_sleep(V**a,in) {
    struct timeval tv={0},*tvp=NULL;
    if(n>0) {
        if(a[0]->t == T_FLOAT) {
            tv.tv_sec = (long long)a[0]->f;
            tv.tv_usec = (long long)((a[0]->f*1e6) - (tv.tv_sec*1e6));
            tvp=&tv;
        } else if(a[0]->t == T_INT) {
            tv.tv_sec = (long long)a[0]->j;
            tvp=&tv;
        } else if(a[0]->t == T_CHAR) {
            tv.tv_sec = 0;
            tv.tv_usec = a[0]->j * 5000;
            tvp=&tv;
        } else if(a[0]->t == T_NIL) {
        } else {
            return v_err("sleep(time)");
        }
    }
    select(0,NULL,NULL,NULL,tvp);
    if(g_sigint) return g_sigint=0,v_err("interrupted");
    return v_nil();
}
static V *bi_abs(V **a, in) {
    P(n < 1,v_int(0))
    P(a[0]->t == T_CVEC || a[0]->t == T_CMAT,a[0]);
    P(a[0]->t == T_IVEC || a[0]->t == T_IMAT,vec_unary_int(a[0], iabs64))
    P(a[0]->t == T_FVEC || a[0]->t == T_FMAT,vec_unary_double(a[0], fabs))
    V *v = a[0];
    P(v->t == T_INT,v_int(v->j < 0 ? -v->j : v->j))
    P(v->t == T_FLOAT,v_float(fabs(v->f)))
    return v_int(0);
}
#define V_MAP_FUNC(NAME, FUNC) \
static V *bi_##NAME(V **a, in) { \
    P(n < 1, v_float(0)) \
    P(bi_numvec(a[0]), vec_unary_double(a[0], FUNC)) \
    V *v = a[0]; \
    return v_float(FUNC(v->t == T_INT ? (double)v->j : v->f)); \
}
#define V_SCALAR_FLOAT(NAME, FUNC) \
static V *bi_##NAME(V **a, in) { \
    P(n < 1, v_float(0)) \
    V *v = a[0]; \
    return v_float(FUNC(v->t == T_INT ? (double)v->j : v->f)); \
}
V_MAP_FUNC(sqrt, sqrt)
V_SCALAR_FLOAT(floor, floor)
V_SCALAR_FLOAT(ceil, ceil)
V_MAP_FUNC(exp, exp)
V_MAP_FUNC(log, log)
V_MAP_FUNC(sin, sin)
V_MAP_FUNC(cos, cos)
V_MAP_FUNC(tan, tan)
#undef V_MAP_FUNC
#undef V_SCALAR_FLOAT
static int cmp_i64(const void*a,const void*b){int64_t x=*(int64_t*)a,y=*(int64_t*)b;return(x>y)-(x<y);}
static int cmp_u8(const void*a,const void*b){unsigned char x=*(unsigned char*)a,y=*(unsigned char*)b;return(x>y)-(x<y);}
static int cmp_f64(const void*a,const void*b){double x=*(double*)a,y=*(double*)b;return(x>y)-(x<y);}
static V *bi_sort(V**a,in){P(n<1,v_list(0))V*v=a[0];
    if(v->t==T_IVEC){V*r=v_copy(v);qsort(r->J,r->n,8,cmp_i64);return r;}
    if(v->t==T_CVEC){V*r=v_copy(v);qsort(r->B,r->n,1,cmp_u8);return r;}
    if(v->t==T_FVEC){V*r=v_copy(v);qsort(r->F,r->n,8,cmp_f64);return r;}
    return v_copy(v);}
static V *bi_reverse(V**a,in){P(n<1,v_list(0))V*v=a[0];
    if(v->t==T_IVEC){V*r=v_ivec(v->n);for(int64_t i=0;i<v->n;i++)r->J[i]=v->J[v->n-1-i];return r;}
    if(v->t==T_CVEC){V*r=v_cvec(v->n);for(int64_t i=0;i<v->n;i++)r->B[i]=v->B[v->n-1-i];return r;}
    if(v->t==T_FVEC){V*r=v_fvec(v->n);for(int64_t i=0;i<v->n;i++)r->F[i]=v->F[v->n-1-i];return r;}
    if(v->t==T_LIST){V*r=v_list(v->n);for(int64_t i=0;i<v->n;i++)r->L[i]=v_ref(v->L[v->n-1-i]);return r;}
    if(v->t==T_STR){int64_t sl=strlen(v->s);char*b=malloc(sl+1);for(int64_t i=0;i<sl;i++)b[i]=v->s[sl-1-i];b[sl]=0;V*r=v_str(b);free(b);return r;}
    return v_copy(v);}
static V *bi_zip(V**a,in){
    P(n<2,v_list(0))int64_t ml=a[0]->n;for(int i=1;i<n;i++)if(a[i]->n<ml)ml=a[i]->n;
    if(n==2&&a[0]->t==T_IVEC&&a[1]->t==T_IVEC){
        V*r=v_list(ml);
        for(int64_t i=0;i<ml;i++){
            V*u=v_ivec(2);
            u->J[0]=a[0]->J[i];
            u->J[1]=a[1]->J[i];
            r->L[i]=u;
        }
        return r;
    }
    V*r=v_list(ml);for(int64_t i=0;i<ml;i++){V*u=v_list(n);
        for(int j=0;j<n;j++){if(a[j]->t==T_IVEC)u->L[j]=v_int(a[j]->J[i]);
            else if(a[j]->t==T_FVEC)u->L[j]=v_float(a[j]->F[i]);
            else if(a[j]->t==T_CVEC)u->L[j]=v_char(a[j]->F[i]);
            else if(a[j]->t==T_LIST)u->L[j]=v_ref(a[j]->L[i]);else u->L[j]=v_nil();}
        r->L[i]=u;}return r;}
static V *bi_enumerate(V**a,in){
    P(n<1,v_list(0))V*v=a[0];int64_t cnt=v->t==T_STR?(int64_t)strlen(v->s):v->n;
    V*r=v_list(cnt);
    if(v->t==T_IVEC){
        for(int64_t i=0;i<cnt;i++){
            V*u=v_ivec(2);
            u->J[0]=i;
            u->J[1]=v->J[i];
            r->L[i]=u;
        }
        return r;
    }
    for(int64_t i=0;i<cnt;i++){
        V*u=v_list(2);
        u->L[0]=v_int(i);
        if(v->t==T_FVEC)u->L[1]=v_float(v->F[i]);
        else if(v->t==T_CVEC)u->L[1]=v_char(v->B[i]);
        else if(v->t==T_LIST)u->L[1]=v_ref(v->L[i]);
        else if(v->t==T_STR){char b[2]={v->s[i],0};u->L[1]=v_str(b);}
        else u->L[1]=v_nil();
        r->L[i]=u;
    }
    return r;
}
static V *bi_map(V**a,in,Env*e){
    P(n<2||a[0]->t!=T_FN,v_err("map(fn,iter)"))
    V*fn=a[0],*iter=a[1];int64_t cnt=iter->t==T_STR?(int64_t)strlen(iter->s):iter->n;
    V*r=v_list(cnt);
    if(fn->n==-1) {
        for(int64_t i=0;i<cnt;++i){
            V*item;if(iter->t==T_IVEC)item=v_int(iter->J[i]);else if(iter->t==T_FVEC)item=v_float(iter->F[i]);
            else if(iter->t==T_CVEC)item=v_char(iter->B[i]);
            else if(iter->t==T_LIST)item=v_ref(iter->L[i]);else if(iter->t==T_STR){char b[2]={iter->s[i],0};item=v_str(b);}
            else item=v_nil();
            V*rv=builtin_call(fn->s,&item,1,NULL,NULL,0,e);
            v_free(item);
            if(g_returning){g_returning=0;v_free(rv);rv=g_retval;g_retval=NULL;}
            r->L[i]=rv;
        }
    } else {
        for(int64_t i=0;i<cnt;i++){
            V*item;if(iter->t==T_IVEC)item=v_int(iter->J[i]);else if(iter->t==T_FVEC)item=v_float(iter->F[i]);
            else if(iter->t==T_CVEC)item=v_char(iter->B[i]);
            else if(iter->t==T_LIST)item=v_ref(iter->L[i]);else if(iter->t==T_STR){char b[2]={iter->s[i],0};item=v_str(b);}
            else item=v_nil();
            Env*ce=env_new(fn->closure);if(fn->params->n>0)env_set(ce,fn->params->L[0]->s,item);v_free(item);
            V*rv=eval(fn_ast[(int)fn->j],ce);if(g_returning){g_returning=0;v_free(rv);rv=g_retval;g_retval=NULL;}
            r->L[i]=rv;env_free(ce);
        }
    }
    return r;
}
static V *bi_filter(V**a,in,Env*e){
    P(n<2||a[0]->t!=T_FN,v_err("filter(fn,iter)"))
    V*fn=a[0],*iter=a[1];int64_t cnt=iter->t==T_STR?(int64_t)strlen(iter->s):iter->n;
    V**tmp=calloc(cnt?cnt:1,sizeof(V*));int64_t out=0;
    if(fn->n==-1) {
        for(int64_t i=0;i<cnt;++i){
            V*item;if(iter->t==T_IVEC)item=v_int(iter->J[i]);else if(iter->t==T_FVEC)item=v_float(iter->F[i]);
            else if(iter->t==T_LIST)item=v_ref(iter->L[i]);else if(iter->t==T_STR){char b[2]={iter->s[i],0};item=v_str(b);}
            else if(iter->t==T_CVEC)item=v_char(iter->B[i]);
            else item=v_nil();
            V*rv=builtin_call(fn->s,&item,1,NULL,NULL,0,e);
            if(g_returning){g_returning=0;v_free(rv);rv=g_retval;g_retval=NULL;}
            int keep=rv&&((rv->t==T_BOOL&&rv->b)||(rv->t==T_INT&&rv->j)||(rv->t!=T_NIL&&rv->t!=T_BOOL&&rv->t!=T_INT));
            v_free(rv);
            if(keep)tmp[out++]=item;else v_free(item);
        }
    } else {
        for(int64_t i=0;i<cnt;i++){
            V*item;if(iter->t==T_IVEC)item=v_int(iter->J[i]);else if(iter->t==T_FVEC)item=v_float(iter->F[i]);
            else if(iter->t==T_LIST)item=v_ref(iter->L[i]);else if(iter->t==T_STR){char b[2]={iter->s[i],0};item=v_str(b);}
            else if(iter->t==T_CVEC)item=v_char(iter->B[i]);
            else item=v_nil();
            Env*ce=env_new(fn->closure);if(fn->params->n>0)env_set(ce,fn->params->L[0]->s,item);
            V*rv=eval(fn_ast[(int)fn->j],ce);if(g_returning){g_returning=0;v_free(rv);rv=g_retval;g_retval=NULL;}
            int keep=rv&&((rv->t==T_BOOL&&rv->b)||(rv->t==T_INT&&rv->j)||(rv->t!=T_NIL&&rv->t!=T_BOOL&&rv->t!=T_INT));
            v_free(rv);env_free(ce);if(keep)tmp[out++]=item;else v_free(item);
        }
    }
    V*r=v_list(out);memcpy(r->L,tmp,out*sizeof(V*));free(tmp);return r;
}
static V *bi_append(V**a,in){P(n<2||a[0]->t!=T_LIST,v_err("append(list,val)"))
    a[0]->L=realloc(a[0]->L,(a[0]->n+1)*sizeof(V*));a[0]->L[a[0]->n++]=v_ref(a[1]);return v_nil();}
static V *bi_pop(V**a,in){P(n<1||a[0]->t!=T_LIST||a[0]->n==0,v_err("pop"))
    return a[0]->L[--a[0]->n];}
static V *bi_keys(V**a,in){return n>0&&(a[0]->t==T_DICT||a[0]->t==T_TABLE)?v_copy(a[0]->keys):v_list(0);}
static V *bi_values(V**a,in){return n>0&&(a[0]->t==T_DICT||a[0]->t==T_TABLE)?v_copy(a[0]->vals):v_list(0);}
static V*check_table(V*r) {
    i(r->keys->n,if(r->vals->L[i]->n < r->n){
        printf("table column %s has wrong length (%lld != %lld)\n", r->keys->L[i]->s, (long long)r->vals->L[i]->n,
                (long long)r->n);
        r->n=r->vals->L[i]->n;
    })
    return r;
}
static V *bi_table(V**a,in,V**kwn,V**kwv,int nkw){
    P(n==1&&a[0]->t==T_DICT,check_table(v_table(a[0]->keys,a[0]->vals)))
    if(nkw>0){V*p=v_list(nkw),*d=v_list(nkw);
        for(int i=0;i<nkw;i++){p->L[i]=v_ref(kwn[i]);d->L[i]=v_ref(kwv[i]);}
        V*r=v_table(p,d);v_free(p);v_free(d); return check_table(r);}
    return v_err("table()");}
static V *bi_columns(V**a,in){return n>0&&a[0]->t==T_TABLE?v_copy(a[0]->keys):v_err("columns()");}
static V *bi_shape(V**a,in){
    P(n<1,v_err("shape()"))
    if (a[0]->t == T_TABLE) {
        V*r=v_list(2);r->L[0]=v_int(a[0]->n);r->L[1]=v_int(a[0]->keys->n);return r;
    }
    P(a[0]->t<T_IMAT||a[0]->t>T_BMAT,v_err("shape()"))
    V*r=v_list(2);r->L[0]=v_int(a[0]->n);r->L[1]=v_int(mat_cols(a[0]));return r;}
static V *bi_head(V**a,in){
    P(n<1,v_nil())int64_t cnt=n>=2&&a[1]->t==T_INT?a[1]->j:5;V*v=a[0];
    if(v->t==T_IVEC){int64_t m=cnt<v->n?cnt:v->n;V*r=v_ivec(m);memcpy(r->J,v->J,m*8);return r;}
    if(v->t==T_FVEC){int64_t m=cnt<v->n?cnt:v->n;V*r=v_fvec(m);memcpy(r->F,v->F,m*8);return r;}
    if(v->t==T_CVEC){int64_t m=cnt<v->n?cnt:v->n;V*r=v_cvec(m);memcpy(r->B,v->B,m);return r;}
    if(v->t==T_LIST){int64_t m=cnt<v->n?cnt:v->n;V*r=v_list(m);for(int64_t i=0;i<m;i++)r->L[i]=v_ref(v->L[i]);return r;}
    if(v->t==T_TABLE){int64_t m=cnt<v->n?cnt:v->n;int nc=v->keys->n;V*nd=v_list(nc);
        j(nc,{V*col=v->vals->L[j];
            if(col->t==T_IVEC){V*x=v_ivec(m);memcpy(x->J,col->J,m*8);nd->L[j]=x;}
            else if(col->t==T_FVEC){V*x=v_fvec(m);memcpy(x->F,col->F,m*8);nd->L[j]=x;}
            else if(col->t==T_LIST){V*x=v_list(m);for(int64_t i=0;i<m;i++)x->L[i]=v_ref(col->L[i]);nd->L[j]=x;}
            else nd->L[j]=v_ref(col);})
        V*r=v_table(v->keys,nd);v_free(nd);return r;}
    return v_nil();}
static V *bi_tail(V**a,in){
    P(n<1,v_nil())int64_t cnt=n>=2&&a[1]->t==T_INT?a[1]->j:5;V*v=a[0];
    int64_t start=v->n>cnt?v->n-cnt:0,m=v->n-start;
    if(v->t==T_IVEC){V*r=v_ivec(m);memcpy(r->J,v->J+start,m*8);return r;}
    if(v->t==T_FVEC){V*r=v_fvec(m);memcpy(r->F,v->F+start,m*8);return r;}
    if(v->t==T_CVEC){V*r=v_cvec(m);memcpy(r->B,v->B+start,m);return r;}
    if(v->t==T_LIST){V*r=v_list(m);for(int64_t i=0;i<m;i++)r->L[i]=v_ref(v->L[start+i]);return r;}
    if(v->t==T_TABLE){
        int nc=v->keys->n;V*nd=v_list(nc);
        j(nc,{V*col=v->vals->L[j];
            if(col->t==T_IVEC){V*x=v_ivec(m);memcpy(x->J,col->J+start,m*8);nd->L[j]=x;}
            else if(col->t==T_FVEC){V*x=v_fvec(m);memcpy(x->F,col->F+start,m*8);nd->L[j]=x;}
            else if(col->t==T_LIST){V*x=v_list(m);for(int64_t i=0;i<m;i++)x->L[i]=v_ref(col->L[start+i]);nd->L[j]=x;}
            else nd->L[j]=v_ref(col);})
        V*r=v_table(v->keys,nd);v_free(nd);return r;}
    return v_nil();}
static V *bi_group_sum(V**a,in){
    if(n<3||a[0]->t!=T_TABLE) {
        char buf[128];
        snprintf(buf, sizeof(buf), "group_sum(table,group_col,sum_col) - got nargs=%d, type0=%s", n, n>0?type_name(a[0]->t):"none");
        return v_err(buf);
    }
    V*tbl=a[0],*gc=v_nil(),*sc=v_nil();
    for(int i=0;i<tbl->keys->n;i++){
        if(!strcmp(tbl->keys->L[i]->s,a[1]->s))gc=tbl->vals->L[i];
        if(!strcmp(tbl->keys->L[i]->s,a[2]->s))sc=tbl->vals->L[i];}
    P(gc->t==T_NIL||sc->t==T_NIL,v_err("column not found"))
    V*k=v_list(0),*v=v_list(0);V*res=v_dict(k,v);v_free(k);v_free(v);
    for(int64_t i=0;i<tbl->n;i++){
        const char*gs=(gc->t==T_STR)?gc->s:(gc->t==T_LIST?gc->L[i]->s:"?");
        double gv=(sc->t==T_FVEC)?sc->F[i]:sc->t==T_CVEC?(double)sc->B[i]:(sc->t==T_IVEC?(double)sc->J[i]:0);
        V*cur=v_dict_get(res,gs);
        if(!cur){V*cv=v_float(gv);v_dict_set(res,gs,cv);v_free(cv);}
        else{cur->f+=gv;}}
    return res;}
static V *bi_input(V **a, in) {
    input_hub_init();
    if (n > 0 && a[0]->t == T_STR)
        return v_input_stream(INPUT_STREAM_LINE, a[0]->s);
    if (n > 0 && a[0]->t == T_INT) {
        int64_t m = a[0]->j;
        if (m == 1) return v_input_stream(INPUT_STREAM_RAW, NULL);
        if (m == 2) return v_input_stream(INPUT_STREAM_KEY, NULL);
        return input_poll_ms((int)m);
    }
    if (n > 0 && a[0]->t == T_FLOAT) {
        if (a[0]->f != a[0]->f || a[0]->f >= 1e30) return input_wait_ms(INPUT_WAIT_FOREVER);
        return input_poll_ms((int)a[0]->f);
    }
    return input_readline("");
}
static V *bi_readline(V **a, in) {
    const char *prompt = (n > 0 && a[0]->t == T_STR) ? a[0]->s : "";
    return input_readline(prompt);
}
static V *bi_wait(V **a, in) {
    input_hub_init();
    if (n < 1) return input_wait_ms(0);
    if (a[0]->t == T_INT) {
        if (a[0]->j < 0) return input_wait_ms(INPUT_WAIT_FOREVER);
        return input_wait_ms(a[0]->j);
    }
    if (a[0]->t == T_FLOAT) {
        if (a[0]->f != a[0]->f || a[0]->f >= 1e30) return input_wait_ms(INPUT_WAIT_FOREVER);
        return input_wait_ms((int64_t)a[0]->f);
    }
    return input_wait_ms(0);
}
static V *bi_input_get_hz(V **a, in) { (void)a; (void)n; return v_int(input_get_hz()); }
static V *bi_input_set_hz(V **a, in) {
    P(n < 1 || a[0]->t != T_INT, v_err("input_set_hz(n)"))
    input_set_hz((int)a[0]->j);
    return v_nil();
}
static V *bi_input_get_x(V **a, in) { (void)a; (void)n; return v_float(input_get_x()); }
static V *bi_input_get_y(V **a, in) { (void)a; (void)n; return v_float(input_get_y()); }
static V *bi_input_get_wheel(V **a, in) { (void)a; (void)n; return v_float(input_get_wheel()); }
static V *bi_input_set_x(V **a, in) {
    P(n < 1 || a[0]->t != T_FLOAT, v_err("input_set_x(x)"))
    input_set_x(a[0]->f);
    return v_nil();
}
static V *bi_input_set_y(V **a, in) {
    P(n < 1 || a[0]->t != T_FLOAT, v_err("input_set_y(y)"))
    input_set_y(a[0]->f);
    return v_nil();
}
static V *bi_input_set_wheel(V **a, in) {
    P(n < 1 || a[0]->t != T_FLOAT, v_err("input_set_wheel(w)"))
    input_set_wheel(a[0]->f);
    return v_nil();
}
static V *bi_input_get_qwerty(V **a, in) { (void)a; (void)n; return input_get_qwerty(); }
static V *bi_input_set_own_gui(V **a, in) {
    P(n < 1 || a[0]->t != T_INT, v_err("input_set_own_gui(0|1)"))
    input_set_own_gui((int)a[0]->j);
    return v_nil();
}
static V *bi_input_qwerty_reload(V **a, in) {
    (void)a; (void)n;
    input_qwerty_reload();
    return v_nil();
}
static V *bi_repr(V**a,in){P(n<1,v_str("None"))char*r=v_repr(a[0]);V*v=v_str(r);free(r);return v;}
static V *bi_datetime(V **a, in) {
    P(n < 1 || a[0]->t != T_STR,v_err("datetime(str)"))
    int64_t ms;
    P(!shakti_parse_datetime_ms(a[0]->s, &ms),v_err("datetime: invalid format"))
    return v_datetime(ms);
}
static V *bi_format_datetime(V **a, in) {
    P(n < 1 || a[0]->t != T_DATETIME,v_err("format_datetime(dt)"))
    char buf[32];
    shakti_format_datetime_ms(a[0]->j, buf, sizeof buf);
    return v_str(buf);
}
static V *bi_date(V **a, in) {
    P(n < 1 || a[0]->t != T_STR,v_err("date(str)"))
    int64_t ms;
    P(!shakti_parse_date_ymd(a[0]->s, &ms),v_err("date: use YYYY-MM-DD"))
    return v_date(ms);
}
static V *bi_format_date(V **a, in) {
    P(n < 1 || a[0]->t != T_DATE,v_err("format_date(date)"))
    char buf[16];
    shakti_format_date_ms(a[0]->j, buf, sizeof buf);
    return v_str(buf);
}
static V *bi_time_ms_builtin(V **a, in) {
    P(n < 1 || a[0]->t != T_INT,v_err("time_ms(ms_since_midnight)"))
    int64_t ms = a[0]->j % 86400000LL;
    if(ms < 0) ms += 86400000LL;
    return v_time(ms);
}
static V *bi_format_time(V **a, in) {
    P(n < 1 || a[0]->t != T_TIME,v_err("format_time(time)"))
    char buf[24];
    shakti_format_time_ms(a[0]->j, buf, sizeof buf);
    return v_str(buf);
}
static V *bi_next(V**a,in){
    P(n<1||a[0]->t!=T_LIST||a[0]->n==0,n>1?v_ref(a[1]):v_nil())
    return v_ref(a[0]->L[0]);}
typedef V *(*BiCall)(V **a, in, V **kwn, V **kwv, int nkw, Env *e);
#define BI0(nm) static MS V *bi_w_##nm(V **a,in,V **k,V **v,int nk,Env *e){(void)k;(void)v;(void)nk;(void)e;return bi_##nm(a,n);}
#define BIKW(nm) static MS V *bi_w_##nm(V **a,in,V **k,V **v,int nk,Env *e){(void)e;return bi_##nm(a,n,k,v,nk);}
#define BIE(nm) static MS V *bi_w_##nm(V **a,in,V **k,V **v,int nk,Env *e){(void)k;(void)v;(void)nk;return bi_##nm(a,n,e);}
#define BIKWE(nm) static MS V *bi_w_##nm(V **a,in,V **k,V **v,int nk,Env *e){return bi_##nm(a,n,k,v,nk,e);}
BIKW(print)
BI0(len) BI0(range) BI0(type) BI0(int) BI0(float) BI0(str) BI0(list) BI0(bool) BI0(sleep)
BIKW(dict) BIKW(ktable) BI0(set)
BI0(sum) BI0(avg) BI0(min) BI0(max) BI0(dot) BI0(abs)
BI0(sqrt) BI0(floor) BI0(ceil) BI0(exp) BI0(log) BI0(sin) BI0(cos) BI0(tan)
BI0(sort) BI0(reverse) BI0(zip) BI0(enumerate)
BIE(map) BIE(filter) BIKWE(sorted)
BIKW(table) BI0(columns) BI0(shape) BI0(head) BI0(tail) BI0(group_sum)
BI0(append) BI0(pop) BI0(keys) BI0(values) BI0(next)
BI0(input) BI0(readline) BI0(wait) BI0(repr)
BI0(input_get_hz) BI0(input_set_hz)
BI0(input_get_x) BI0(input_get_y) BI0(input_get_wheel)
BI0(input_set_x) BI0(input_set_y) BI0(input_set_wheel)
BI0(input_get_qwerty) BI0(input_set_own_gui) BI0(input_qwerty_reload)
BI0(datetime) BI0(format_datetime) BI0(date) BI0(format_date) BI0(format_time)
static MS V *bi_w_time_ms(V **a,in,V **k,V **v,int nk,Env *e){
    (void)k;(void)v;(void)nk;(void)e;return bi_time_ms_builtin(a,n);}
BI0(fread) BI0(fwrite) BI0(readlines) BI0(listdir) BI0(walk) BI0(stat)
BI0(path_join) BI0(path_exists) BI0(path_isdir) BI0(path_isfile)
BI0(path_basename) BI0(path_dirname) BI0(path_splitext)
BI0(getcwd) BI0(mkdir) BI0(getenv) BI0(machine) BI0(sh)
BI0(re_findall) BI0(re_sub) BI0(re_match) BI0(re_split)
BI0(json_loads) BI0(json_dumps) BI0(json_load) BI0(json_dump)
BI0(any) BI0(all) BI0(isinstance) BI0(hasattr) BI0(getattr) BI0(chr) BI0(ord) BI0(hex)
#ifdef SHAKTI_HAVE_IPC
BI0(ipc_accept) BI0(ipc_close) BI0(ipc_connect) BI0(ipc_listen) BI0(ipc_poll)
BI0(ipc_recv) BI0(ipc_recv_nowait) BI0(ipc_rdma_available) BI0(ipc_send)
BI0(ipc_set_nonblock) BI0(ipc_shm_close) BI0(ipc_shm_open)
#endif
BI0(graph_create) BI0(graph_add) BI0(graph_query) BI0(graph_neighbors) BI0(graph_path)
BI0(graph_from_table) BI0(graph_to_table) BI0(graph_count) BI0(graph_clear)
#undef BI0
#undef BIKW
#undef BIE
#undef BIKWE
typedef struct { const char *name; BiCall fn; } BiEntry;
static int bi_name_cmp(const void *va, const void *vb) {
    return strcmp(((const BiEntry *)va)->name, ((const BiEntry *)vb)->name);
}

static const BiEntry bi_tab[] = {
    {"abs", bi_w_abs},
    {"all", bi_w_all},
    {"any", bi_w_any},
    {"append", bi_w_append},
    {"avg", bi_w_avg},
    {"bool", bi_w_bool},
    {"ceil", bi_w_ceil},
    {"chr", bi_w_chr},
    {"columns", bi_w_columns},
    {"cos", bi_w_cos},
    {"date", bi_w_date},
    {"datetime", bi_w_datetime},
    {"dict", bi_w_dict},
    {"dot", bi_w_dot},
    {"enumerate", bi_w_enumerate},
    {"exp", bi_w_exp},
    {"filter", bi_w_filter},
    {"float", bi_w_float},
    {"floor", bi_w_floor},
    {"format_date", bi_w_format_date},
    {"format_datetime", bi_w_format_datetime},
    {"format_time", bi_w_format_time},
    {"getattr", bi_w_getattr},
    {"getcwd", bi_w_getcwd},
    {"getenv", bi_w_getenv},
    {"graph_add", bi_w_graph_add},
    {"graph_clear", bi_w_graph_clear},
    {"graph_count", bi_w_graph_count},
    {"graph_create", bi_w_graph_create},
    {"graph_from_table", bi_w_graph_from_table},
    {"graph_neighbors", bi_w_graph_neighbors},
    {"graph_path", bi_w_graph_path},
    {"graph_query", bi_w_graph_query},
    {"graph_to_table", bi_w_graph_to_table},
    {"group_sum", bi_w_group_sum},
    {"hasattr", bi_w_hasattr},
    {"head", bi_w_head},
    {"hex", bi_w_hex},
    {"input", bi_w_input},
    {"input_get_hz", bi_w_input_get_hz},
    {"input_get_qwerty", bi_w_input_get_qwerty},
    {"input_get_wheel", bi_w_input_get_wheel},
    {"input_get_x", bi_w_input_get_x},
    {"input_get_y", bi_w_input_get_y},
    {"input_qwerty_reload", bi_w_input_qwerty_reload},
    {"input_set_hz", bi_w_input_set_hz},
    {"input_set_own_gui", bi_w_input_set_own_gui},
    {"input_set_wheel", bi_w_input_set_wheel},
    {"input_set_x", bi_w_input_set_x},
    {"input_set_y", bi_w_input_set_y},
    {"int", bi_w_int},
#ifdef SHAKTI_HAVE_IPC
    {"ipc_accept", bi_w_ipc_accept},
    {"ipc_close", bi_w_ipc_close},
    {"ipc_connect", bi_w_ipc_connect},
    {"ipc_listen", bi_w_ipc_listen},
    {"ipc_poll", bi_w_ipc_poll},
    {"ipc_rdma_available", bi_w_ipc_rdma_available},
    {"ipc_recv", bi_w_ipc_recv},
    {"ipc_recv_nowait", bi_w_ipc_recv_nowait},
    {"ipc_send", bi_w_ipc_send},
    {"ipc_set_nonblock", bi_w_ipc_set_nonblock},
    {"ipc_shm_close", bi_w_ipc_shm_close},
    {"ipc_shm_open", bi_w_ipc_shm_open},
#endif
    {"isinstance", bi_w_isinstance},
    {"json_dump", bi_w_json_dump},
    {"json_dumps", bi_w_json_dumps},
    {"json_load", bi_w_json_load},
    {"json_loads", bi_w_json_loads},
    {"keys", bi_w_keys},
    {"ktable", bi_w_ktable},
    {"len", bi_w_len},
    {"list", bi_w_list},
    {"listdir", bi_w_listdir},
    {"log", bi_w_log},
    {"machine", bi_w_machine},
    {"map", bi_w_map},
    {"max", bi_w_max},
    {"min", bi_w_min},
    {"mkdir", bi_w_mkdir},
    {"next", bi_w_next},
    {"ord", bi_w_ord},
    {"path_basename", bi_w_path_basename},
    {"path_dirname", bi_w_path_dirname},
    {"path_exists", bi_w_path_exists},
    {"path_isdir", bi_w_path_isdir},
    {"path_isfile", bi_w_path_isfile},
    {"path_join", bi_w_path_join},
    {"path_splitext", bi_w_path_splitext},
    {"pop", bi_w_pop},
    {"print", bi_w_print},
    {"range", bi_w_range},
    {"re_findall", bi_w_re_findall},
    {"re_match", bi_w_re_match},
    {"re_split", bi_w_re_split},
    {"re_sub", bi_w_re_sub},
    {"read", bi_w_fread},
    {"readline", bi_w_readline},
    {"readlines", bi_w_readlines},
    {"repr", bi_w_repr},
    {"reverse", bi_w_reverse},
    {"set", bi_w_set},
    {"sh", bi_w_sh},
    {"shape", bi_w_shape},
    {"sin", bi_w_sin},
    {"sleep", bi_w_sleep},
    {"sort", bi_w_sort},
    {"sorted", bi_w_sorted},
    {"sqrt", bi_w_sqrt},
    {"stat", bi_w_stat},
    {"str", bi_w_str},
    {"sum", bi_w_sum},
    {"table", bi_w_table},
    {"tail", bi_w_tail},
    {"tan", bi_w_tan},
    {"time_ms", bi_w_time_ms},
    {"type", bi_w_type},
    {"values", bi_w_values},
    {"wait", bi_w_wait},
    {"walk", bi_w_walk},
    {"write", bi_w_fwrite},
    {"zip", bi_w_zip},
};
const char *builtin_complete(const char *s,in) {
    for(int i=0;i<sizeof(bi_tab)/sizeof(*bi_tab);++i)
        if(strncmp(bi_tab[i].name,s,n)==0)return bi_tab[i].name;
    return NULL;
}
static BiCall bi_find(const char *name) {
    BiEntry key = {name, NULL};
    const BiEntry *hit = bsearch(&key, bi_tab, sizeof bi_tab / sizeof bi_tab[0], sizeof *hit, bi_name_cmp);
#ifndef NDEBUG
    if(!hit) {
          static int order_report = 0;
          if(!order_report)for(int i=1;i<sizeof(bi_tab)/sizeof(*bi_tab);++i,order_report=1){
              int g=strcmp(bi_tab[i].name,bi_tab[i-1].name);
              if(g<1){
                  printf("%d is out of order (near %s <=> %s)\n",i,bi_tab[i].name,bi_tab[i-1].name);
              }
          }
    }
#endif
    return hit ? hit->fn : NULL;
}
int is_builtin(const char *name){
  if(bi_find(name)!=NULL)return 1;
  if(strcmp(name,"clock")==0)return 1;if(strcmp(name,"timer")==0)return 1;
  if(strcmp(name,"assert")==0)return 1;
  if(strcmp(name,"save_context")==0)return 1;
  if(strcmp(name,"load_context")==0)return 1;
  if(strcmp(name,"load")==0)return 1;
  if(strcmp(name,"__apply__")==0)return 1;
  if(strcmp(name,"__invoke__")==0)return 1;
  return 0;
}
__thread Env*shakti_builtin_call_env = NULL;
V *builtin_call(const char *name,V **args,int nargs,V **kwn,V **kwv,int nkw,Env *e){
    BiCall fn;
    shakti_builtin_call_env = e;
    if(!strcmp(name,"clock") || !strcmp(name,"timer")){
        struct timespec tb;
        clock_gettime(CLOCK_MONOTONIC, &tb);
        return v_float(tb.tv_sec + tb.tv_nsec / 1e9);
    }
    if(!strcmp(name,"assert")){
        P(nargs<1,v_err("assert(condition[, message])"))
        V*cond=args[0];int ok=0;
        if(cond->t==T_BOOL)ok=cond->b;else if(cond->t==T_INT)ok=cond->j!=0;
        else if(cond->t==T_FLOAT)ok=cond->f!=0;else if(cond->t==T_STR)ok=cond->s[0]!=0;
        else if(cond->t==T_NIL || cond->t==T_ERR)ok=0;else ok=1;
        if(!ok){
            const char *msg=nargs>1&&args[1]->t==T_STR?args[1]->s:"assertion failed";
            fprintf(stderr,"AssertionError: %s\n",msg);exit(1);}
        return v_nil();}
    if(!strcmp(name,"save_context")){
        P(nargs<1,v_err("save_context(path)"))
        return env_save(e,args[0]->s)?v_nil():v_err("save failed");}
    if(!strcmp(name,"load_context")){
        P(nargs<1,v_err("load_context(path)"))
        return env_load(e,args[0]->s)?v_nil():v_err("load failed");}
    fn = bi_find(name);
    if(fn) return fn(args, nargs, kwn, kwv, nkw, e);
    if(!strcmp(name,"__apply__")){
        P(nargs<2,v_err("__apply__(f,args)"))
        V*fnv=args[0];V*arg=args[1];
        P(fnv->t!=T_FN,v_err("__apply__: not a function"))
        P(arg->t==T_LIST,builtin_call("__invoke__", (V*[]){fnv,arg}, 2, NULL, NULL, 0, e))
        V*al=v_list(1);al->L[0]=v_ref(arg);
        V*r=builtin_call("__invoke__", (V*[]){fnv,al}, 2, NULL, NULL, 0, e);
        v_free(al);return r;
    }
    if(!strcmp(name,"__invoke__")){
        V*fnv=args[0],*al=args[1];
        P(fnv->n == -1,builtin_call(fnv->s, al->L, al->n, NULL, NULL, 0, e))
        Env*ce=env_new(fnv->closure);V*p=fnv->params;
        for(int i=0;i<p->n && i<al->n;i++) {
            if(!p->L[i]->b) {
                env_set(ce,p->L[i]->s,al->L[i]);
            } else if(p->L[i]->b == 1) {
                V*rest = v_list(al->n - i);int j=i;i(al->n-i,rest->L[i]=al->L[i+j]);
                env_set(ce,p->L[i]->s,rest);
            } else if(p->L[i]->b == 2) {
                V*keys = v_list(nkw), *vals = v_list(nkw); i(nkw,keys->L[i]=kwn[i];vals->L[i]=kwv[i]);
                env_set(ce,p->L[i]->s,v_dict(keys,vals));
            }
        }
        Node*body=fn_ast[(int)fnv->j];V*rv=eval(body,ce);
        if(g_returning){g_returning=0;v_free(rv);rv=g_retval;g_retval=NULL;}
        env_free(ce);return rv;
    }
    if(!strcmp(name,"load")) {
        if(nargs < 1 || args[0]->t != T_STR) {
            return v_err("load(path, ...)");
        }
        char*ext;
        if(!chdir(args[0]->s)) {
            V*result=NULL;FILE*f=fopen(".d","rb");
            i(nkw,if(kwn[i]&&kwn[i]->t==T_STR&&!strcmp(kwn[i]->s,"__dir__")){result=kwv[i];break;})
            if(!result) result = env_get(e,"__dir__");
            if(result && result->t == T_STR) setenv("__dir__", result->s, 1);else result=NULL;
            if(f) {
                V*keys = v_deserialize(f);
                V*vals = v_list(keys->n);
                fclose(f);
                i(keys->n,{
                     f=fopen(keys->L[i]->s,"rb");
                     vals->L[i] = v_deserialize(f);
                     fclose(f);
                });
                return check_table(v_table(keys,vals));
            } else if(result && (result=subprocess(args+1,nargs-1))) {
            } else if(nargs > 1) {
                V*keys = v_list(nargs-1);
                V*vals = v_list(nargs-1);
                i(nargs-1,keys->L[i]=v_ref(v_ref(args[i]));vals->L[i]=builtin_call("load",args+i,1,kwn,kwv,nkw,e));
                result=check_table(v_table(keys,vals));
            } else {
                V*keys = v_list(0);
                V*vals = v_list(0);
                DIR*dir=opendir(".");
                struct dirent*d;
                int pn = strlen(args[0]->s);
                while((d=readdir(dir))) {
                    if(*d->d_name == '.')continue;
                    v_list_append(keys, v_str(d->d_name));
                    int sn = pn+ strlen(d->d_name)+2;
                    char *s = malloc(sn);
                    snprintf(s,sn,"%s/%s",args[0]->s,d->d_name);
                    V*fun = v_fn(NULL, v_str(s), NULL, NULL);
                    v_list_append(vals, fun);free(s);
                }
                closedir(dir);
                result=v_dict(keys,vals);
            }
            extern int start_dir;
            fchdir(start_dir);
            return result;
        }
        if((ext=strrchr(args[0]->s,'.'))&&strcmp(ext,".sql")) {
            if(!strcmp(ext,".xml")) {
                V*columns_opt = v_list(0);i(nargs-1,columns_opt->L[i]=args[i]);
                return table_xml_load(args[0]->s,columns_opt);
            }
            V *mod_dict = v_ref(load_module(ext+1));
            V *impl = mod_dict ? v_dict_get(mod_dict, "load") : NULL;
            if(impl) {
                if(impl->n == -1) return builtin_call(impl->s,args,nargs,kwn,kwv,nkw,e);
                Env *call_env = env_new(impl->closure);
                V *params = impl->params;
                for(int i=0; i<params->n; i++) {
                    V*p = params->L[i];
                    if(p->b == 1) {
                        V*rest = v_list(0);
                        for(int j=i; j<nargs;++j) v_list_append(rest, args[j]);
                        env_set(call_env, p->s, rest);
                    } else if (p->b == 2) {
                        V*keys=v_list(nkw),*vals=v_list(nkw);i(nkw,keys->L[i]=kwn[i];vals->L[i]=kwv[i]);
                        env_set(call_env, p->s, v_dict(keys,vals));
                    } else if(i < nargs) {
                        env_set(call_env, p->s, args[i]);
                    } else if(impl->defaults && i < impl->defaults->n) {
                        env_set(call_env, p->s, impl->defaults->L[i]);
                    }
                }
                i(nkw,env_set(call_env,kwn[i]->s,kwv[i]));
                env_set(call_env, "self", mod_dict);
                Node *body = fn_ast[(int)impl->j];
                V *result = eval(body, call_env);
                if(g_returning) {
                    g_returning = 0; v_free(result);
                    result = g_retval ? g_retval : v_nil();
                    g_retval = NULL;
                }
                env_free(call_env);
                return result;
            }
        }
        FILE*f=fopen(args[0]->s,"rb");
        fseek(f,0, SEEK_END);
        off_t size = ftell(f);
        fseek(f,0, SEEK_SET);
        V*result = v_deserialize(f);
        if((!result || result->t == T_NIL) && ftell(f) < size) {
            if(result) v_free(result);
            char *src=read_file(args[0]->s);
            if(!src) return v_err("load failed");

            Node *body = strcmp(ext,".sql")==0?parse_sql(src):parse(src);
            i(nkw,env_set(e,kwn[i]->s,kwv[i]));
            result = eval(body, e);
            if(g_returning) {
                g_returning = 0; v_free(result);
                result = g_retval ? g_retval : v_nil();
                g_retval = NULL;
            }
        }
        fclose(f);
        return result;
    }
    if(!strcmp(name,"save")) {
        if(nargs < 2 || args[1]->t != T_STR) {
            return v_err("save(object, path, ...)");
        }
        char*ext;
        if(!chdir(args[1]->s)) {
            V*result;
            if(args[0]->t == T_TABLE) {
                i(args[0]->keys->n,{
                    FILE*f=fopen(args[0]->keys->L[i]->s,"wb");
										v_serialize(args[0]->vals->L[i], f);
										fclose(f);
                });
                FILE*f=fopen(".d","wb");
                v_serialize(args[0]->keys,f);
								fclose(f);
								result = v_int(args[0]->n);
            } else result = v_err("nyi");
            extern int start_dir;
            fchdir(start_dir);
            return result;
        }
        if((ext=strrchr(args[1]->s,'.'))&&strcmp(ext,".sql")) {
            V *mod_dict = v_ref(load_module(ext+1));
            V *impl = mod_dict ? v_dict_get(mod_dict, "save") : NULL;
            if(impl) {
                if(impl->n == -1) return builtin_call(impl->s,args,nargs,kwn,kwv,nkw,e);
                Env *call_env = env_new(impl->closure);
                V *params = impl->params;
                for(int i=0; i<params->n; i++) {
                    V*p = params->L[i];
                    if(p->b == 1) {
                        V*rest = v_list(0);
                        for(int j=i; j<nargs;++j) v_list_append(rest, args[j]);
                        env_set(call_env, p->s, rest);
                    } else if (p->b == 2) {
                        V*keys=v_list(nkw),*vals=v_list(nkw);i(nkw,keys->L[i]=kwn[i];vals->L[i]=kwv[i]);
                        env_set(call_env, p->s, v_dict(keys,vals));
                    } else if(i < nargs) {
                        env_set(call_env, p->s, args[i]);
                    } else if(impl->defaults && i < impl->defaults->n) {
                        env_set(call_env, p->s, impl->defaults->L[i]);
                    }
                }
                i(nkw,env_set(call_env,kwn[i]->s,kwv[i]));
                env_set(call_env, "self", mod_dict);
                Node *body = fn_ast[(int)impl->j];
                V *result = eval(body, call_env);
                if(g_returning) {
                    g_returning = 0; v_free(result);
                    result = g_retval ? g_retval : v_nil();
                    g_retval = NULL;
                }
                env_free(call_env);
                return result;
            }
        }
        FILE*f = fopen(args[1]->s,"wb");
        v_serialize(args[0], f);
        off_t n = ftell(f);
        fclose(f);
        return v_int(n);
    }
    return v_errf("unknown builtin '%s'",name);
}
void builtin_register(Env *e){shakti_builtin_call_env=e;}
