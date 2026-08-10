#include "shakti.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(SHAKTI_HAVE_LIBEXPAT)
/* Unlock expat ≥2.6 amplification-protection prototypes (symbols are in libexpat). */
#ifndef XML_GE
#define XML_GE 1
#endif
#include <expat.h>
struct xml_cb{V*tag;V*id;V*name;V*text;size_t cur_cap;};
static void xml_start(void*ud,const char*name,const char**atts){
 struct xml_cb*c=(struct xml_cb*)ud;
 const char*idv="",*nm="";
 for(int i=0;atts&&atts[i];i+=2){
  if(!strcmp(atts[i],"id"))idv=atts[i+1]?atts[i+1]:"";
  if(!strcmp(atts[i],"name"))nm=atts[i+1]?atts[i+1]:"";}
 v_list_append_own(c->tag,v_str(name));
 v_list_append_own(c->id,v_str(idv));
 v_list_append_own(c->name,v_str(nm));
 v_list_append_own(c->text,v_str(""));
 c->cur_cap=1;}
static void xml_ch(void*ud,const XML_Char*s,int len){
 struct xml_cb*c=(struct xml_cb*)ud;
 if(c->text->n==0||len<=0)return;
 if(c->text->L[c->text->n-1]->t!=T_STR)return;
 V*last=c->text->L[c->text->n-1];
 size_t o=strlen(last->s);
 size_t need=o+(size_t)len+1;
 if(need>c->cur_cap){
  size_t ncap=c->cur_cap?c->cur_cap*2:16;
  while(ncap<need){
   if(ncap>(SIZE_MAX/2)){ncap=need;break;}
   ncap*=2;
  }
  char*n=realloc(last->s,ncap);
  if(!n)return;
  last->s=n;
  c->cur_cap=ncap;
 }
 memcpy(last->s+o,s,(size_t)len);
 last->s[o+(size_t)len]=0;}
#define SHAKTI_XML_MAX_FILE (256u * 1024u * 1024u)
V*table_xml_load(const char*path,V*columns_opt){
 (void)columns_opt;
 FILE*f=fopen(path,"rb");
 P(!f,v_errf("xml: open '%s'",path))
 fseek(f,0,SEEK_END);
 long z=ftell(f);
 fseek(f,0,SEEK_SET);
 if(z<0||(unsigned long)z>SHAKTI_XML_MAX_FILE){fclose(f);return v_err("xml: file too large or unreadable");}
 char*buf=malloc((size_t)z+1);
 if(!buf){fclose(f);return v_err("xml: oom");}
 size_t got=fread(buf,1,(size_t)z,f);
 buf[got]=0;
 fclose(f);
 struct xml_cb cb;
 memset(&cb,0,sizeof(cb));
 cb.tag=v_list(0);
 cb.id=v_list(0);
 cb.name=v_list(0);
 cb.text=v_list(0);
 XML_Parser p=XML_ParserCreate(NULL);
 if(!p){
  free(buf);
  v_free(cb.tag);
  v_free(cb.id);
  v_free(cb.name);
  v_free(cb.text);
  return v_err("xml: parser");}
 XML_SetParamEntityParsing(p, XML_PARAM_ENTITY_PARSING_NEVER);
#if defined(XML_DTD) || (defined(XML_GE) && XML_GE == 1)
 XML_SetBillionLaughsAttackProtectionMaximumAmplification(p, 100.0f);
 XML_SetBillionLaughsAttackProtectionActivationThreshold(p, 8388608ull);
#endif
 XML_SetUserData(p,&cb);
 XML_SetElementHandler(p,xml_start,NULL);
 XML_SetCharacterDataHandler(p,xml_ch);
 if(XML_Parse(p,buf,(int)got,1)==XML_STATUS_ERROR){
  enum XML_Error code=XML_GetErrorCode(p);
  XML_ParserFree(p);
  free(buf);
  v_free(cb.tag);
  v_free(cb.id);
  v_free(cb.name);
  v_free(cb.text);
  return v_errf("xml: parse: %s",XML_ErrorString(code));}
 XML_ParserFree(p);
 free(buf);
 V*kl=v_list(4);
 kl->L[0]=v_str("tag");
 kl->L[1]=v_str("id");
 kl->L[2]=v_str("name");
 kl->L[3]=v_str("text");
 V*dl=v_list(4);
 dl->L[0]=cb.tag;
 dl->L[1]=cb.id;
 dl->L[2]=cb.name;
 dl->L[3]=cb.text;
 V*t=v_table(kl,dl);
 v_free(kl);
 v_free(dl);
 t->n=cb.tag->n;
 return t;}
#else
V*table_xml_load(const char*path,V*columns_opt){
 (void)path;
 (void)columns_opt;
 return v_err("xml: built without expat (SHAKTI_HAVE_LIBEXPAT)");}
#endif
