// Stage 24: thread-safe guest-ABI OpenSL ES compatibility layer.
// This deliberately supplies silent/no-op playback objects. It keeps ARM32 guest
// interface pointers and vtables inside the guest address space; it does NOT yet
// produce sound or translate guest callbacks into emulator callbacks.
#include <cstdint>
#include <cstring>
#include <deque>
#include <mutex>
#include <unordered_map>
#include "hle.h"

namespace {
constexpr uint32_t SL_SUCCESS = 0;
constexpr uint32_t SL_RESULT_PARAMETER_INVALID = 3;
constexpr uint32_t SL_RESULT_FEATURE_UNSUPPORTED = 12;
constexpr uint32_t SL_RESULT_RESOURCE_ERROR = 9;
constexpr size_t SL_MAX_QUEUED_BUFFERS = 64;
constexpr uint32_t SL_PLAYSTATE_STOPPED = 1;
constexpr uint32_t SL_PLAYSTATE_PAUSED = 2;
constexpr uint32_t SL_PLAYSTATE_PLAYING = 3;
constexpr uint32_t SL_OBJECT_STATE_REALIZED = 2;

enum Kind : uint32_t { kEngine=1, kOutputMix=2, kPlayer=3 };
struct QueuedBuffer { uint32_t address; uint32_t size; };
struct Obj {
    // Guest calls can arrive from multiple emulated threads. Keep per-object
    // mutable state independent from the global interface-cell registry lock.
    std::mutex state_mu;
    bool destroyed=false;
    Kind kind;
    uint32_t object_ifc=0, engine_ifc=0, play_ifc=0, queue_ifc=0, volume_ifc=0;
    uint32_t state=0, play_state=SL_PLAYSTATE_STOPPED;
    int32_t volume_millibel=0;
    uint32_t muted=0;
    uint32_t queue_callback=0, queue_context=0, queue_play_index=0;
    std::deque<QueuedBuffer> queued_buffers;
};
std::mutex mu;
std::unordered_map<uint32_t, Obj*> objects; // guest interface-cell address -> owning object
uint32_t iid_addr[7]{}; // ANDROIDSIMPLEBUFFERQUEUE, ENGINE, ENGINECAPABILITIES, PLAY, PLAYBACKRATE, SEEK, VOLUME
uint32_t v_object=0, v_engine=0, v_play=0, v_queue=0, v_volume=0;

uint32_t make_cell(uint32_t vtable) {
    uint32_t cell=arena::static_alloc(4,4); *arena::hp<uint32_t>(cell)=vtable; return cell;
}
uint32_t make_vtable(const uint32_t* entries, size_t n) {
    uint32_t p=arena::static_alloc(static_cast<uint32_t>(n*4),4);
    std::memcpy(arena::h(p),entries,n*4); return p;
}
Obj* find_obj(uint32_t cell) { std::lock_guard<std::mutex> l(mu); auto i=objects.find(cell); return i==objects.end()?nullptr:i->second; }
uint32_t bind_interface(Obj* o, uint32_t vt, uint32_t Obj::*member) {
    uint32_t& slot=o->*member; if(!slot) { slot=make_cell(vt); std::lock_guard<std::mutex> l(mu); objects[slot]=o; } return slot;
}
uint32_t new_object(Kind k, GPtr<uint32_t> out) {
    if(!out) return SL_RESULT_PARAMETER_INVALID;
    Obj* o=new Obj{}; o->kind=k;
    o->object_ifc=bind_interface(o,v_object,&Obj::object_ifc);
    { std::lock_guard<std::mutex> l(mu); objects[o->object_ifc]=o; }
    *out.h()=o->object_ifc; return SL_SUCCESS;
}
uint32_t g_obj_realize(uint32_t self,uint32_t) { Obj* o=find_obj(self); if(!o)return SL_RESULT_PARAMETER_INVALID; std::lock_guard<std::mutex> l(o->state_mu); if(o->destroyed)return SL_RESULT_PARAMETER_INVALID; o->state=SL_OBJECT_STATE_REALIZED; return SL_SUCCESS; }
uint32_t g_obj_get_state(uint32_t self,GPtr<uint32_t> out) { Obj* o=find_obj(self); if(!o||!out)return SL_RESULT_PARAMETER_INVALID; std::lock_guard<std::mutex> l(o->state_mu); if(o->destroyed)return SL_RESULT_PARAMETER_INVALID; *out.h()=o->state; return SL_SUCCESS; }
uint32_t g_obj_get_interface(uint32_t self,uint32_t iid,GPtr<uint32_t> out) {
    Obj* o=find_obj(self); if(!o||!out)return SL_RESULT_PARAMETER_INVALID;
    uint32_t result=0;
    if(iid==iid_addr[1] && o->kind==kEngine) result=bind_interface(o,v_engine,&Obj::engine_ifc);
    else if(iid==iid_addr[3] && o->kind==kPlayer) result=bind_interface(o,v_play,&Obj::play_ifc);
    else if(iid==iid_addr[0] && o->kind==kPlayer) result=bind_interface(o,v_queue,&Obj::queue_ifc);
    else if(iid==iid_addr[6] && o->kind==kPlayer) result=bind_interface(o,v_volume,&Obj::volume_ifc);
    if(!result) return SL_RESULT_FEATURE_UNSUPPORTED;
    *out.h()=result; return SL_SUCCESS;
}
void g_obj_destroy(uint32_t self) { Obj* o=find_obj(self); if(!o)return; std::lock_guard<std::mutex> l(o->state_mu); o->destroyed=true; o->state=0; o->play_state=SL_PLAYSTATE_STOPPED; o->queued_buffers.clear(); o->queue_play_index=0; o->queue_callback=0; o->queue_context=0; /* static cells remain allocated, but calls reject this object */ }
uint32_t g_engine_create_player(uint32_t self,GPtr<uint32_t> out,uint32_t,uint32_t,uint32_t,GPtr<uint32_t>,GPtr<uint32_t>) { Obj* o=find_obj(self); if(!o||o->kind!=kEngine)return SL_RESULT_PARAMETER_INVALID; return new_object(kPlayer,out); }
uint32_t g_engine_create_mix(uint32_t self,GPtr<uint32_t> out,uint32_t,GPtr<uint32_t>,GPtr<uint32_t>) { Obj* o=find_obj(self); if(!o||o->kind!=kEngine)return SL_RESULT_PARAMETER_INVALID; return new_object(kOutputMix,out); }
uint32_t g_play_set_state(uint32_t self,uint32_t state) { Obj* o=find_obj(self); if(!o||o->kind!=kPlayer)return SL_RESULT_PARAMETER_INVALID; if(state<SL_PLAYSTATE_STOPPED||state>SL_PLAYSTATE_PLAYING)return SL_RESULT_PARAMETER_INVALID; std::lock_guard<std::mutex> l(o->state_mu); if(o->destroyed)return SL_RESULT_PARAMETER_INVALID; o->play_state=state; return SL_SUCCESS; }
uint32_t g_play_get_state(uint32_t self,GPtr<uint32_t> out) { Obj* o=find_obj(self); if(!o||!out)return SL_RESULT_PARAMETER_INVALID; std::lock_guard<std::mutex> l(o->state_mu); if(o->destroyed)return SL_RESULT_PARAMETER_INVALID; *out.h()=o->play_state; return SL_SUCCESS; }
uint32_t g_queue_enqueue(uint32_t self,GPtr<void> data,uint32_t size) {
    Obj* o=find_obj(self);
    if(!o||o->kind!=kPlayer||!data||!size) return SL_RESULT_PARAMETER_INVALID;
    if(!arena::contains(data.h(),size)) return SL_RESULT_PARAMETER_INVALID;
    std::lock_guard<std::mutex> l(o->state_mu);
    if(o->destroyed) return SL_RESULT_PARAMETER_INVALID;
    if(o->queued_buffers.size()>=SL_MAX_QUEUED_BUFFERS) return SL_RESULT_RESOURCE_ERROR;
    // Keep guest addresses, not host pointers: the guest arena owns this memory.
    o->queued_buffers.push_back({data.a,size});
    return SL_SUCCESS;
}
uint32_t g_queue_clear(uint32_t self) {
    Obj* o=find_obj(self); if(!o||o->kind!=kPlayer)return SL_RESULT_PARAMETER_INVALID;
    std::lock_guard<std::mutex> l(o->state_mu); if(o->destroyed)return SL_RESULT_PARAMETER_INVALID;
    o->queued_buffers.clear(); o->queue_play_index=0; return SL_SUCCESS;
}
uint32_t g_queue_state(uint32_t self,GPtr<uint32_t> out) {
    Obj* o=find_obj(self); if(!o||o->kind!=kPlayer||!out)return SL_RESULT_PARAMETER_INVALID;
    std::lock_guard<std::mutex> l(o->state_mu); if(o->destroyed)return SL_RESULT_PARAMETER_INVALID;
    out.h()[0]=static_cast<uint32_t>(o->queued_buffers.size());
    out.h()[1]=o->queue_play_index;
    return SL_SUCCESS;
}
uint32_t g_queue_callback(uint32_t self,uint32_t callback,uint32_t context) {
    Obj* o=find_obj(self); if(!o||o->kind!=kPlayer)return SL_RESULT_PARAMETER_INVALID;
    std::lock_guard<std::mutex> l(o->state_mu); if(o->destroyed)return SL_RESULT_PARAMETER_INVALID;
    o->queue_callback=callback; o->queue_context=context; return SL_SUCCESS;
}
uint32_t g_volume_set(uint32_t self,int32_t mb) {
    Obj* o=find_obj(self); if(!o)return SL_RESULT_PARAMETER_INVALID;
    std::lock_guard<std::mutex> l(o->state_mu); if(o->destroyed)return SL_RESULT_PARAMETER_INVALID;
    o->volume_millibel=mb; return SL_SUCCESS;
}
uint32_t g_volume_get(uint32_t self,GPtr<int32_t> out) {
    Obj* o=find_obj(self); if(!o||!out)return SL_RESULT_PARAMETER_INVALID;
    std::lock_guard<std::mutex> l(o->state_mu); if(o->destroyed)return SL_RESULT_PARAMETER_INVALID;
    *out.h()=o->volume_millibel; return SL_SUCCESS;
}
uint32_t g_volume_mute(uint32_t self,uint32_t mute) {
    Obj* o=find_obj(self); if(!o)return SL_RESULT_PARAMETER_INVALID;
    std::lock_guard<std::mutex> l(o->state_mu); if(o->destroyed)return SL_RESULT_PARAMETER_INVALID;
    o->muted=!!mute; return SL_SUCCESS;
}
uint32_t g_volume_get_mute(uint32_t self,GPtr<uint32_t> out) {
    Obj* o=find_obj(self); if(!o||!out)return SL_RESULT_PARAMETER_INVALID;
    std::lock_guard<std::mutex> l(o->state_mu); if(o->destroyed)return SL_RESULT_PARAMETER_INVALID;
    *out.h()=o->muted; return SL_SUCCESS;
}
uint32_t g_volume_max(uint32_t self,GPtr<int32_t> out) {
    Obj* o=find_obj(self); if(!o||!out)return SL_RESULT_PARAMETER_INVALID;
    std::lock_guard<std::mutex> l(o->state_mu); if(o->destroyed)return SL_RESULT_PARAMETER_INVALID;
    *out.h()=0; return SL_SUCCESS;
}
uint32_t g_volume_stereo_enable(uint32_t self,uint32_t) {
    Obj* o=find_obj(self); if(!o)return SL_RESULT_PARAMETER_INVALID;
    std::lock_guard<std::mutex> l(o->state_mu); return o->destroyed?SL_RESULT_PARAMETER_INVALID:SL_SUCCESS;
}
uint32_t g_volume_stereo_enabled(uint32_t self,GPtr<uint32_t> out) {
    Obj* o=find_obj(self); if(!o||!out)return SL_RESULT_PARAMETER_INVALID;
    std::lock_guard<std::mutex> l(o->state_mu); if(o->destroyed)return SL_RESULT_PARAMETER_INVALID;
    *out.h()=1; return SL_SUCCESS;
}
uint32_t g_volume_stereo_set(uint32_t self,uint32_t,int32_t) {
    Obj* o=find_obj(self); if(!o)return SL_RESULT_PARAMETER_INVALID;
    std::lock_guard<std::mutex> l(o->state_mu); return o->destroyed?SL_RESULT_PARAMETER_INVALID:SL_SUCCESS;
}
uint32_t g_volume_stereo_get(uint32_t self,GPtr<int32_t> out,GPtr<uint32_t> enabled) {
    Obj* o=find_obj(self); if(!o||!out||!enabled)return SL_RESULT_PARAMETER_INVALID;
    std::lock_guard<std::mutex> l(o->state_mu); if(o->destroyed)return SL_RESULT_PARAMETER_INVALID;
    *out.h()=0; *enabled.h()=1; return SL_SUCCESS;
}
uint32_t g_noop_result(uint32_t self) {
    Obj* o=find_obj(self); if(!o)return SL_RESULT_PARAMETER_INVALID;
    std::lock_guard<std::mutex> l(o->state_mu); return o->destroyed?SL_RESULT_PARAMETER_INVALID:SL_SUCCESS;
}
uint32_t g_noop_u32(uint32_t self,uint32_t) { return g_noop_result(self); }
uint32_t g_noop_out_u32(uint32_t self,GPtr<uint32_t> out) {
    if(!out)return SL_RESULT_PARAMETER_INVALID;
    uint32_t r=g_noop_result(self); if(r!=SL_SUCCESS)return r;
    *out.h()=0; return SL_SUCCESS;
}
uint32_t g_engine_query_num(uint32_t self,GPtr<uint32_t> out) {
    if(!out)return SL_RESULT_PARAMETER_INVALID;
    uint32_t r=g_noop_result(self); if(r!=SL_SUCCESS)return r;
    *out.h()=0; return SL_SUCCESS;
}
uint32_t g_engine_query_ifaces(uint32_t self,uint32_t,GPtr<uint32_t>) { return g_noop_result(self); }
uint32_t g_sl_create_engine(GPtr<uint32_t> out,uint32_t,GPtr<void>,uint32_t,GPtr<uint32_t>,GPtr<uint32_t>) { return new_object(kEngine,out); }

uint32_t thunk(const char* name, hle::Fn fn) { return hle::add(name,fn); }
const hle::Def defs[] = {
 H("slCreateEngine",g_sl_create_engine),
 H("opensl_obj_realize",g_obj_realize), H("opensl_obj_get_state",g_obj_get_state), H("opensl_obj_get_interface",g_obj_get_interface), H("opensl_obj_destroy",g_obj_destroy),
 H("opensl_engine_create_player",g_engine_create_player), H("opensl_engine_create_mix",g_engine_create_mix),
 H("opensl_play_set_state",g_play_set_state), H("opensl_play_get_state",g_play_get_state),
 H("opensl_queue_enqueue",g_queue_enqueue), H("opensl_queue_clear",g_queue_clear), H("opensl_queue_state",g_queue_state), H("opensl_queue_callback",g_queue_callback),
 H("opensl_volume_set",g_volume_set), H("opensl_volume_get",g_volume_get), H("opensl_volume_mute",g_volume_mute), H("opensl_volume_get_mute",g_volume_get_mute), H("opensl_volume_max",g_volume_max), H("opensl_volume_stereo_enable",g_volume_stereo_enable), H("opensl_volume_stereo_enabled",g_volume_stereo_enabled), H("opensl_volume_stereo_set",g_volume_stereo_set), H("opensl_volume_stereo_get",g_volume_stereo_get),
 H("opensl_noop_result",g_noop_result), H("opensl_noop_u32",g_noop_u32), H("opensl_noop_out_u32",g_noop_out_u32), H("opensl_engine_query_num",g_engine_query_num), H("opensl_engine_query_ifaces",g_engine_query_ifaces)
};
}
void hle::register_opensles() {
    add_defs(defs,sizeof(defs)/sizeof(defs[0]));
    // Create guest-layout IID storage. Equality is by imported symbol address, so UUID
    // byte values are intentionally not relied on by this bridge layer.
    const char* names[]={"SL_IID_ANDROIDSIMPLEBUFFERQUEUE","SL_IID_ENGINE","SL_IID_ENGINECAPABILITIES","SL_IID_PLAY","SL_IID_PLAYBACKRATE","SL_IID_SEEK","SL_IID_VOLUME"};
    for(size_t i=0;i<7;++i){ iid_addr[i]=arena::static_alloc(16,4); std::memset(arena::h(iid_addr[i]),0,16); hle::add_data(names[i],iid_addr[i]); }
    uint32_t object_entries[9]={hle::resolve("opensl_obj_realize"),hle::resolve("opensl_obj_get_state"),hle::resolve("opensl_obj_get_interface"),hle::resolve("opensl_noop_u32"),hle::resolve("opensl_noop_result"),hle::resolve("opensl_obj_destroy"),hle::resolve("opensl_noop_u32"),hle::resolve("opensl_noop_out_u32"),hle::resolve("opensl_noop_u32")};
    uint32_t engine_entries[12]; for(auto& e:engine_entries)e=hle::resolve("opensl_noop_result"); engine_entries[2]=hle::resolve("opensl_engine_create_player"); engine_entries[7]=hle::resolve("opensl_engine_create_mix"); engine_entries[10]=hle::resolve("opensl_engine_query_ifaces"); engine_entries[11]=hle::resolve("opensl_engine_query_num");
    uint32_t play_entries[12]; for(auto& e:play_entries)e=hle::resolve("opensl_noop_result"); play_entries[0]=hle::resolve("opensl_play_set_state"); play_entries[1]=hle::resolve("opensl_play_get_state");
    uint32_t queue_entries[6]={hle::resolve("opensl_queue_enqueue"),hle::resolve("opensl_queue_clear"),hle::resolve("opensl_queue_state"),hle::resolve("opensl_queue_callback"),hle::resolve("opensl_noop_u32"),hle::resolve("opensl_noop_out_u32")};
    uint32_t volume_entries[9]={hle::resolve("opensl_volume_set"),hle::resolve("opensl_volume_get"),hle::resolve("opensl_volume_max"),hle::resolve("opensl_volume_mute"),hle::resolve("opensl_volume_get_mute"),hle::resolve("opensl_volume_stereo_enable"),hle::resolve("opensl_volume_stereo_enabled"),hle::resolve("opensl_volume_stereo_set"),hle::resolve("opensl_volume_stereo_get")};
    v_object=make_vtable(object_entries,9); v_engine=make_vtable(engine_entries,12); v_play=make_vtable(play_entries,12); v_queue=make_vtable(queue_entries,6); v_volume=make_vtable(volume_entries,9);
}
