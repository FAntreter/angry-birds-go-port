# Build 134 device-log analysis — 2026-11-07

## Input

- Uploaded log: `abgo_bridge_crash.txt`
- Size: 368,180 bytes
- Lines: 5,348
- Associated diagnostic bridge: workflow build #134, commit `9beed9ade06022c09cd52efe9ab47d3c796a2cbe`

## Confirmed startup progress

The log shows:

1. The ARM64 bridge's `JNI_OnLoad` stub entered and the guest runtime initialized.
2. Guest `libgnustl_shared.so`, `libSkynestSDK.so`, `libABK.so`, `libjs.so`, and `libadcolony.so` loaded; their logged constructors completed.
3. The bridge bound 96 native methods and invoked guest `JNI_OnLoad`.
4. Guest `JNI_OnLoad` returned JNI version `0x10006`.
5. The first game-native callback entered and returned:
   - Method: `com/rovio/angrybirdsgo/AngryBirdsGoActivity.isDeviceAmazon`
   - Runtime guest address: `0x01b2f578`
   - Logged result: `0x0000000500000000`

## Static verification of the first callback

The staged ARM32 `libABK_arm32.so` exports this method at ELF virtual address `0x0046f578`. The runtime address in the log implies load bias `0x016c0000`, consistent with that symbol.

Disassembly of the exact function is:

```asm
0046f578: e3a00000    mov r0, #0
0046f57c: e12fff1e    bx lr
```

Therefore the native method is a simple successful return of boolean false. The low 32 bits of the logged return value are zero, as expected for this method. The high 32 bits (`R1=5`) are not part of a JNI boolean return and should not be interpreted as its value.

## What remains unresolved

- The captured log ends immediately after the first JNI callback returns.
- No later game JNI callback appears.
- No EGL display/surface/make-current/swap event appears.
- No explicit guest fault or bridge fatal error appears after the callback.

This narrows the observed stall: the guest engine has loaded and the first JNI method executes and returns. The log does **not** establish why the Java Activity does not progress to the Exient GL-surface lifecycle. A Java-side exception, Activity/view initialization issue, or a log capture ending before later work remain possible; none is proven by this bridge-only log.

## Instrumentation note

Build #134's generic first-eight-call CPU probe was consumed by early C++ runtime constructor calls (the log shows probes for `libgnustl_shared.so` init-array entries). It did not single-step the later `isDeviceAmazon` callback. That limitation is now understood; the callback itself was independently verified by ELF symbol lookup and disassembly.

## Next diagnostic gate

Prioritize observing the Android-side Activity and GL-view lifecycle (including Java exceptions) between the return from `isDeviceAmazon` and the first Exient surface callback. Do not change the native method's return value: its disassembly and observed return are correct. Do not claim rendering or gameplay until EGL surface creation and frame submission are observed and the game is tested on-device.
