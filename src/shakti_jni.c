#include "a.h"
#include <jni.h>
#include <stdlib.h>
#include <string.h>

/* Optional JVM bridge — not linked into .build/shakti. Build via `make shakti_jni.o`
 * (writes .build/shakti_jni.o) when an external Java/Android host needs
 * Java_com_shakti_shakti_ShaktiNative_runFile. */

extern int shakti_lang_main(int argc, char **argv);

JNIEXPORT jint JNICALL
Java_com_shakti_shakti_ShaktiNative_runFile(JNIEnv *e, jclass z, jstring p) {
    (void)z;
    P(!p, -100)
    const char *path = (*e)->GetStringUTFChars(e, p, 0);
    P(!path, -101)
    char *owned = strdup(path);
    (*e)->ReleaseStringUTFChars(e, p, path);
    P(!owned, -102)
    char *argv0 = "shakti";
    char *argv[] = {argv0, owned, 0};
    int rc = shakti_lang_main(2, argv);
    free(owned);
    return (jint)rc;
}
