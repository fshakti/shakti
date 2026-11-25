// SPDX-License-Identifier: Apache-2.0
#include "shakti.h"
#include "str_ops.h"
#include <ctype.h>
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
 if((o->t==T_STR||o->t==T_CVEC)&&(!strcmp(m,"upper")||!strcmp(m,"lower"))){
  int upper=m[0]=='u';
  if(o->t==T_CVEC){
   V*r=v_cvec(o->n);
   if(!r||r->t==T_ERR)return r?r:v_err("str case: out of memory");
   if(o->n>0&&o->B)str_case_ascii(r->B,o->B,o->n,upper);
   return r;
  }else{
   const g0*s=o->s?o->s:"";
   int64_t len=(int64_t)strlen(s);
   char*buf=malloc((size_t)len+1);
   if(!buf)return v_err("str case: out of memory");
   str_case_ascii((unsigned char*)buf,(const unsigned char*)s,len,upper);
   buf[len]=0;
   return v_str_take(buf);
  }
 }
 if(o->t==T_STR&&(!strcmp(m,"startswith")||!strcmp(m,"endswith"))){
  int tail=m[0]=='e';
  const g0*s,*p;
  int64_t sn,pn;
  if(n<1||!a[0]||a[0]->t!=T_STR)return v_err(tail?"str.endswith(suffix)":"str.startswith(prefix)");
  s=o->s?o->s:"";
  p=a[0]->s?a[0]->s:"";
  sn=(int64_t)strlen(s);
  pn=(int64_t)strlen(p);
  return v_bool(tail?str_has_suffix(s,sn,p,pn):str_has_prefix(s,sn,p,pn));
 }
 if(o->t==T_STR&&!strcmp(m,"strip")){
  const g0*s=o->s?o->s:"";
  int64_t n=(int64_t)strlen(s),lo=0,hi=n;
  char*buf;
  while(lo<hi&&isspace((unsigned char)s[lo]))lo++;
  while(hi>lo&&isspace((unsigned char)s[hi-1]))hi--;
  buf=malloc((size_t)(hi-lo)+1);
  if(!buf)return v_err("str.strip: out of memory");
  memcpy(buf,s+lo,(size_t)(hi-lo));
  buf[hi-lo]=0;
  return v_str_take(buf);
 }
 if(o->t==T_STR&&!strcmp(m,"split")){
  const g0*s=o->s?o->s:"";
  int64_t sn=(int64_t)strlen(s);
  V*r;
  if(n==0){
   r=str_split_ws(s,sn);
   return r?r:v_err("str.split: out of memory");
  }
  if(!a[0]||a[0]->t!=T_STR)return v_err("str.split(sep)");
  {
   const g0*sep=a[0]->s?a[0]->s:"";
   int64_t sepn=(int64_t)strlen(sep);
   int64_t max_cuts=-1;
   if(sepn==0)return v_err("str.split(sep)");
   if(n>=2){
    if(!a[1]||a[1]->t!=T_INT||a[1]->j<0)return v_err("str.split(sep, max)");
    max_cuts=a[1]->j;
   }
   r=str_split_lit(s,sn,sep,sepn,max_cuts);
   return r?r:v_err("str.split: out of memory");
  }
 }
 if(o->t==T_STR&&!strcmp(m,"replace")){
  const g0*s,*old,*neu;
  int64_t sn,oldn,neun,limit=-1;
  V*out=NULL;
  int rc;
  if(n<2||!a[0]||a[0]->t!=T_STR||!a[1]||a[1]->t!=T_STR)return v_err("str.replace(old, new)");
  old=a[0]->s?a[0]->s:"";
  neu=a[1]->s?a[1]->s:"";
  oldn=(int64_t)strlen(old);
  neun=(int64_t)strlen(neu);
  if(oldn==0)return v_err("str.replace(old, new)");
  if(n>=3){
   if(!a[2]||a[2]->t!=T_INT||a[2]->j<0)return v_err("str.replace(old, new, count)");
   limit=a[2]->j;
  }
  s=o->s?o->s:"";
  sn=(int64_t)strlen(s);
  rc=str_replace_lit(s,sn,old,oldn,neu,neun,limit,&out);
  if(rc<0)return v_err("str.replace: out of memory");
  if(rc==0)return v_ref(o);
  return out;
 }
 return v_errf("method '%s' for type %s",m,type_name(o->t));}
