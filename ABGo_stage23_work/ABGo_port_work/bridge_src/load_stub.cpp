// ARM64 placeholder libraries satisfy the original Java System.loadLibrary calls.
// Their ARM32 implementations are loaded privately by libABK.so through Dynarmic.
// Record this earliest native-load point so a missing bridge log is diagnosable.
#include <jni.h>
#include <android/log.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>

namespace {
void write_load_marker() {
    constexpr char kLine[] = "ARM64 load stub JNI_OnLoad entered\n";
    const char* paths[] = {
        "/sdcard/Download/abgo_bridge_crash.txt",
        "/storage/emulated/0/Download/abgo_bridge_crash.txt",
        "/data/user/0/com.rovio.angrybirdsgo/files/abgo_bridge_crash.txt",
        "/data/data/com.rovio.angrybirdsgo/files/abgo_bridge_crash.txt",
        "/sdcard/Android/data/com.rovio.angrybirdsgo/files/abgo_bridge_crash.txt"
    };
    __android_log_print(ANDROID_LOG_INFO, "absw", "%s", kLine);
    for (const char* path : paths) {
        int fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0660);
        if (fd < 0) continue;
        const char* p = kLine;
        size_t n = sizeof(kLine) - 1;
        while (n) {
            ssize_t written = write(fd, p, n);
            if (written <= 0) break;
            p += written;
            n -= static_cast<size_t>(written);
        }
        fsync(fd);
        close(fd);
    }
}
}

extern "C" JNIEXPORT jint JNI_OnLoad(JavaVM*, void*) {
    write_load_marker();
    return JNI_VERSION_1_6;
}
