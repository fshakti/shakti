#include "shakti.h"
#include "vec_kernels.h"
#include "input.h"
#include <time.h>
extern int is_isolde_builtin(const char *name);
extern V *isolde_builtin_call(const char *name, V **args, int nargs);
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
extern V *bi_synth_vu(V**,in);
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
extern V *bi_gfx_open(V**,in);
extern V *bi_gfx_close(V**,in);
extern V *bi_gfx_alive(V**,in);
extern V *bi_gfx_available(V**,in);
extern V *bi_gfx_tick(V**,in);
extern V *bi_gfx_sync_keys(V**,in);
extern V *bi_gfx_clear(V**,in);
extern V *bi_gfx_fill_rect(V**,in);
extern V *bi_gfx_line(V**,in);
extern V *bi_gfx_fill_circle(V**,in);
extern V *bi_gfx_click_pending(V**,in);
extern V *bi_gfx_click_x(V**,in);
extern V *bi_gfx_click_y(V**,in);
extern V *bi_gfx_consume_click(V**,in);
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
extern V *bi_rest_request(V**,in);
extern V *bi_rest_get(V**,in);
extern V *bi_rest_post(V**,in);
extern V *bi_rest_put(V**,in);
extern V *bi_rest_delete(V**,in);
extern V *bi_rest_listen(V**,in);
extern V *bi_rest_accept(V**,in);
extern V *bi_rest_read(V**,in);
extern V *bi_rest_write(V**,in);
extern V *bi_rest_close(V**,in);
extern V *bi_pcm_open(V**,in);
extern V *bi_pcm_write(V**,in);
extern V *bi_pcm_close(V**,in);
static const char *BUILTINS[] = {
    "print","len","range","type","int","float","str","list","bool",
    "sum","avg","min","max","dot","mmul","abs","sqrt","floor","ceil","exp","log","sin","cos","tan",
    "bin","asof_sort","asof_bin","sort","reverse","zip","enumerate","map","filter",
    "table","columns","shape","head","tail","group_sum",
    "append","pop","keys","values",
    "load","save","input","readline","wait","repr","clock","timer",
    "input_get_hz","input_set_hz","input_get_x","input_get_y","input_get_wheel",
    "input_key_down","input_keys_clear",
    "input_set_x","input_set_y","input_set_wheel","input_get_qwerty","input_set_own_gui","input_qwerty_reload",
    "ipc_accept","ipc_close","ipc_connect","ipc_listen","ipc_poll","ipc_recv","ipc_recv_nowait",
    "ipc_rdma_available","ipc_send","ipc_set_nonblock","ipc_shm_close","ipc_shm_open",
    "graph_create","graph_add","graph_query","graph_neighbors","graph_path",
    "graph_from_table","graph_to_table","graph_count","graph_clear",
    "rest_request","rest_get","rest_post","rest_put","rest_delete",
    "rest_listen","rest_accept","rest_read","rest_write","rest_close",
    "pcm_open","pcm_write","pcm_close",
    "read","write","readlines",
    "listdir","walk","stat",
    "path_join","path_exists","path_isdir","path_isfile",
    "path_basename","path_dirname","path_splitext",
    "getcwd","mkdir","getenv",
    "machine",
    "sh",
    "re_findall","re_sub","re_match","re_split",
    "json_loads","json_dumps","json_load","json_dump",
    "sorted","any","all","isinstance","hasattr","getattr",
    "chr","ord","hex",
    "dict","ktable","set",
    "next","assert",
    "datetime","format_datetime","date","format_date","time_ms","format_time",
    "save_context","load_context",
    "talk_listen","talk_set_locale","talk_set_model",
    "synth_open","synth_close","synth_alive","synth_tick","synth_set_steps","synth_steps",
    "synth_set_metro","synth_metro_on","synth_set_metro_sound","synth_metro_sound",
    "synth_set_mute","synth_mute_on",
    "synth_note_on","synth_note_off","synth_set_bpm","synth_bpm",
    "synth_set_tuning","synth_tuning",
    "synth_set_level","synth_level","synth_set_cutoff","synth_cutoff",
    "synth_set_reso","synth_reso","synth_set_seq_row","synth_play","synth_playing",
    "synth_mouse_press","synth_mouse_release","synth_set_viz","synth_viz_mode","synth_vu",
    "synth_load_sample","synth_sample_loaded","synth_sample_name",
    "synth_set_row_note","synth_row_note",
    "synth_looper_rec","synth_looper_play","synth_looper_clear","synth_looper_overdub",
    "synth_looper_rec_on","synth_looper_play_on","synth_looper_has_loop",
    "gfx_open","gfx_close","gfx_alive","gfx_available","gfx_tick","gfx_sync_keys",
    "gfx_clear","gfx_fill_rect","gfx_line","gfx_fill_circle",
    "gfx_click_pending","gfx_click_x","gfx_click_y","gfx_consume_click",
    NULL
};
int is_builtin(const char *name){if(is_isolde_builtin(name))return 1;for(int i=0;BUILTINS[i];i++)P(!strcmp(name,BUILTINS[i]),1)return 0;}

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
    P(v->t==T_DICT||v->t==T_TABLE,v_int(v->n))
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
    P(v->t==T_INT,v_int(v->j))P(v->t==T_FLOAT,v_int((int64_t)v->f))
    P(v->t==T_BOOL,v_int(v->b))P(v->t==T_STR,v_int(strtoll(v->s,NULL,0)))
    return v_err("cannot convert to int");}
static V *bi_float(V**a,in){
    P(n<1,v_float(0))V*v=a[0];
    P(v->t==T_FLOAT,v_float(v->f))P(v->t==T_INT,v_float((double)v->j))
    P(v->t==T_BOOL,v_float(v->b))P(v->t==T_STR,v_float(strtod(v->s,NULL)))
    return v_err("cannot convert to float");}
static V *bi_str(V**a,in){
    P(n<1,v_str(""))
    P(a[0]->t==T_STR,v_str(a[0]->s))
    char *s=v_to_str(a[0]);V*r=v_str(s);free(s);return r;}
static V *bi_list(V**a,in){
    P(n<1,v_list(0))V*v=a[0];
    if(v->t==T_IVEC){V*r=v_list(v->n);for(int64_t i=0;i<v->n;i++)r->L[i]=v_int(v->J[i]);return r;}
    if(v->t==T_FVEC){V*r=v_list(v->n);for(int64_t i=0;i<v->n;i++)r->L[i]=v_float(v->F[i]);return r;}
    P(v->t==T_LIST,v_copy(v))
    if(v->t==T_STR){int64_t sl=strlen(v->s);V*r=v_list(sl);for(int64_t i=0;i<sl;i++){char b[2]={v->s[i],0};r->L[i]=v_str(b);}return r;}
    if(v->t==T_DICT){V*r=v_copy(v->keys);return r;}
    return v_err("cannot convert to list");}
static V *bi_bool(V**a,in){
    P(n<1,v_bool(0))V*v=a[0];
    P(v->t==T_BOOL,v_bool(v->b))P(v->t==T_INT,v_bool(v->j!=0))
    P(v->t==T_FLOAT,v_bool(v->f!=0))P(v->t==T_STR,v_bool(v->s[0]!=0))
    P(v->t==T_NIL,v_bool(0))return v_bool(1);}
static int bi_numvec(V *v) { return v->t == T_IVEC || v->t == T_FVEC || v->t == T_IMAT || v->t == T_FMAT; }
static int64_t mat_nelem(V *v) { return v->n * mat_cols(v); }
static V *vec_reduce_sum(V *v) {
    if (v->t == T_IMAT) {
        int64_t s = 0;
        int64_t ne = mat_nelem(v);
        for (int64_t i = 0; i < ne; i++) s += v->J[i];
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
    if (v->t == T_FMAT) {
        P(v->n == 0 || mat_cols(v) == 0,v_nil())
        double m = v->F[0];
        int64_t ne = mat_nelem(v);
        for (int64_t i = 1; i < ne; i++) if (v->F[i] < m) m = v->F[i];
        return v_float(m);
    }
    if (v->t == T_IVEC) {
        P(v->n == 0,v_nil())
        if (v->n >= ISL_OMP_VEC_MIN)
            return v_int(shakti_min_i64(v->J, v->n));
        int64_t m = v->J[0];
        for (int64_t i = 1; i < v->n; i++) if (v->J[i] < m) m = v->J[i];
        return v_int(m);
    }
    if (v->t == T_FVEC) {
        P(v->n == 0,v_nil())
        if (v->n >= ISL_OMP_VEC_MIN)
            return v_float(shakti_min_f64(v->F, v->n));
        double m = v->F[0];
        for (int64_t i = 1; i < v->n; i++) if (v->F[i] < m) m = v->F[i];
        return v_float(m);
    }
    if (v->t == T_LIST) {
        P(v->n == 0,v_nil())
        V *m = v_ref(v->L[0]);
        for (int64_t i = 1; i < v->n; i++) {
            V *c = vec_cmp(m, v->L[i], OP_LT);
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
    if (v->t == T_FMAT) {
        P(v->n == 0 || mat_cols(v) == 0,v_nil())
        double m = v->F[0];
        int64_t ne = mat_nelem(v);
        for (int64_t i = 1; i < ne; i++) if (v->F[i] > m) m = v->F[i];
        return v_float(m);
    }
    if (v->t == T_IVEC) {
        P(v->n == 0,v_nil())
        if (v->n >= ISL_OMP_VEC_MIN)
            return v_int(shakti_max_i64(v->J, v->n));
        int64_t m = v->J[0];
        for (int64_t i = 1; i < v->n; i++) if (v->J[i] > m) m = v->J[i];
        return v_int(m);
    }
    if (v->t == T_FVEC) {
        P(v->n == 0,v_nil())
        if (v->n >= ISL_OMP_VEC_MIN)
            return v_float(shakti_max_f64(v->F, v->n));
        double m = v->F[0];
        for (int64_t i = 1; i < v->n; i++) if (v->F[i] > m) m = v->F[i];
        return v_float(m);
    }
    if (v->t == T_LIST) {
        P(v->n == 0,v_nil())
        V *m = v_ref(v->L[0]);
        for (int64_t i = 1; i < v->n; i++) {
            V *c = vec_cmp(m, v->L[i], OP_GT);
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
        if ((v->t >= T_IVEC && v->t <= T_LIST) && is_isolde_builtin("isolde_sum"))
            return isolde_builtin_call("isolde_sum", a, n);
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
    if (is_isolde_builtin("isolde_dot"))
        return isolde_builtin_call("isolde_dot", a, n);
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
static V *bi_mmul(V **a, in) {
    P(n < 2, v_err("mmul(a, b)"))
    return mat_matmul(a[0], a[1]);
}
static V *bi_avg(V **a, in) {
    P(n < 1,v_float(0))
    P((a[0]->t >= T_IVEC && a[0]->t <= T_LIST) || (a[0]->t >= T_IMAT && a[0]->t <= T_FMAT),vec_reduce_avg(a[0]))
    return v_float(0);
}
static V *bi_min(V **a, in) {
    P(n < 1,v_nil())
    if (n == 1 && (a[0]->t >= T_IVEC && a[0]->t <= T_LIST) && is_isolde_builtin("isolde_min"))
        return isolde_builtin_call("isolde_min", a, n);
    P((a[0]->t >= T_IVEC && a[0]->t <= T_LIST) || (a[0]->t >= T_IMAT && a[0]->t <= T_FMAT),vec_reduce_min(a[0]))
    if (n == 2) {
        double x = a[0]->t == T_INT ? (double)a[0]->j : a[0]->f;
        double y = a[1]->t == T_INT ? (double)a[1]->j : a[1]->f;
        return x < y ? v_float(x) : v_float(y);
    }
    return v_nil();
}
static V *bi_max(V **a, in) {
    P(n < 1,v_nil())
    if (n == 1 && (a[0]->t >= T_IVEC && a[0]->t <= T_LIST) && is_isolde_builtin("isolde_max"))
        return isolde_builtin_call("isolde_max", a, n);
    P((a[0]->t >= T_IVEC && a[0]->t <= T_LIST) || (a[0]->t >= T_IMAT && a[0]->t <= T_FMAT),vec_reduce_max(a[0]))
    if (n == 2) {
        double x = a[0]->t == T_INT ? (double)a[0]->j : a[0]->f;
        double y = a[1]->t == T_INT ? (double)a[1]->j : a[1]->f;
        return x > y ? v_float(x) : v_float(y);
    }
    return v_nil();
}
static V *bi_abs(V **a, in) {
    P(n < 1,v_int(0))
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
static int cmp_f64(const void*a,const void*b){double x=*(double*)a,y=*(double*)b;return(x>y)-(x<y);}
static V *bi_sort(V**a,in){P(n<1,v_list(0))V*v=a[0];
    if(v->t==T_IVEC){V*r=v_copy(v);qsort(r->J,r->n,8,cmp_i64);return r;}
    if(v->t==T_FVEC){V*r=v_copy(v);qsort(r->F,r->n,8,cmp_f64);return r;}
    return v_copy(v);}
/* q-compatible bin(keys, query): last i with keys[i] <= query; -1 below range. */
static V *bi_bin(V**a,in){
    P(n<2,v_err("bin(keys, query)"))
    V *keys=a[0], *q=a[1];
    if((keys->t==T_LIST||keys->t==T_IVEC||keys->t==T_FVEC)&&keys->n==0){
        if(q->t==T_INT||q->t==T_FLOAT) return v_int(-1);
        if(q->t==T_IVEC||q->t==T_FVEC){V*r=v_ivec(q->n);for(int64_t i=0;i<q->n;i++)r->J[i]=-1;return r;}
        return v_err("bin: empty keys need scalar or vector query");
    }
    if(keys->t==T_IVEC){
        if(q->t==T_INT) return v_int(shakti_bin_i64(keys->J,keys->n,q->j));
        if(q->t==T_IVEC){
            V *r=v_ivec(q->n);
            shakti_bin_i64_batch(keys->J,keys->n,q->J,q->n,r->J);
            return r;
        }
        return v_err("bin: int keys need int/ivec query");
    }
    if(keys->t==T_FVEC){
        if(q->t==T_FLOAT) return v_int(shakti_bin_f64(keys->F,keys->n,q->f));
        if(q->t==T_INT) return v_int(shakti_bin_f64(keys->F,keys->n,(double)q->j));
        if(q->t==T_FVEC){
            V *r=v_ivec(q->n);
            shakti_bin_f64_batch(keys->F,keys->n,q->F,q->n,r->J);
            return r;
        }
        return v_err("bin: float keys need float/fvec query");
    }
    return v_err("bin: keys must be ivec or fvec");
}

static int asof_time_key_name(const char *s){
    return s && (!strcmp(s,"time") || !strcmp(s,"time_ns"));
}
static int asof_col_named(V *tbl,const char *name){
    if(!tbl||tbl->t!=T_TABLE||!name)return 0;
    for(int64_t i=0;i<tbl->keys->n;i++)
        if(tbl->keys->L[i]->t==T_STR&&!strcmp(tbl->keys->L[i]->s,name))return 1;
    return 0;
}
static int asof_ascending_i64(V *v){
    if(!v||v->t!=T_IVEC)return 0;
    for(int64_t i=1;i<v->n;i++)if(v->J[i]<v->J[i-1])return 0;
    return 1;
}
static V *asof_cell(V *col,int64_t row){
    if(row<0)return v_nil();
    if(col->t==T_IVEC)return v_int(col->J[row]);
    if(col->t==T_FVEC)return v_float(col->F[row]);
    if(col->t==T_BVEC)return v_bool(col->B[row]);
    if(col->t==T_LIST)return v_ref(col->L[row]);
    if(col->t==T_IMAT||col->t==T_FMAT||col->t==T_BMAT)return v_mat_row(col,row);
    return v_ref(col);
}
static V *asof_gather_col(V *col,const int64_t *idx,int64_t n){
    int miss=0;
    for(int64_t i=0;i<n;i++)if(idx[i]<0){miss=1;break;}
    if(!miss&&col->t==T_IVEC){
        V *r=v_ivec(n);for(int64_t i=0;i<n;i++)r->J[i]=col->J[idx[i]];return r;
    }
    if(!miss&&col->t==T_FVEC){
        V *r=v_fvec(n);for(int64_t i=0;i<n;i++)r->F[i]=col->F[idx[i]];return r;
    }
    if(!miss&&col->t==T_BVEC){
        V *r=v_bvec(n);for(int64_t i=0;i<n;i++)r->B[i]=col->B[idx[i]];return r;
    }
    V *r=v_list(n);
    for(int64_t i=0;i<n;i++)r->L[i]=asof_cell(col,idx[i]);
    return r;
}
/* Dyadic comma is currently reserved for this one join shape only:
 * left,right where both tables' first column is the same ascending integer
 * time key. SQL JOIN and all other join forms remain unimplemented. */
V *table_asof_comma_join(V *left,V *right){
    if(!left||!right||left->t!=T_TABLE||right->t!=T_TABLE)
        return v_err(",: asof join requires two tables");
    if(left->keys->n<1||right->keys->n<1)
        return v_err(",: both tables must be keyed by time");
    V *lk=left->keys->L[0],*rk=right->keys->L[0];
    if(lk->t!=T_STR||rk->t!=T_STR||strcmp(lk->s,rk->s)||!asof_time_key_name(lk->s))
        return v_err(",: first column of both tables must be the same time/time_ns key");
    V *lt=left->vals->L[0],*rt=right->vals->L[0];
    if(lt->t!=T_IVEC||rt->t!=T_IVEC)
        return v_err(",: time keys must be integer vectors");
    if(!asof_ascending_i64(lt)||!asof_ascending_i64(rt))
        return v_err(",: both time keys must be sorted ascending");
    for(int64_t c=1;c<right->keys->n;c++){
        V *name=right->keys->L[c];
        if(name->t!=T_STR)return v_err(",: right column name must be string");
        if(asof_col_named(left,name->s))
            return v_errf(",: duplicate payload column '%s'",name->s);
    }
    int64_t *idx=malloc((size_t)(left->n?left->n:1)*sizeof(int64_t));
    if(!idx)return v_err(",: allocation failed");
    shakti_bin_i64_batch(rt->J,rt->n,lt->J,left->n,idx);
    int64_t nc=left->keys->n+right->keys->n-1;
    V *keys=v_list(nc),*data=v_list(nc);
    int64_t out=0;
    for(int64_t c=0;c<left->keys->n;c++,out++){
        keys->L[out]=v_ref(left->keys->L[c]);
        data->L[out]=v_ref(left->vals->L[c]);
    }
    for(int64_t c=1;c<right->keys->n;c++,out++){
        keys->L[out]=v_ref(right->keys->L[c]);
        data->L[out]=asof_gather_col(right->vals->L[c],idx,left->n);
    }
    free(idx);
    V *r=v_table(keys,data);
    v_free(keys);v_free(data);
    return r;
}
/* Sort (eq,time) → list [eq_sorted, time_sorted] for asof_bin. */
static V *bi_asof_sort(V**a,in){
    P(n<2||a[0]->t!=T_IVEC||a[1]->t!=T_IVEC,v_err("asof_sort(eq, time)"))
    P(a[0]->n!=a[1]->n,v_err("asof_sort: length mismatch"))
    V *eq=v_ivec(a[0]->n), *tm=v_ivec(a[1]->n);
    shakti_asof_sort_i64(a[0]->J,a[1]->J,a[0]->n,eq->J,tm->J);
    V *r=v_list(2); r->L[0]=eq; r->L[1]=tm; return r;
}
/* Coerce list[int] → owned ivec (or NULL on type error). */
static V *as_ivec_arg(V *v, const char *ctx){
    if(v->t==T_IVEC) return v_ref(v);
    if(v->t!=T_LIST) return NULL;
    V *r=v_ivec(v->n);
    for(int64_t i=0;i<v->n;i++){
        if(!v->L[i]||v->L[i]->t!=T_INT){v_free(r);return NULL;}
        r->J[i]=v->L[i]->j;
    }
    (void)ctx;
    return r;
}
/* Grouped asof index: right side sorted by (eq,time). */
static V *bi_asof_bin(V**a,in){
    P(n<4,v_err("asof_bin(eq, time, query_eq, query_time)"))
    P(a[0]->t!=T_IVEC||a[1]->t!=T_IVEC,v_err("asof_bin: eq/time must be ivec"))
    P(a[0]->n!=a[1]->n,v_err("asof_bin: eq/time length mismatch"))
    V *qeq=as_ivec_arg(a[2],"query_eq");
    if(!qeq) return v_err("asof_bin: query_eq must be ivec or list[int]");
    const int64_t *qtm=NULL; int64_t scalar=0; V *qtm_own=NULL;
    if(a[3]->t==T_IVEC){
        if(a[3]->n!=qeq->n){v_free(qeq);return v_err("asof_bin: query length mismatch");}
        qtm=a[3]->J;
    }else if(a[3]->t==T_LIST){
        qtm_own=as_ivec_arg(a[3],"query_time");
        if(!qtm_own){v_free(qeq);return v_err("asof_bin: query_time must be int or ivec/list[int]");}
        if(qtm_own->n!=qeq->n){v_free(qeq);v_free(qtm_own);return v_err("asof_bin: query length mismatch");}
        qtm=qtm_own->J;
    }else if(a[3]->t==T_INT){
        scalar=a[3]->j;
    }else{v_free(qeq);return v_err("asof_bin: query_time must be int or ivec");}
    V *r=v_ivec(qeq->n);
    shakti_asof_bin_i64(a[0]->J,a[1]->J,a[0]->n,qeq->J,qtm,qeq->n,scalar,r->J);
    v_free(qeq); if(qtm_own) v_free(qtm_own);
    return r;
}
static V *bi_reverse(V**a,in){P(n<1,v_list(0))V*v=a[0];
    if(v->t==T_IVEC){V*r=v_ivec(v->n);for(int64_t i=0;i<v->n;i++)r->J[i]=v->J[v->n-1-i];return r;}
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
static V *bi_table(V**a,in,V**kwn,V**kwv,int nkw){
    P(n==1&&a[0]->t==T_DICT,v_table(a[0]->keys,a[0]->vals))
    if(nkw>0){V*p=v_list(nkw),*d=v_list(nkw);
        for(int i=0;i<nkw;i++){p->L[i]=v_ref(kwn[i]);d->L[i]=v_ref(kwv[i]);}
        V*r=v_table(p,d);v_free(p);v_free(d);return r;}
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
        double gv=(sc->t==T_FVEC)?sc->F[i]:(sc->t==T_IVEC?(double)sc->J[i]:0);
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
static V *bi_input_key_down(V **a, in) {
    P(n < 1 || a[0]->t != T_INT, v_err("input_key_down(code)"))
    return v_int(input_hub_key_down((int)a[0]->j));
}
static V *bi_input_keys_clear(V **a, in) {
    (void)a; (void)n;
    input_hub_keys_clear();
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
BI0(len) BI0(range) BI0(type) BI0(int) BI0(float) BI0(str) BI0(list) BI0(bool)
BIKW(dict) BIKW(ktable) BI0(set)
BI0(sum) BI0(avg) BI0(min) BI0(max) BI0(dot) BI0(mmul) BI0(abs)
BI0(sqrt) BI0(floor) BI0(ceil) BI0(exp) BI0(log) BI0(sin) BI0(cos) BI0(tan)
BI0(bin) BI0(asof_sort) BI0(asof_bin)
BI0(sort) BI0(reverse) BI0(zip) BI0(enumerate)
BIE(map) BIE(filter) BIKWE(sorted)
BIKW(table) BI0(columns) BI0(shape) BI0(head) BI0(tail) BI0(group_sum)
BI0(append) BI0(pop) BI0(keys) BI0(values) BI0(next)
BI0(input) BI0(readline) BI0(wait) BI0(repr)
BI0(input_get_hz) BI0(input_set_hz)
BI0(input_get_x) BI0(input_get_y) BI0(input_get_wheel)
BI0(input_key_down) BI0(input_keys_clear)
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
#ifdef SHAKTI_HAVE_TALK
BI0(talk_listen) BI0(talk_set_locale) BI0(talk_set_model)
#endif
#ifdef SHAKTI_HAVE_SYNTH
BI0(synth_open) BI0(synth_close) BI0(synth_alive) BI0(synth_tick)
BI0(synth_set_steps) BI0(synth_steps) BI0(synth_set_metro) BI0(synth_metro_on)
BI0(synth_set_metro_sound) BI0(synth_metro_sound) BI0(synth_set_mute) BI0(synth_mute_on)
BI0(synth_note_on) BI0(synth_note_off) BI0(synth_set_bpm) BI0(synth_bpm)
BI0(synth_set_tuning) BI0(synth_tuning)
BI0(synth_set_level) BI0(synth_level) BI0(synth_set_cutoff) BI0(synth_cutoff)
BI0(synth_set_reso) BI0(synth_reso) BI0(synth_set_seq_row) BI0(synth_play) BI0(synth_playing)
BI0(synth_mouse_press) BI0(synth_mouse_release) BI0(synth_set_viz) BI0(synth_viz_mode) BI0(synth_vu)
BI0(synth_load_sample) BI0(synth_sample_loaded) BI0(synth_sample_name)
BI0(synth_set_row_note) BI0(synth_row_note)
BI0(synth_looper_rec) BI0(synth_looper_play) BI0(synth_looper_clear) BI0(synth_looper_overdub)
BI0(synth_looper_rec_on) BI0(synth_looper_play_on) BI0(synth_looper_has_loop)
#endif
#ifdef SHAKTI_HAVE_GFX
BI0(gfx_open) BI0(gfx_close) BI0(gfx_alive) BI0(gfx_available) BI0(gfx_tick) BI0(gfx_sync_keys)
BI0(gfx_clear) BI0(gfx_fill_rect) BI0(gfx_line) BI0(gfx_fill_circle)
BI0(gfx_click_pending) BI0(gfx_click_x) BI0(gfx_click_y) BI0(gfx_consume_click)
#endif
#ifdef SHAKTI_HAVE_IPC
BI0(ipc_accept) BI0(ipc_close) BI0(ipc_connect) BI0(ipc_listen) BI0(ipc_poll)
BI0(ipc_recv) BI0(ipc_recv_nowait) BI0(ipc_rdma_available) BI0(ipc_send)
BI0(ipc_set_nonblock) BI0(ipc_shm_close) BI0(ipc_shm_open)
#endif
BI0(graph_create) BI0(graph_add) BI0(graph_query) BI0(graph_neighbors) BI0(graph_path)
BI0(graph_from_table) BI0(graph_to_table) BI0(graph_count) BI0(graph_clear)
BI0(rest_request) BI0(rest_get) BI0(rest_post) BI0(rest_put) BI0(rest_delete)
BI0(rest_listen) BI0(rest_accept) BI0(rest_read) BI0(rest_write) BI0(rest_close)
BI0(pcm_open) BI0(pcm_write) BI0(pcm_close)
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
    {"asof_bin", bi_w_asof_bin},
    {"asof_sort", bi_w_asof_sort},
    {"avg", bi_w_avg},
    {"bin", bi_w_bin},
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
#ifdef SHAKTI_HAVE_GFX
    {"gfx_alive", bi_w_gfx_alive},
    {"gfx_available", bi_w_gfx_available},
    {"gfx_clear", bi_w_gfx_clear},
    {"gfx_click_pending", bi_w_gfx_click_pending},
    {"gfx_click_x", bi_w_gfx_click_x},
    {"gfx_click_y", bi_w_gfx_click_y},
    {"gfx_close", bi_w_gfx_close},
    {"gfx_consume_click", bi_w_gfx_consume_click},
    {"gfx_fill_circle", bi_w_gfx_fill_circle},
    {"gfx_fill_rect", bi_w_gfx_fill_rect},
    {"gfx_line", bi_w_gfx_line},
    {"gfx_open", bi_w_gfx_open},
    {"gfx_sync_keys", bi_w_gfx_sync_keys},
    {"gfx_tick", bi_w_gfx_tick},
#endif
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
    {"input_key_down", bi_w_input_key_down},
    {"input_keys_clear", bi_w_input_keys_clear},
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
    {"mmul", bi_w_mmul},
    {"next", bi_w_next},
    {"ord", bi_w_ord},
    {"path_basename", bi_w_path_basename},
    {"path_dirname", bi_w_path_dirname},
    {"path_exists", bi_w_path_exists},
    {"path_isdir", bi_w_path_isdir},
    {"path_isfile", bi_w_path_isfile},
    {"path_join", bi_w_path_join},
    {"path_splitext", bi_w_path_splitext},
    {"pcm_close", bi_w_pcm_close},
    {"pcm_open", bi_w_pcm_open},
    {"pcm_write", bi_w_pcm_write},
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
    {"rest_accept", bi_w_rest_accept},
    {"rest_close", bi_w_rest_close},
    {"rest_delete", bi_w_rest_delete},
    {"rest_get", bi_w_rest_get},
    {"rest_listen", bi_w_rest_listen},
    {"rest_post", bi_w_rest_post},
    {"rest_put", bi_w_rest_put},
    {"rest_read", bi_w_rest_read},
    {"rest_request", bi_w_rest_request},
    {"rest_write", bi_w_rest_write},
    {"reverse", bi_w_reverse},
    {"set", bi_w_set},
    {"sh", bi_w_sh},
    {"shape", bi_w_shape},
    {"sin", bi_w_sin},
    {"sort", bi_w_sort},
    {"sorted", bi_w_sorted},
    {"sqrt", bi_w_sqrt},
    {"stat", bi_w_stat},
    {"str", bi_w_str},
    {"sum", bi_w_sum},
#ifdef SHAKTI_HAVE_SYNTH
    {"synth_alive", bi_w_synth_alive},
    {"synth_bpm", bi_w_synth_bpm},
    {"synth_close", bi_w_synth_close},
    {"synth_cutoff", bi_w_synth_cutoff},
    {"synth_level", bi_w_synth_level},
    {"synth_load_sample", bi_w_synth_load_sample},
    {"synth_looper_clear", bi_w_synth_looper_clear},
    {"synth_looper_has_loop", bi_w_synth_looper_has_loop},
    {"synth_looper_overdub", bi_w_synth_looper_overdub},
    {"synth_looper_play", bi_w_synth_looper_play},
    {"synth_looper_play_on", bi_w_synth_looper_play_on},
    {"synth_looper_rec", bi_w_synth_looper_rec},
    {"synth_looper_rec_on", bi_w_synth_looper_rec_on},
    {"synth_metro_on", bi_w_synth_metro_on},
    {"synth_metro_sound", bi_w_synth_metro_sound},
    {"synth_mouse_press", bi_w_synth_mouse_press},
    {"synth_mouse_release", bi_w_synth_mouse_release},
    {"synth_mute_on", bi_w_synth_mute_on},
    {"synth_note_off", bi_w_synth_note_off},
    {"synth_note_on", bi_w_synth_note_on},
    {"synth_open", bi_w_synth_open},
    {"synth_play", bi_w_synth_play},
    {"synth_playing", bi_w_synth_playing},
    {"synth_reso", bi_w_synth_reso},
    {"synth_row_note", bi_w_synth_row_note},
    {"synth_sample_loaded", bi_w_synth_sample_loaded},
    {"synth_sample_name", bi_w_synth_sample_name},
    {"synth_set_bpm", bi_w_synth_set_bpm},
    {"synth_set_cutoff", bi_w_synth_set_cutoff},
    {"synth_set_level", bi_w_synth_set_level},
    {"synth_set_metro", bi_w_synth_set_metro},
    {"synth_set_metro_sound", bi_w_synth_set_metro_sound},
    {"synth_set_mute", bi_w_synth_set_mute},
    {"synth_set_reso", bi_w_synth_set_reso},
    {"synth_set_row_note", bi_w_synth_set_row_note},
    {"synth_set_seq_row", bi_w_synth_set_seq_row},
    {"synth_set_steps", bi_w_synth_set_steps},
    {"synth_set_tuning", bi_w_synth_set_tuning},
    {"synth_set_viz", bi_w_synth_set_viz},
    {"synth_steps", bi_w_synth_steps},
    {"synth_tick", bi_w_synth_tick},
    {"synth_tuning", bi_w_synth_tuning},
    {"synth_viz_mode", bi_w_synth_viz_mode},
    {"synth_vu", bi_w_synth_vu},
#endif
    {"table", bi_w_table},
    {"tail", bi_w_tail},
#ifdef SHAKTI_HAVE_TALK
    {"talk_listen", bi_w_talk_listen},
    {"talk_set_locale", bi_w_talk_set_locale},
    {"talk_set_model", bi_w_talk_set_model},
#endif
    {"tan", bi_w_tan},
    {"time_ms", bi_w_time_ms},
    {"type", bi_w_type},
    {"values", bi_w_values},
    {"wait", bi_w_wait},
    {"walk", bi_w_walk},
    {"write", bi_w_fwrite},
    {"zip", bi_w_zip},
};
static BiCall bi_find(const char *name) {
    BiEntry key = {name, NULL};
    const BiEntry *hit = bsearch(&key, bi_tab, sizeof bi_tab / sizeof bi_tab[0], sizeof *hit, bi_name_cmp);
    return hit ? hit->fn : NULL;
}
V *builtin_call(const char *name,V **args,int nargs,V **kwn,V **kwv,int nkw,Env *e){
    BiCall fn;
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
        for(int i=0;i<p->n && i<al->n;i++) env_set(ce,p->L[i]->s,al->L[i]);
        Node*body=fn_ast[(int)fnv->j];V*rv=eval(body,ce);
        if(g_returning){g_returning=0;v_free(rv);rv=g_retval;g_retval=NULL;}
        env_free(ce);return rv;
    }
    if(!strcmp(name,"load")) {
        if(nargs < 1 || args[0]->t != T_STR) {
            return v_err("load(path) or load(path, [column, ...])");
        }
        V *cols = NULL;
        if(nargs > 1) {
            if(args[1]->t != T_LIST) {
                return v_err("load(path, [columns]): columns must be a list of strings");
            }
            cols = args[1];
        }
        return table_load(args[0]->s, cols);
    }
    P(!strcmp(name,"save"),nargs>1&&args[1]->t==T_STR?(table_save(args[0],args[1]->s)?v_err("save failed"):v_nil()):v_err("save(table,path)"))
    if(is_isolde_builtin(name)) return isolde_builtin_call(name, args, nargs);
    return v_errf("unknown builtin '%s'",name);
}
void builtin_register(Env *e){(void)e;}