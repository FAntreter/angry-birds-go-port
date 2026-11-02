# Stage 15 — Android build preflight and first real configure attempt

## Purpose

This stage shifts from general audits to reproducible build readiness. `tools/preflight_android_build.py` checks for CMake/Ninja, a real Android NDK toolchain file, a Dynarmic checkout with the A32 frontend headers, and the required generated bridge inputs. It deliberately fails clearly rather than reporting a false successful build.

## Result in the current environment

- `python3 tools/validate_build_inputs.py`: **PASS**.
- CMake configure attempted with `cmake -S . -B /tmp/abgo-stage15-build -G Ninja`: **BLOCKED** at the project's explicit Android guard, because CMake is running with the host GNU compiler and no Android NDK toolchain was found.
- No Dynarmic source checkout was found in the workspace or common locations.
- `tools/preflight_android_build.py`: expected to return status 2 with those missing prerequisites.

## Next action required

Provide/install an Android NDK and a compatible Dynarmic source checkout with its A32 frontend. Then run the exact configure command printed by the preflight tool and address compile errors. A successful configure/compile still would not mean a playable game: EGL/native-window forwarding, OpenSL ES, import resolution, APK packaging, and device testing remain.

## Honesty boundary

This stage does not claim to build an APK. It captures the first actual CMake configure attempt and adds a reusable preflight to avoid spending more stages on checks that cannot lead to compilation in this environment.
