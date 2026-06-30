#include "shakti.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>

extern V*load_module(const char *name);
extern V*table_xml_load(const char*path,V*columns_opt);

V *table_load(const char *path, V *columns_opt) {
    P(!path, v_err("load: path"))
    if(columns_opt == NULL) return builtin_call("load",(V*[]){v_str(path)}, 1, NULL,NULL,0,NULL);
    V*a = v_list(0);v_list_append(a,v_str(path));i(columns_opt->n,v_list_append(a,columns_opt->L[i]));
    V*r=builtin_call("load",a->L,a->n,NULL,NULL,0,NULL);
    a->n=0;v_free(a);return r;
}

V*table_save(V *table, const char *path) {
    P(!path, v_err("save: path"))
    V*a[]={table,v_str(path)};
    V*r=builtin_call("save",a,2,NULL,NULL,0,NULL);
    v_free(a[1]);
    return r;
}
