// SPDX-License-Identifier: Apache-2.0
#include "shakti.h"
V*method_call(V*o,const g0*m,V**a,in,Env*e){
 P(!strcmp(m,"append")&&(o->t==T_LIST||o->t==T_IVEC||o->t==T_FVEC||o->t==T_BVEC||o->t==T_CVEC)&&n<1,v_err("append(value)"))
 P(!strcmp(m,"append")&&(o->t==T_LIST||o->t==T_IVEC||o->t==T_FVEC||o->t==T_BVEC||o->t==T_CVEC),builtin_call("append",(V*[]){o,a[0]},2,NULL,NULL,0,e))
 P(!strcmp(m,"pop")&&(o->t==T_LIST||o->t==T_IVEC||o->t==T_FVEC||o->t==T_BVEC||o->t==T_CVEC),builtin_call("pop",(V*[]){o},1,NULL,NULL,0,e))
 P(!strcmp(m,"get")&&o->t==T_DICT&&n<1,v_nil())
 P(!strcmp(m,"get")&&o->t==T_DICT,({V*f=v_dict_find(o,a[0]); f?v_ref(f):v_nil();}))
 P(!strcmp(m,"keys")&&(o->t==T_DICT||o->t==T_TABLE),builtin_call("keys",(V*[]){o},1,NULL,NULL,0,e))
 P(!strcmp(m,"values")&&(o->t==T_DICT||o->t==T_TABLE),builtin_call("values",(V*[]){o},1,NULL,NULL,0,e))
 P(!strcmp(m,"len")&&(o->t==T_LIST||o->t==T_STR||o->t==T_IVEC||o->t==T_FVEC||o->t==T_BVEC||o->t==T_CVEC||o->t==T_IMAT||o->t==T_FMAT||o->t==T_BMAT||o->t==T_CMAT),builtin_call("len",(V*[]){o},1,NULL,NULL,0,e))
 P(!strcmp(m,"find")&&o->t==T_STR&&(n<1||!a[0]||a[0]->t!=T_STR),v_err("str.find(substr)"))
 P(!strcmp(m,"find")&&o->t==T_STR&&(!o->s||!a[0]->s),v_int(-1))
 if(!strcmp(m,"find")&&o->t==T_STR){
  const g0*p=strstr(o->s,a[0]->s);
  return p?v_int((int64_t)(p-o->s)):v_int(-1);
 }
 if(!strcmp(m,"strip")&&o->t==T_STR){
  const char *s = o->s ? o->s : "";
  size_t n;
  char *b;
  while(*s==' '||*s=='\t'||*s=='\n'||*s=='\r') s++;
  n = strlen(s);
  while(n && (s[n-1]==' '||s[n-1]=='\t'||s[n-1]=='\n'||s[n-1]=='\r')) n--;
  b = malloc(n+1);
  if(!b) return v_err("out of memory");
  memcpy(b, s, n);
  b[n] = 0;
  return v_str_take(b);
 }
 return v_errf("method '%s' for type %s",m,type_name(o->t));}
