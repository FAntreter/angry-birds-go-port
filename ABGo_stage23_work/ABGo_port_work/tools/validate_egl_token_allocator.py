#!/usr/bin/env python3
"""Static regression checks for collision-safe guest EGL/native handle tokens."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
s = (root / 'bridge_src/hle_egl.cpp').read_text()
assert 'uint32_t allocate_token_locked()' in s
assert 'bool token_in_use(uint32_t id)' in s
for table in ('EGLDisplay', 'EGLConfig', 'EGLContext', 'EGLSurface', 'ANativeWindow*'):
    assert f'table<{table}>().count(id)' in s, f'token collision scan omits {table}'
assert 'if (!token_in_use(candidate)) return candidate;' in s
assert 'while (g_next != initial)' in s
assert 'if (!id) return 0;' in s
assert 'if (!id) { ANativeWindow_release(w); return 0; }' in s
assert s.count('g_next += 4') == 1, 'token counter should only advance in central allocator'
print('PASS: shared token allocator checks every live handle table, handles wrap, and fails safely on exhaustion')
