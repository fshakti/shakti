#include "shakti.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(SHAKTI_HAVE_LIBEXPAT)
#include <expat.h>
struct xml_cb{V*tag;V*id;V*name;V*text;};
static void xml_start(void*ud,const char*name,const char**atts){
 struct xml_cb*c=(struct xml_cb*)ud;
 const char*idv="",*nm="";
 for(int i=0;atts&&atts[i];i+=2){
  if(!strcmp(atts[i],"id"))idv=atts[i+1]?atts[i+1]:"";
  if(!strcmp(atts[i],"name"))nm=atts[i+1]?atts[i+1]:"";}
 v_list_append(c->tag,v_str(name));
 v_list_append(c->id,v_str(idv));
 v_list_append(c->name,v_str(nm));
 v_list_append(c->text,v_str(""));}
static void xml_ch(void*ud,const XML_Char*s,int len){
 struct xml_cb*c=(struct xml_cb*)ud;
 if(c->text->n==0)return;
 if(c->text->L[c->text->n-1]->t!=T_STR)return;
 V*last=c->text->L[c->text->n-1];
 size_t o=strlen(last->s);
 char*n=malloc(o+(size_t)len+1);
 if(!n)return;
 memcpy(n,last->s,o);
 memcpy(n+o,s,(size_t)len);
 n[o+(size_t)len]=0;
 free(last->s);
 last->s=n;}
V*table_xml_load(const char*path,V*columns_opt){
 (void)columns_opt;
 FILE*f=fopen(path,"rb");
 P(!f,v_errf("xml: open '%s'",path))
 fseek(f,0,SEEK_END);
 long z=ftell(f);
 fseek(f,0,SEEK_SET);
 char*buf=malloc((size_t)z+1);
 if(!buf){fclose(f);return v_err("xml: oom");}
 fread(buf,1,(size_t)z,f);
 buf[z]=0;
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
 XML_SetUserData(p,&cb);
 XML_SetElementHandler(p,xml_start,NULL);
 XML_SetCharacterDataHandler(p,xml_ch);
 if(XML_Parse(p,buf,(int)z,1)==XML_STATUS_ERROR){
  XML_ParserFree(p);
  free(buf);
  v_free(cb.tag);
  v_free(cb.id);
  v_free(cb.name);
  v_free(cb.text);
  return v_errf("xml: parse: %s",XML_ErrorString(XML_GetErrorCode(p)));}
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
