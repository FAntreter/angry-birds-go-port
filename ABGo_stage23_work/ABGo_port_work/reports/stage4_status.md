# Stage 4 status — dependency graph

## Verified output

Running `tools/plan_guest_load_order.py` against `target_libs/` produced this dependency-first guest order for `libABK.so`:

1. `libgnustl_shared.so`
2. `libSkynestSDK.so`
3. `libABK.so`

The root engine references Android/HLE libraries including `libEGL.so`, `libGLESv2.so`, `libOpenSLES.so`, `libandroid.so`, `libjnigraphics.so`, `liblog.so`, `libm.so`, `libc.so`, and `libdl.so`. Those names are categorized as host-side dependencies, but their guest-call forwarding is not thereby implemented.

## Caveats

- The script uses host `readelf` to inspect ELF metadata; it is a planning tool, not runtime code.
- `libadcolony.so` and `libjs.so` are not in the root engine's direct dependency closure; Java may load them separately for ads. Do not load them unconditionally without checking the Java load path.
- The current `elf::load()` still resolves imports as each module is loaded, so loading in dependency-first order is necessary but may not be sufficient. Constructor timing and cross-module relocation behavior still require integration and tests.
- No Android APK build or device runtime test has been performed.
