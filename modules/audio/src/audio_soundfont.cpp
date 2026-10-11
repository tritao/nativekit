#include "audio_soundfont.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#ifdef NK_AUDIO_TESTING
#include "nativekit_audio_dsp.h"
#include <atomic>
#include <cstdlib>
namespace {
thread_local bool in_soundfont_callback = false;
std::atomic<uint64_t> callback_allocations{0}, callback_frees{0};
void *test_malloc(size_t bytes) { if (in_soundfont_callback) ++callback_allocations; return std::malloc(bytes); }
void *test_realloc(void *p, size_t bytes) { if (in_soundfont_callback) ++callback_allocations; return std::realloc(p,bytes); }
void test_free(void *p) { if (p && in_soundfont_callback) ++callback_frees; std::free(p); }
}
#define TSF_MALLOC test_malloc
#define TSF_REALLOC test_realloc
#define TSF_FREE test_free
extern "C" NKAUDIO_API void NK_CALL nk_audio_test_soundfont_callback_counts(uint64_t *allocated, uint64_t *freed) {
    *allocated = callback_allocations.load(); *freed = callback_frees.load();
}
#endif
#define TSF_IMPLEMENTATION
#define TSF_NO_STDIO
#include "tsf.h"

namespace nk::audio_dsp {
#ifdef NK_AUDIO_TESTING
void soundfont_callback_begin() noexcept { in_soundfont_callback = true; }
void soundfont_callback_end() noexcept { in_soundfont_callback = false; }
#endif
namespace {
uint32_t u32(const unsigned char *p) {
    return uint32_t(p[0]) | uint32_t(p[1])<<8 | uint32_t(p[2])<<16 | uint32_t(p[3])<<24;
}
uint32_t u16(const unsigned char *p) { return uint32_t(p[0]) | uint32_t(p[1])<<8; }
struct Chunk { const unsigned char *data = nullptr; uint32_t size = 0; };
// Validate RIFF bounds and hydra indices before passing data to the upstream parser.
// The supported format is uncompressed SF2; duplicate tables and SF3 are rejected.
bool validate(const unsigned char *p, uint32_t size) {
    if (!p || size < 12 || size > 256u*1024*1024 || std::memcmp(p,"RIFF",4) ||
        std::memcmp(p+8,"sfbk",4) || uint64_t(u32(p+4))+8 != size) return false;
    const char *names[] = {"phdr","pbag","pmod","pgen","inst","ibag","imod","igen","shdr","smpl"};
    const uint32_t widths[] = {38,4,10,4,22,4,10,4,46,2};
    std::array<Chunk,10> chunks{};
    for (uint64_t pos = 12; pos < size;) {
        if (pos+8 > size) return false;
        const auto bytes = u32(p+pos+4);
        const uint64_t end = pos+8+bytes;
        if (end+(bytes&1) > size || (bytes&1)) return false;
        if (!std::memcmp(p+pos,"LIST",4)) {
            if (bytes < 4) return false;
            const bool hydra = !std::memcmp(p+pos+8,"pdta",4);
            const bool samples = !std::memcmp(p+pos+8,"sdta",4);
            for (uint64_t child=pos+12; child < end;) {
                if (child+8 > end) return false;
                const auto length = u32(p+child+4);
                if (child+8+length+(length&1) > end || ((hydra || samples) && (length&1))) return false;
                for (uint32_t i=0; i<10; ++i) {
                    if ((i==9 ? samples : hydra) && !std::memcmp(p+child,names[i],4)) {
                        if (chunks[i].data || !length || length%widths[i]) return false;
                        chunks[i] = {p+child+8,length};
                    }
                }
                child += 8+length+(length&1);
            }
        }
        pos=end+(bytes&1);
    }
    std::array<uint32_t,10> counts{};
    for (uint32_t i=0; i<10; ++i) {
        if (!chunks[i].data) return false;
        counts[i]=chunks[i].size/widths[i];
        if (i<9 && counts[i]>65536) return false;
    }
    if (counts[0]<2 || counts[0]>4097 || counts[4]<2 || counts[8]<2 || counts[9]<48) return false;
    // Header and bag terminal indices must form ordered, bounded ranges.
    for (const auto pair : {std::array<uint32_t,4>{0,24,1,0}, {4,20,5,0},
                           {1,0,3,0}, {1,2,2,0}, {5,0,7,0}, {5,2,6,0}}) {
        uint32_t previous=0;
        for (uint32_t n=0; n<counts[pair[0]]; ++n) {
            const auto value=u16(chunks[pair[0]].data+n*widths[pair[0]]+pair[1]);
            if (value<previous || value>=counts[pair[2]]) return false;
            previous=value;
        }
    }
    for (uint32_t table : {3u,7u}) {
        for (uint32_t n=0; n<counts[table]; ++n) {
            const auto gen=chunks[table].data+n*4;
            const auto op=u16(gen), amount=u16(gen+2);
            if ((op==41 && (table!=3 || amount>=counts[4]-1)) ||
                (op==53 && (table!=7 || amount>=counts[8]-1))) return false;
        }
    }
    // Bound the parser's preset-region expansion before it allocates those
    // arrays. Count conservatively across key/velocity filters in linear time.
    std::vector<uint32_t> instrument_regions(counts[4]-1);
    for (uint32_t i=0;i+1<counts[4];++i) {
        const auto begin=u16(chunks[4].data+i*22+20), end=u16(chunks[4].data+(i+1)*22+20);
        for (uint32_t bag=begin;bag<end;++bag) {
            const auto first=u16(chunks[5].data+bag*4), last=u16(chunks[5].data+(bag+1)*4);
            for (uint32_t gen=first;gen<last;++gen)
                if (u16(chunks[7].data+gen*4)==53) ++instrument_regions[i];
        }
    }
    uint64_t total_regions=0;
    for (uint32_t i=0;i+1<counts[0];++i) {
        uint64_t regions=0;
        const auto begin=u16(chunks[0].data+i*38+24), end=u16(chunks[0].data+(i+1)*38+24);
        for (uint32_t bag=begin;bag<end;++bag) {
            const auto first=u16(chunks[1].data+bag*4), last=u16(chunks[1].data+(bag+1)*4);
            for (uint32_t gen=first;gen<last;++gen) if (u16(chunks[3].data+gen*4)==41) {
                regions+=instrument_regions[u16(chunks[3].data+gen*4+2)];
                if (regions>4096) return false;
            }
        }
        total_regions+=regions;
        if (total_regions>65536) return false;
    }
    for (uint32_t n=0; n+1<counts[8]; ++n) {
        const auto header=chunks[8].data+n*46;
        const auto start=u32(header+20), end=u32(header+24);
        const auto loop_start=u32(header+28), loop_end=u32(header+32), rate=u32(header+36);
        if (start>=end || uint64_t(end)+1>=counts[9] || loop_start<start ||
            loop_end> end || loop_start>loop_end || rate<1000 || rate>384000 ||
            (u16(header+44)&~7u)) return false;
    }
    return true;
}
}
// Upstream stores envelope/LFO durations in signed sample counters. Validate
// prepared regions for every playable key at our highest supported rate so a
// malformed generator cannot overflow a counter in the callback.
static bool valid_envelope(const tsf_envelope &env, uint32_t low, uint32_t high) {
    auto duration = [](double seconds) {
        return std::isfinite(seconds) && seconds >= 0 && seconds * 384000.0 < 1073741824.0;
    };
    if (!duration(env.delay) || !duration(env.attack) || !duration(env.release) ||
        !std::isfinite(env.sustain) || env.sustain < 0 || env.sustain > 1) return false;
    for (uint32_t key=low; key<=high; ++key) {
        const double hold = env.keynumToHold ? std::exp2((env.hold + double(env.keynumToHold)*(60-int(key)))/1200) : env.hold;
        const double decay = env.keynumToDecay ? std::exp2((env.decay + double(env.keynumToDecay)*(60-int(key)))/1200) : env.decay;
        if (!duration(hold) || !duration(decay)) return false;
    }
    return true;
}
std::shared_ptr<SoundFontBank> SoundFontBank::load(const void *bytes, uint32_t size) {
    if (!validate(static_cast<const unsigned char *>(bytes),size)) return {};
    auto *font=tsf_load_memory(bytes,static_cast<int>(size));
    if (!font) return {};
    // Generator offsets are applied by the parser. Check their resulting ranges,
    // including the interpolation guard sample, before any rendering occurs.
    const auto *p=static_cast<const unsigned char *>(bytes);
    uint32_t samples=0;
    for (uint64_t pos=12;pos<size;) {
        const auto length=u32(p+pos+4);
        if (!std::memcmp(p+pos,"LIST",4) && !std::memcmp(p+pos+8,"sdta",4)) {
            for (uint64_t child=pos+12;child<pos+8+length;) {
                const auto count=u32(p+child+4);
                if (!std::memcmp(p+child,"smpl",4)) samples=count/2;
                child+=8+count+(count&1);
            }
        }
        pos+=8+length+(length&1);
    }
    bool valid=true;
    for (int i=0;i<font->presetNum && valid;++i) {
        const auto &preset=font->presets[i];
        if (preset.regionNum>4096) { valid=false; break; }
        for (int j=0;j<preset.regionNum;++j) {
            const auto &r=preset.regions[j];
            if (r.offset>=r.end || uint64_t(r.end)+1>=samples ||
                r.sample_rate<1000 ||
                r.sample_rate>384000 || r.lokey>r.hikey || r.hikey>127 ||
                r.lovel>r.hivel || r.hivel>127 ||
                // One-shot regions never use loop addresses. Generator offsets
                // may leave them reversed or outside the playable sample range.
                (r.loop_mode && (r.loop_start<r.offset ||
                    r.loop_start>r.loop_end || r.loop_end>=r.end)) ||
                !valid_envelope(r.ampenv,r.lokey,r.hikey) ||
                !valid_envelope(r.modenv,r.lokey,r.hikey) ||
                !std::isfinite(r.delayModLFO) || r.delayModLFO<0 || r.delayModLFO*384000.0>=1073741824.0 ||
                !std::isfinite(r.delayVibLFO) || r.delayVibLFO<0 || r.delayVibLFO*384000.0>=1073741824.0) { valid=false; break; }
        }
    }
    if (!valid) { tsf_close(font); return {}; }
    auto bank=std::make_shared<SoundFontBank>(); bank->font_=font; return bank;
}
SoundFontBank::~SoundFontBank() { tsf_close(static_cast<tsf *>(font_)); }
uint32_t SoundFontBank::preset_count() const noexcept { return tsf_get_presetcount(static_cast<tsf *>(font_)); }
bool SoundFontBank::preset_info(uint32_t index,uint32_t &bank,uint32_t &program,std::string &name) const {
    const auto *font=static_cast<tsf *>(font_);
    if (index>=static_cast<uint32_t>(font->presetNum)) return false;
    bank=font->presets[index].bank; program=font->presets[index].preset;
    name=font->presets[index].presetName; return true;
}
std::shared_ptr<SoundFontPool> SoundFontPool::prepare(std::shared_ptr<SoundFontBank> bank,
    uint32_t preset,uint32_t voices,uint32_t sample_rate) {
    if (!bank || preset>=bank->preset_count() || !voices || voices>8192) return {};
    std::unique_lock lock(bank->mutex_);
    auto *font=static_cast<tsf *>(bank->font_);
    const auto &definition=font->presets[preset];
    // Count worst-case layered regions for any key/velocity, outside the callback.
    uint32_t layers=1;
    for (int key=0;key<128;++key) {
        std::array<uint32_t,128> overlap{};
        for (int n=0;n<definition.regionNum;++n) {
            const auto &r=definition.regions[n];
            if (key<r.lokey || key>r.hikey) continue;
            for (int v=std::max(0,int(r.lovel));v<=std::min(127,int(r.hivel));++v)
                layers=std::max(layers,++overlap[v]);
        }
    }
    if (layers>64) return {};
    auto pool=std::make_shared<SoundFontPool>(); pool->bank_=bank; pool->preset_=preset;
    pool->slots_.resize(voices);
    for (auto &slot:pool->slots_) {
        auto *copy=tsf_copy(font);
        if (!copy) { lock.unlock(); return {}; } // Close prepared copies outside this lock.
        slot.font=copy;
        tsf_set_output(copy,TSF_STEREO_INTERLEAVED,sample_rate,0);
        if (!tsf_set_max_voices(copy,layers)) { lock.unlock(); return {}; }
    }
    return pool;
}
SoundFontPool::~SoundFontPool() {
    // Upstream shared sample-data refcounts are not atomic.
    // Preparation and destruction of copies use the same bank lock.
    if (!bank_) return;
    std::lock_guard lock(bank_->mutex_);
    for (auto &slot:slots_) if (slot.font) tsf_close(static_cast<tsf *>(slot.font));
}
int SoundFontPool::acquire(uint32_t note,float velocity) noexcept {
    for (uint32_t i=0;i<slots_.size();++i) if (!slots_[i].used) {
        auto &slot=slots_[i]; slot.used=true;
        tsf_note_on(static_cast<tsf *>(slot.font),preset_,note,velocity);
        return static_cast<int>(i);
    }
    return -1;
}
void SoundFontPool::release(int slot) noexcept {
    if (slot<0) return;
    auto &s=slots_[slot]; auto *font=static_cast<tsf *>(s.font);
    // tsf_reset leaves a quick-release tail. Pool recycling requires an immediate
    // stop: this small adapter is confined to the pinned upstream implementation.
    for (int i=0;i<font->voiceNum;++i) tsf_voice_kill(&font->voices[i]);
    s.used=false;
}
void SoundFontPool::note_off(int slot) noexcept {
    if (slot>=0) tsf_note_off_all(static_cast<tsf *>(slots_[slot].font));
}
StereoSample SoundFontPool::process(int slot) noexcept {
    StereoSample result;
    if (slot<0) return result;
    float samples[2]{};
    tsf_render_float(static_cast<tsf *>(slots_[slot].font),samples,1,0);
    result.left=samples[0]; result.right=samples[1]; return result;
}
bool SoundFontPool::active(int slot) const noexcept {
    return slot>=0 && tsf_active_voice_count(static_cast<tsf *>(slots_[slot].font))>0;
}
}
