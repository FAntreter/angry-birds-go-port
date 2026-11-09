# Stage 9 — guest import resolution audit

Generated from `readelf --dyn-syms` for bundled ARM32 libraries and HLE registration names found in bridge source.

**Important:** a name match is not proof of ABI correctness or runtime resolution. This audit does not account for symbol versions, visibility, exact ELF lookup scopes, or symbols obtained dynamically.

## Summary

- Undefined symbol names inspected: **1067** (counted per library/import occurrence).
- HLE-name matches: **795**.
- Names exported by another bundled guest ELF: **210**.
- Names with no matching guest export/HLE name: **62**.
- Unmatched names with dynamic relocations: **62**.
- Unmatched names without dynamic relocations: **0**.

## Unmatched imports by library

### `libABK.so` — 62 unmatched names
- `AConfiguration_delete` — R_ARM_JUMP_SLOT
- `AConfiguration_fromAssetManager` — R_ARM_JUMP_SLOT
- `AConfiguration_getCountry` — R_ARM_JUMP_SLOT
- `AConfiguration_getLanguage` — R_ARM_JUMP_SLOT
- `AConfiguration_new` — R_ARM_JUMP_SLOT
- `AInputEvent_getDeviceId` — R_ARM_JUMP_SLOT
- `AInputEvent_getSource` — R_ARM_JUMP_SLOT
- `AInputEvent_getType` — R_ARM_JUMP_SLOT
- `AInputQueue_attachLooper` — R_ARM_JUMP_SLOT
- `AInputQueue_detachLooper` — R_ARM_JUMP_SLOT
- `AInputQueue_finishEvent` — R_ARM_JUMP_SLOT
- `AInputQueue_getEvent` — R_ARM_JUMP_SLOT
- `AInputQueue_preDispatchEvent` — R_ARM_JUMP_SLOT
- `AKeyEvent_getAction` — R_ARM_JUMP_SLOT
- `AKeyEvent_getKeyCode` — R_ARM_JUMP_SLOT
- `ALooper_addFd` — R_ARM_JUMP_SLOT
- `ALooper_forThread` — R_ARM_JUMP_SLOT
- `ALooper_pollAll` — R_ARM_JUMP_SLOT
- `ALooper_prepare` — R_ARM_JUMP_SLOT
- `AMotionEvent_getAction` — R_ARM_JUMP_SLOT
- `AMotionEvent_getDownTime` — R_ARM_JUMP_SLOT
- `AMotionEvent_getEdgeFlags` — R_ARM_JUMP_SLOT
- `AMotionEvent_getEventTime` — R_ARM_JUMP_SLOT
- `AMotionEvent_getFlags` — R_ARM_JUMP_SLOT
- `AMotionEvent_getMetaState` — R_ARM_JUMP_SLOT
- `AMotionEvent_getOrientation` — R_ARM_JUMP_SLOT
- `AMotionEvent_getPointerCount` — R_ARM_JUMP_SLOT
- `AMotionEvent_getPointerId` — R_ARM_JUMP_SLOT
- `AMotionEvent_getPressure` — R_ARM_JUMP_SLOT
- `AMotionEvent_getSize` — R_ARM_JUMP_SLOT
- `AMotionEvent_getToolMajor` — R_ARM_JUMP_SLOT
- `AMotionEvent_getToolMinor` — R_ARM_JUMP_SLOT
- `AMotionEvent_getTouchMajor` — R_ARM_JUMP_SLOT
- `AMotionEvent_getTouchMinor` — R_ARM_JUMP_SLOT
- `AMotionEvent_getX` — R_ARM_JUMP_SLOT
- `AMotionEvent_getXPrecision` — R_ARM_JUMP_SLOT
- `AMotionEvent_getY` — R_ARM_JUMP_SLOT
- `AMotionEvent_getYPrecision` — R_ARM_JUMP_SLOT
- `ANativeActivity_finish` — R_ARM_JUMP_SLOT
- `ASensorEventQueue_disableSensor` — R_ARM_JUMP_SLOT
- `ASensorEventQueue_enableSensor` — R_ARM_JUMP_SLOT
- `ASensorEventQueue_getEvents` — R_ARM_JUMP_SLOT
- `ASensorEventQueue_setEventRate` — R_ARM_JUMP_SLOT
- `ASensorManager_createEventQueue` — R_ARM_JUMP_SLOT
- `ASensorManager_destroyEventQueue` — R_ARM_JUMP_SLOT
- `ASensorManager_getDefaultSensor` — R_ARM_JUMP_SLOT
- `ASensorManager_getInstance` — R_ARM_JUMP_SLOT
- `ASensorManager_getSensorList` — R_ARM_JUMP_SLOT
- `ASensor_getMinDelay` — R_ARM_JUMP_SLOT
- `ASensor_getType` — R_ARM_JUMP_SLOT
- `AndroidBitmap_getInfo` — R_ARM_JUMP_SLOT
- `AndroidBitmap_lockPixels` — R_ARM_JUMP_SLOT
- `__srget` — R_ARM_JUMP_SLOT
- `atol` — R_ARM_JUMP_SLOT
- `bsd_signal` — R_ARM_JUMP_SLOT
- `cacheflush` — R_ARM_JUMP_SLOT
- `inet_ntoa` — R_ARM_JUMP_SLOT
- `mprotect` — R_ARM_JUMP_SLOT
- `pthread_exit` — R_ARM_JUMP_SLOT
- `sched_get_priority_max` — R_ARM_JUMP_SLOT
- `sched_get_priority_min` — R_ARM_JUMP_SLOT
- `sigaltstack` — R_ARM_JUMP_SLOT

### `libSkynestSDK.so` — 0 unmatched names
No unmatched names in this static comparison.

### `libadcolony.so` — 0 unmatched names
No unmatched names in this static comparison.

### `libgnustl_shared.so` — 0 unmatched names
No unmatched names in this static comparison.

### `libjs.so` — 0 unmatched names
No unmatched names in this static comparison.

## Interpretation and next actions

1. The OpenSL ES IID names and bridge registrations are included in this audit; interface/vtable behavior still needs runtime validation.
2. Remaining unmatched names include libc, pthread, networking, process-control, and stdio symbols. Implement only functions with correct guest ABI and semantics; do not install generic no-op stubs for process or socket APIs.
3. Some names may be provided dynamically or through symbol-versioned dependencies; inspect ELF version tables and actual loader lookup scopes before treating every unmatched name as a fatal gap.
4. Runtime loader diagnostics now report the importing module, symbol, and relocation type for unresolved imports; device validation is still required.
5. NDK compilation and static checks do not establish launchability. Install/launch and gameplay validation on the target Android device remain required.

## Re-run
`python3 tools/audit_guest_import_resolution.py`
