#!/usr/bin/env python3
"""Static checks for OpenSL ES object lifetime and queue concurrency guards."""
from pathlib import Path
s=(Path(__file__).resolve().parents[1]/'bridge_src/hle_opensles.cpp').read_text()
checks={
 'per-object state mutex':'std::mutex state_mu;' in s,
 'destroy marks object invalid':'o->destroyed=true' in s,
 'destroy flushes queued buffers':'o->queued_buffers.clear()' in s and 'o->queue_play_index=0' in s,
 'destroy clears guest callback references':'o->queue_callback=0' in s and 'o->queue_context=0' in s,
 'play state operations lock object state':'g_play_set_state' in s and 'std::lock_guard<std::mutex> l(o->state_mu); if(o->destroyed)' in s,
 'enqueue validates guest buffer memory':'arena::contains(data.h(),size)' in s,
 'enqueue is synchronized and checks dead objects':'std::lock_guard<std::mutex> l(o->state_mu);\n    if(o->destroyed) return SL_RESULT_PARAMETER_INVALID;' in s,
 'queue state reads are synchronized':'g_queue_state' in s and 'out.h()[0]=static_cast<uint32_t>(o->queued_buffers.size())' in s,
}
for name,ok in checks.items(): print(('PASS' if ok else 'FAIL')+' '+name)
raise SystemExit(0 if all(checks.values()) else 1)
