#include "shakti.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

extern V*load_module(const char *name);
extern V*table_xml_load(const char*path,V*columns_opt);

static int ends_with(const char *s, const char *t) {
    size_t ls = strlen(s), lu = strlen(t);
    return ls >= lu && strcmp(s + ls - lu, t) == 0;
}

V *table_load(const char *path, V *columns_opt) {
    P(!path, v_err("load: path"))
    char *ext=strrchr(path,'.');
    if(!chdir(path)) {
        V*r;
        if(columns_opt && columns_opt->n) {
            V*keys = v_list(columns_opt->n);
            V*vals = v_list(columns_opt->n);
            i(columns_opt->n,keys->L[i]=columns_opt->L[i];vals->L[i]=table_load(columns_opt->L[i]->s,NULL));
            r=v_table(keys,vals);
        } else {
            V*keys = v_list(0);
            V*vals = v_list(0);
            DIR*dir=opendir(".");
            struct dirent*d;
            int pn = strlen(path);
            while((d=readdir(dir))) {
                if(*d->d_name == '.')continue;
                v_list_append(keys, v_str(d->d_name));
                int sn = pn+ strlen(d->d_name)+2;
                char *s = malloc(sn);
                snprintf(s,sn,"%s/%s",path,d->d_name);
                V*fun = v_fn(NULL, v_str(s), NULL, NULL);
                v_list_append(vals, fun);free(s);
            }
            closedir(dir);
            r = v_dict(keys,vals);
        }
        extern int start_dir;
        fchdir(start_dir);
        return r;
    } else if(ext) {
        if(!strcmp(ext,".xml")) return table_xml_load(path,columns_opt);

        V *mod_dict = v_ref(load_module(ext+1));
        if(!mod_dict)return v_err("load: unsupported format/extension");
        V *impl = v_dict_get(mod_dict, "load");
        if(!impl)return v_free(mod_dict),v_err("load: unsupported format/extension");

        Env *e = env_new(NULL);
        Node *n = node_new(N_CALL),*q,*d;
        env_set(e, "self", mod_dict);
        env_set(e, "filename", v_str(path));
        if(columns_opt)env_set(e, "columns_opt", v_ref(columns_opt));
        q=node_new(N_NAME);q->sval=strdup("self");
        d=node_new(N_DOT);d->sval=strdup("load");node_add(d,q);
        node_add(n, d);
        q=node_new(N_NAME);q->sval=strdup("filename");
        node_add(n, q);
        if(columns_opt){
            q=node_new(N_NAME);q->sval=strdup("columns_opt");
            d=node_new(N_UNOP);d->op=OP_MUL;node_add(d,q);
            node_add(n,d);
        }
        V *t = eval(n, e);
        env_free(e);
        return t;
    } else {
        V *a[1] = {v_str(path)}, *t=bi_fread(a,1); v_free(*a); return t;
    }
}

V*table_save(V *table, const char *path) {
    P(!path, v_err("save: path"))
    char *ext=strrchr(path,'.');
    if(ext) {
        V *mod_dict = v_ref(load_module(ext+1));
        if(!mod_dict)return v_err("save: unsupported format/extension");
        V *impl = v_dict_get(mod_dict, "save");
        if(!impl)return v_free(mod_dict),v_err("save: unsupported format/extension");

        Env *e = env_new(NULL);
        Node *n = node_new(N_CALL),*q,*d;
        env_set(e, "self", mod_dict);
        env_set(e, "filename", v_str(path));
        env_set(e, "all", table);
        q=node_new(N_NAME);q->sval=strdup("self");
        d=node_new(N_DOT);d->sval=strdup("save");node_add(d,q);node_add(n, d);
        q=node_new(N_NAME);q->sval=strdup("all");node_add(n, q);
        q=node_new(N_NAME);q->sval=strdup("filename");node_add(n, q);
        V *t = eval(n, e);
        env_free(e);
        return t;
    }
    return v_err("save: unsupported format/extension");
}
