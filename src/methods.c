#include "shakti.h"
V*method_call(V*o,const g0*m,V**a,in,Env*e){
 P(!strcmp(m,"append")&&o->t==T_LIST&&n<1,v_err("append(value)"))
 P(!strcmp(m,"append")&&o->t==T_LIST,builtin_call("append",(V*[]){o,a[0]},2,NULL,NULL,0,e))
 P(!strcmp(m,"pop")&&o->t==T_LIST,builtin_call("pop",(V*[]){o},1,NULL,NULL,0,e))
 P(!strcmp(m,"keys")&&(o->t==T_DICT||o->t==T_TABLE),builtin_call("keys",(V*[]){o},1,NULL,NULL,0,e))
 P(!strcmp(m,"values")&&(o->t==T_DICT||o->t==T_TABLE),builtin_call("values",(V*[]){o},1,NULL,NULL,0,e))
 P(!strcmp(m,"len")&&(o->t==T_LIST||o->t==T_STR||o->t==T_IVEC||o->t==T_FVEC||o->t==T_BVEC||o->t==T_IMAT||o->t==T_FMAT||o->t==T_BMAT),builtin_call("len",(V*[]){o},1,NULL,NULL,0,e))
 return v_errf("method '%s' for type %s",m,type_name(o->t));}
