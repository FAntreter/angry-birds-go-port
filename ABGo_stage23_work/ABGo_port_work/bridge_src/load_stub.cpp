// ARM64 placeholder libraries satisfy the original Java System.loadLibrary calls.
// Their ARM32 implementations are loaded privately by libABK.so through Dynarmic.
#include <jni.h>

extern "C" JNIEXPORT jint JNI_OnLoad(JavaVM*, void*) {
    return JNI_VERSION_1_6;
}
