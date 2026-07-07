#include "a.h"
#include <jni.h>
extern i shakti_lang_main(i c,g0**v);
JNIEXPORT jint JNICALL Java_com_shakti_shakti_ShaktiNative_runFile(JNIEnv*e,jclass z,jstring p){
 (void)z;
 P(!p,-100)
 ss path=(*e)->GetStringUTFChars(e,p,0);
 P(!path,-101)
 ss owned=strdup(path);
 (*e)->ReleaseStringUTFChars(e,p,path);
 P(!owned,-102)
 g0*v0="shakti";
 g0*v[]={v0,owned,0};
 i rc=shakti_lang_main(2,v);
 free(owned);
 return(jint)rc;}
