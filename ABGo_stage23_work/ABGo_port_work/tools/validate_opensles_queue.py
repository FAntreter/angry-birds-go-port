#!/usr/bin/env python3
"""Static checks for OpenSL ES buffer-queue bookkeeping semantics."""
from pathlib import Path
s=(Path(__file__).resolve().parents[1]/'bridge_src/hle_opensles.cpp').read_text()
checks={
 'guest buffer address and size retained':'struct QueuedBuffer { uint32_t address; uint32_t size; };' in s and 'o->queued_buffers.push_back({data.a,size})' in s,
 'queue capacity bounded':'SL_MAX_QUEUED_BUFFERS = 64' in s and 'SL_RESULT_RESOURCE_ERROR' in s,
 'queue state reports count and play index':'out.h()[0]=static_cast<uint32_t>(o->queued_buffers.size())' in s and 'out.h()[1]=o->queue_play_index' in s,
 'clear empties queue and resets index':'o->queued_buffers.clear(); o->queue_play_index=0' in s,
 'callback and context retained':'o->queue_callback=callback; o->queue_context=context' in s,
 'queue methods validate player kind':'o->kind!=kPlayer' in s,
}
for k,v in checks.items(): print(('PASS' if v else 'FAIL')+' '+k)
raise SystemExit(0 if all(checks.values()) else 1)
