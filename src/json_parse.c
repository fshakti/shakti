#include "shakti.h"
#include "json_parse.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
static const char*skip_ws(const char*s){W(*s==' '||*s=='\t'||*s=='\n'||*s=='\r',s++)return s;}
static V*parse_value(const char*s,const char**end_out);
static int hex_digit(char c){P(c>='0'&&c<='9',c-'0')P(c>='a'&&c<='f',c-'a'+10)P(c>='A'&&c<='F',c-'A'+10)return-1;}
static V*parse_string(const char*s,const char**end_out){
 P(*s!='"',v_err("json: expected string"))
 s++;
 size_t cap=64,len=0;
 char*buf=malloc(cap);
 P(!buf,v_err("json: oom"))
 while(*s&&*s!='"'){
  char c=*s++;
  if(c=='\\'){
   if(!*s){free(buf);return v_err("json: bad escape");}
   c=*s++;
   switch(c){
   case '"':case '\\':case '/':break;
   case 'b':c='\b';break;
   case 'f':c='\f';break;
   case 'n':c='\n';break;
   case 'r':c='\r';break;
   case 't':c='\t';break;
   case 'u':{
    if(!s[0]||!s[1]||!s[2]||!s[3]){free(buf);return v_err("json: bad \\u");}
    int d0=hex_digit(s[0]),d1=hex_digit(s[1]),d2=hex_digit(s[2]),d3=hex_digit(s[3]);
    if(d0<0||d1<0||d2<0||d3<0){free(buf);return v_err("json: bad \\u");}
    int h=(d0<<12)|(d1<<8)|(d2<<4)|d3;
    c=(char)(h&0xff);
    s+=4;
    break;}
   default:free(buf);return v_err("json: bad escape");}}
  if(len+2>=cap){
   cap*=2;
   char*n=realloc(buf,cap);
   if(!n){free(buf);return v_err("json: oom");}
   buf=n;}
  buf[len++]=c;}
 if(*s!='"'){free(buf);return v_err("json: unterminated string");}
 s++;
 buf[len]=0;
 V*out=v_str(buf);
 free(buf);
 *end_out=s;
 return out;}
static V*parse_number(const char*s,const char**end_out){
 char*e=NULL;
 if(*s=='-')s++;
 P(!isdigit((unsigned char)*s)&&*s!='.',v_err("json: bad number"))
 int has_dot=0;
 const char*p=s;
 if(*p=='-')p++;
 W(isdigit((unsigned char)*p),p++)
 if(*p=='.'){has_dot=1;p++;W(isdigit((unsigned char)*p),p++)}
 if(*p=='e'||*p=='E'){has_dot=1;p++;if(*p=='+'||*p=='-')p++;W(isdigit((unsigned char)*p),p++)}
 if(has_dot){
  double f=strtod(s,&e);
  P(e==s,v_err("json: bad number"))
  *end_out=e;
  return v_float(f);}
 long long j=strtoll(s,&e,10);
 P(e==s,v_err("json: bad number"))
 *end_out=e;
 return v_int((int64_t)j);}
static V*parse_array(const char*s,const char**end_out){
 P(*s!='[',v_err("json: expected ["))
 s=skip_ws(s+1);
 V*r=v_list(0);
 if(*s==']'){*end_out=s+1;return r;}
 for(;;){
  const char*e=NULL;
  V*item=parse_value(s,&e);
  if(!item||item->t==T_ERR){v_free(r);return item?item:v_err("json: array value");}
  v_list_append(r,item);
  v_free(item);
  s=skip_ws(e);
  if(*s==']'){*end_out=s+1;return r;}
  Pr(*s!=',',v_free(r);v_err("json: expected , or ]");)s=skip_ws(s+1);}}
static V*parse_object(const char*s,const char**end_out){
 P(*s!='{',v_err("json: expected {"))
 s=skip_ws(s+1);
 V*keys=v_list(0);
 V*vals=v_list(0);
 if(*s=='}'){*end_out=s+1;return v_dict(keys,vals);}
 for(;;){
  const char*e=NULL;
  V*key=parse_string(s,&e);
  if(!key||key->t==T_ERR){v_free(keys);v_free(vals);return key?key:v_err("json: object key");}
  s=skip_ws(e);
  Pr(*s!=':',v_free(key);v_free(keys);v_free(vals);v_err("json: expected :");)
  s=skip_ws(s+1);
  V*val=parse_value(s,&e);
  if(!val||val->t==T_ERR){v_free(key);v_free(keys);v_free(vals);return val?val:v_err("json: object value");}
  v_list_append(keys,key);
  v_free(key);
  v_list_append(vals,val);
  v_free(val);
  s=skip_ws(e);
  if(*s=='}'){*end_out=s+1;return v_dict(keys,vals);}
  Pr(*s!=',',v_free(keys);v_free(vals);v_err("json: expected , or }");)s=skip_ws(s+1);}}
static V*parse_value(const char*s,const char**end_out){
 s=skip_ws(s);
 P(*s=='"',parse_string(s,end_out))
 P(*s=='[',parse_array(s,end_out))
 P(*s=='{',parse_object(s,end_out))
 P(*s=='-'||isdigit((unsigned char)*s),parse_number(s,end_out))
 if(!strncmp(s,"true",4)){*end_out=s+4;return v_bool(1);}
 if(!strncmp(s,"false",5)){*end_out=s+5;return v_bool(0);}
 if(!strncmp(s,"null",4)){*end_out=s+4;return v_nil();}
 return v_err("json: unexpected token");}
V*shakti_json_parse(const char*s,const char**end_out){
 const char*e=NULL;
 V*v=parse_value(s,&e);
 P(!v||v->t==T_ERR,v)
 e=skip_ws(e);
 if(*e){v_free(v);return v_err("json: trailing data");}
 if(end_out)*end_out=e;
 return v;}
