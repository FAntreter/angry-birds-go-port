#!/usr/bin/env python3
"""Static guardrails for the guest-ABI OpenSL ES compatibility layer."""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
s=(root/'bridge_src/hle_opensles.cpp').read_text()
h=(root/'bridge_src/hle.h').read_text(); init=(root/'bridge_src/hle.cpp').read_text(); cm=(root/'CMakeLists.txt').read_text()
checks={
 'slCreateEngine registered': 'H("slCreateEngine",g_sl_create_engine)' in s,
 'all seven imported IID symbols mapped': all(f'"{n}"' in s for n in ['SL_IID_ANDROIDSIMPLEBUFFERQUEUE','SL_IID_ENGINE','SL_IID_ENGINECAPABILITIES','SL_IID_PLAY','SL_IID_PLAYBACKRATE','SL_IID_SEEK','SL_IID_VOLUME']),
 'guest-owned vtables allocated in arena': 'make_vtable' in s and 'arena::static_alloc' in s,
 'object/engine/play/queue/volume interfaces present': all(x in s for x in ['v_object','v_engine','v_play','v_queue','v_volume']),
 'queue operations present': all(x in s for x in ['g_queue_enqueue','g_queue_clear','g_queue_state']),
 'play-state operations present': all(x in s for x in ['g_play_set_state','g_play_get_state']),
 'volume operations present': all(x in s for x in ['g_volume_set','g_volume_get','g_volume_mute']),
 'registration declared and initialized': 'void register_opensles();' in h and 'register_opensles();' in init,
 'source included in CMake': 'bridge_src/hle_opensles.cpp' in cm,
 'silent behavior explicitly documented': 'does NOT yet' in s and 'produce sound' in s,
}
for name,ok in checks.items(): print(('PASS' if ok else 'FAIL')+' '+name)
raise SystemExit(0 if all(checks.values()) else 1)
