/* =============================================================================
 * airwin_shell.h - the VST2 plug-in around an unmodified Airwindows effect, for the MPC OS plugin
 * host (Force, MPC Live/One/X/Key), armhf. Hand-written like mpc-vst-vfilter's vfilter_vst.cpp
 * (no wrapper, no SDK). The including .cpp defines
 *     AW_KEYS[]   module.json keys in the Airwindows parameter order (kParamA, kParamB, ...)
 *     AW_CHUNK    five-character chunk header, e.g. "AWM1;"
 *     AW_ID       plugin id: "<id>.so" is looked up to find the SD card folder, the presets go
 *                 to "<id>_presets.txt", the offline test points AW_PRESET_ENV somewhere else
 *     AW_TITLE    name for unnamed saved slots
 * 32 preset slots with LOAD/SAVE in the pattern of mpc-vst-rattler (module.json keys slot, load,
 * save): shared by every instance and project, also the plugin's VST programs.
 * Knobs hand their full position to Airwindows (the 0..100 the host shows is only the readout);
 * option parameters send option/(count-1), which lands in the middle of Airwindows' own steps.
 * MIT license (see ../LICENSE).
 * ========================================================================== */
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "params.h"
#include "popup.h"    /* mpc-vst-plugins wrapper/popup.h, copied into build/ by build.sh */
#include "audioeffectx.h"

enum {
    effOpen = 0, effClose = 1, effSetProgram = 2, effGetProgram = 3, effSetProgramName = 4,
    effGetProgramName = 5, effGetProgramNameIndexed = 29, effGetParamLabel = 6, effGetParamDisplay = 7, effGetParamName = 8,
    effSetSampleRate = 10, effSetBlockSize = 11, effMainsChanged = 12, effGetChunk = 23,
    effSetChunk = 24, effCanBeAutomated = 26, effGetPlugCategory = 35,
    effGetEffectName = 45, effGetVendorString = 47, effGetProductString = 48,
    effGetVendorVersion = 49, effCanDo = 51, effGetVstVersion = 58,
};
enum { audioMasterAutomate = 0, audioMasterUpdateDisplay = 42 };
enum { effFlagsCanReplacing = 1 << 4, effFlagsProgramChunks = 1 << 5 };

struct Plugin {
    AEffect fx;
    audioMasterCallback master = nullptr;
    std::atomic<float> cache[NPARAMS];
    std::atomic<int> notify[NPARAMS];
    float open[NPARAMS] = {0};
    volatile int release[NPARAMS] = {0};
    std::atomic<bool> dirty{true};
    AudioEffect *aw = nullptr;
    int awIndex[NPARAMS];             /* our parameter -> Airwindows parameter, -1 = none */
    float init[NPARAMS];              /* Airwindows' own start values */
    bool down[NPARAMS] = {false};     /* LOAD/SAVE: the host currently reports them pressed */
    std::atomic<int> cur{0};          /* selected preset slot, 0-based */
    std::vector<uint8_t> chunk;
    ~Plugin() { delete aw; }
};

static int param_index(const char *key) {
    for (int i = 0; i < NPARAMS; i++) if (!std::strcmp(PARAMS[i].key, key)) return i;
    return -1;
}
static int IDX_SLOT = -1, IDX_LOAD = -1, IDX_SAVE = -1;
/* slot, load and save belong to the preset bank: never part of a chunk or a preset */
static bool is_bank_key(int i) { return i >= 0 && (i == IDX_SLOT || i == IDX_LOAD || i == IDX_SAVE); }
static float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static void copy_str(void *dst, const char *s, size_t max) {
    std::strncpy((char *)dst, s, max - 1);
    ((char *)dst)[max - 1] = 0;
}
static int norm_to_ui(const param_t *p, float n) {
    if (p->nopts) return (int)std::lround(clamp01(n) * (p->nopts - 1));
    return (int)std::lround(p->min + (p->max - p->min) * clamp01(n));
}

static void configure(Plugin *w) {
    for (int i = 0; i < NPARAMS; i++) {
        if (w->awIndex[i] < 0) continue;
        const param_t *p = &PARAMS[i];
        float n = clamp01(w->cache[i].load());
        if (p->nopts > 1) n = (float)norm_to_ui(p, n) / (p->nopts - 1);
        w->aw->setParameter(w->awIndex[i], n);
    }
}

static void processReplacing(AEffect *e, float **in, float **out, int32_t n) {
    Plugin *w = (Plugin *)e->object;
    if (w->dirty.exchange(false)) configure(w);
    if (n > 0) w->aw->processReplacing(in, out, n);
    bool any = false;
    for (int i = 0; i < NPARAMS; i++) {
        if (w->release[i]) { w->release[i] = 0; any = true; w->master(&w->fx, audioMasterAutomate, i, 0, 0, 0.0f); }
        if (!w->notify[i].exchange(0)) continue;
        any = true;
        w->master(&w->fx, audioMasterAutomate, i, 0, 0, w->cache[i].load());
    }
    if (any) w->master(&w->fx, audioMasterUpdateDisplay, 0, 0, 0, 0.0f);
}

/* ---- state as text: "key=value;" for every sound parameter, used for the project chunk and
 * for the preset slots alike, restored by key. The value is the knob's full position (x 100000,
 * as an integer: no decimal point, so no locale trouble), not the rounded readout. ---------- */
static std::string build_state(Plugin *w) {
    std::string t;
    char buf[64];
    for (int i = 0; i < NPARAMS; i++) {
        if (is_bank_key(i)) continue;
        std::snprintf(buf, sizeof buf, "%s=%ld;", PARAMS[i].key, std::lround(w->cache[i].load() * 100000.0f));
        t += buf;
    }
    return t;
}
static void apply_state(Plugin *w, const std::string &t, bool with_slot) {
    for (size_t pos = 0; pos < t.size();) {
        size_t semi = t.find(';', pos);
        if (semi == std::string::npos) break;
        std::string kv = t.substr(pos, semi - pos);
        pos = semi + 1;
        size_t eq = kv.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = kv.substr(0, eq);
        if (key == "prog") {   /* which slot a project had selected; never loads the slot */
            const int v = std::atoi(kv.c_str() + eq + 1);
            if (with_slot && v >= 0 && v < 32) w->cur.store(v);
            continue;
        }
        int i = param_index(key.c_str());
        if (i < 0 || is_bank_key(i)) continue;
        w->cache[i].store(clamp01(std::atol(kv.c_str() + eq + 1) / 100000.0f));
        w->notify[i].store(1);
    }
    w->dirty.store(true);
}

/* ---------------------------------------------------------------------------
 * Preset bank (pattern of mpc-vst-rattler / mpc-vst-acid): NSLOTS slots shared by every instance
 * and every project, kept in one text file on the SD card (one line per saved slot:
 * "index<TAB>name<TAB>state"). The slots are also the plugin's VST programs, so the host's
 * PRESET list shows and selects them; the PRESET knob + LOAD/SAVE buttons on the PRESET tab do
 * the same from the skin. Slot 1 is INIT (Airwindows' start values) until SAVE overwrites it.
 * ------------------------------------------------------------------------- */
#define NSLOTS 32
static std::mutex g_bank_lock;
static bool g_bank_loaded;
static std::string g_slot_chunk[NSLOTS], g_slot_name[NSLOTS], g_bank_path;

/* Next to the plugin's own folder rather than inside it, so reinstalling the plugin folder
 * doesn't take the presets with it; inside it if the parent can't be written. No dladdr:
 * /proc/self/maps has the path. AW_PRESET_ENV overrides the place (offline test). */
static std::string so_dir() {
    std::string dir;
    if (FILE *f = std::fopen("/proc/self/maps", "r")) {
        char line[1024];
        while (std::fgets(line, sizeof line, f)) {
            char *p = std::strstr(line, "/" AW_ID ".so");
            char *first = std::strchr(line, '/');
            if (!p || !first || first > p) continue;
            dir.assign(first, (size_t)(p - first));
            break;
        }
        std::fclose(f);
    }
    return dir;
}
static void bank_load_locked() {
    if (g_bank_loaded) return;
    g_bank_loaded = true;
    g_bank_path.clear();
    if (const char *env = std::getenv(AW_PRESET_ENV)) g_bank_path = env;
    else {
        std::string dir = so_dir(), cand[2];
        if (dir.empty()) dir = "/tmp";
        size_t cut = dir.rfind('/');
        cand[0] = (cut != std::string::npos && cut > 0 ? dir.substr(0, cut) : dir) + "/" AW_ID "_presets.txt";
        cand[1] = dir + "/" AW_ID "_presets.txt";
        for (const std::string &c : cand)             /* an existing file wins ... */
            if (FILE *f = std::fopen(c.c_str(), "r")) { std::fclose(f); g_bank_path = c; break; }
        for (int i = 0; i < 2 && g_bank_path.empty(); i++)   /* ... else the first place we may write */
            if (FILE *f = std::fopen(cand[i].c_str(), "a")) { std::fclose(f); g_bank_path = cand[i]; }
    }
    if (g_bank_path.empty()) return;
    if (FILE *f = std::fopen(g_bank_path.c_str(), "r")) {
        static char line[4096];
        while (std::fgets(line, sizeof line, f)) {
            line[std::strcspn(line, "\r\n")] = 0;
            char *t1 = std::strchr(line, '\t');
            char *t2 = t1 ? std::strchr(t1 + 1, '\t') : nullptr;
            int idx = std::atoi(line);
            if (!t2 || idx < 1 || idx > NSLOTS) continue;
            *t1 = *t2 = 0;
            g_slot_name[idx - 1] = t1 + 1;
            g_slot_chunk[idx - 1] = t2 + 1;
        }
        std::fclose(f);
    }
}
static bool bank_write_locked() {   /* whole file, via a temp file so a power cut can't leave half a bank */
    if (g_bank_path.empty()) return false;
    std::string tmp = g_bank_path + ".tmp";
    FILE *f = std::fopen(tmp.c_str(), "w");
    if (!f) return false;
    for (int i = 0; i < NSLOTS; i++)
        if (!g_slot_chunk[i].empty())
            std::fprintf(f, "%d\t%s\t%s\n", i + 1, g_slot_name[i].c_str(), g_slot_chunk[i].c_str());
    return std::fclose(f) == 0 && std::rename(tmp.c_str(), g_bank_path.c_str()) == 0;
}
static std::string slot_title(int i) {
    std::lock_guard<std::mutex> lk(g_bank_lock);
    char buf[32];
    if (g_slot_chunk[i].empty()) {
        if (i == 0) return "INIT";
        std::snprintf(buf, sizeof buf, "%02d (empty)", i + 1);
        return buf;
    }
    if (!g_slot_name[i].empty()) return g_slot_name[i];
    std::snprintf(buf, sizeof buf, AW_TITLE " %02d", i + 1);
    return buf;
}
static void start_values(Plugin *w) {
    for (int i = 0; i < NPARAMS; i++) { if (is_bank_key(i)) continue; w->cache[i].store(w->init[i]); w->notify[i].store(1); }
    w->dirty.store(true);
}
static void slot_load(Plugin *w) {
    const int n = clampi(w->cur.load(), 0, NSLOTS - 1);
    std::string c;
    { std::lock_guard<std::mutex> lk(g_bank_lock); c = g_slot_chunk[n]; }
    if (c.empty() && n != 0) return;    /* an empty slot leaves the current setting alone */
    start_values(w);
    apply_state(w, c, false);
}
static bool slot_save(Plugin *w) {
    const int n = clampi(w->cur.load(), 0, NSLOTS - 1);
    const std::string c = build_state(w);
    std::lock_guard<std::mutex> lk(g_bank_lock);
    g_slot_chunk[n] = c;
    return bank_write_locked();
}

/* project chunk: AW_CHUNK + the state + "prog=<slot>;" */
static intptr_t get_chunk(Plugin *w, void **ptr) {
    std::string t = AW_CHUNK + build_state(w);
    char buf[24];
    std::snprintf(buf, sizeof buf, "prog=%d;", w->cur.load());
    t += buf;
    w->chunk.assign(t.begin(), t.end());
    *ptr = w->chunk.data();
    return (intptr_t)w->chunk.size();
}
static intptr_t set_chunk(Plugin *w, const void *data, intptr_t len) {
    if (!data || len < 5) return 0;
    std::string t((const char *)data, (size_t)len);
    if (t.compare(0, 5, AW_CHUNK)) return 0;
    apply_state(w, t.substr(5), true);
    return 1;
}

static void setParameter(AEffect *e, int32_t i, float n) {
    Plugin *w = (Plugin *)e->object;
    if (i < 0 || i >= NPARAMS) return;
    const param_t *p = &PARAMS[i];
    if (popup_set(w->open, i, n)) return;
    if (i == IDX_SLOT) {   /* browsing only: LOAD loads, so turning the knob can't wipe the setting */
        const int v = (int)std::lround(clamp01(n) * (NSLOTS - 1));
        if (v != w->cur.load()) { w->cur.store(v); w->master(&w->fx, audioMasterUpdateDisplay, 0, 0, 0, 0.0f); }
        return;
    }
    if (i == IDX_LOAD || i == IDX_SAVE) {
        const bool down = n > 0.5f;
        const bool rising = down && !w->down[i];   /* an echo of our own automate must not fire again */
        w->down[i] = down;
        if (rising) {
            if (i == IDX_LOAD) slot_load(w); else slot_save(w);
            w->release[i] = 1;
            w->master(&w->fx, audioMasterUpdateDisplay, 0, 0, 0, 0.0f);
        }
        return;
    }
    bool nudge = false;
    if (p->nopts > 1) {
        float pos = clamp01(n) * (p->nopts - 1);
        if (std::fabs(pos - std::round(pos)) > 0.001f) {
            float cur = w->cache[i].load() * (p->nopts - 1);
            n = (float)clampi((int)std::lround(cur) + (pos > cur ? 1 : -1), 0, p->nopts - 1) / (p->nopts - 1);
            nudge = true;
        }
    }
    w->cache[i].store(clamp01(n));
    w->dirty.store(true);
    if (!nudge) popup_picked(w->open, w->release, i);
}
static float getParameter(AEffect *e, int32_t i) {
    Plugin *w = (Plugin *)e->object;
    if (i < 0 || i >= NPARAMS) return 0.0f;
    if (i == IDX_SLOT) return (float)w->cur.load() / (NSLOTS - 1);
    if (i == IDX_LOAD || i == IDX_SAVE) return 0.0f;   /* triggers always read released */
    if (popup_is(i)) return w->open[i];
    return w->cache[i].load();
}

static intptr_t dispatcher(AEffect *e, int32_t op, int32_t idx, intptr_t v, void *p, float o) {
    Plugin *w = (Plugin *)e->object;
    switch (op) {
    case effOpen: return 1;
    case effClose: delete w; return 1;
    case effSetProgram:
        if (v >= 0 && v < NSLOTS) {
            w->cur.store((int)v);
            slot_load(w);
            w->master(&w->fx, audioMasterUpdateDisplay, 0, 0, 0, 0.0f);
        }
        return 0;
    case effGetProgram: return w->cur.load();
    case effGetProgramName: if (p) copy_str(p, slot_title(clampi(w->cur.load(), 0, NSLOTS - 1)).c_str(), 24); return 0;
    case effGetProgramNameIndexed:
        if (idx < 0 || idx >= NSLOTS || !p) return 0;
        copy_str(p, slot_title(idx).c_str(), 24);
        return 1;
    case effSetProgramName: {   /* only a saved slot has a line in the file to carry the name */
        const int n = clampi(w->cur.load(), 0, NSLOTS - 1);
        std::lock_guard<std::mutex> lk(g_bank_lock);
        if (!p || g_slot_chunk[n].empty()) return 0;
        std::string nm((const char *)p);
        for (char &c : nm) if (c == '\t' || c == '\n' || c == '\r') c = ' ';
        g_slot_name[n] = nm.substr(0, 23);
        bank_write_locked();
        return 0;
    }
    case effGetPlugCategory: return 1;   /* kPlugCategEffect */
    case effGetEffectName:
    case effGetProductString: copy_str(p, PLUG_NAME, 32); return 1;
    case effGetVendorString: copy_str(p, PLUG_VENDOR, 32); return 1;
    case effGetVendorVersion: return PLUG_VERSION;
    case effGetVstVersion: return 2400;
    case effCanBeAutomated: return idx >= 0 && idx < NPARAMS && !is_bank_key(idx);
    case effGetParamName: if (idx >= 0 && idx < NPARAMS) copy_str(p, PARAMS[idx].name, 32); return 1;
    case effGetParamLabel: if (idx >= 0 && idx < NPARAMS) copy_str(p, PARAMS[idx].unit, 8); return 1;
    case effGetParamDisplay: {
        if (idx < 0 || idx >= NPARAMS) return 0;
        const param_t *pp = &PARAMS[idx];
        if (idx == IDX_SLOT) { copy_str(p, slot_title(clampi(w->cur.load(), 0, NSLOTS - 1)).c_str(), 24); return 1; }
        if (idx == IDX_LOAD || idx == IDX_SAVE) { *(char *)p = 0; return 1; }
        const float n = popup_is(idx) ? w->open[idx] : w->cache[idx].load();
        const int u = norm_to_ui(pp, n);
        char buf[32];
        if (pp->nopts) std::snprintf(buf, sizeof buf, "%s", pp->opts[u]);
        else std::snprintf(buf, sizeof buf, "%d", u);
        copy_str(p, buf, 24);
        return 1;
    }
    case effSetSampleRate: if (o > 0) w->aw->setSampleRate(o); return 1;
    case effSetBlockSize: case effMainsChanged: return 1;
    case effCanDo: return -1;
    case effGetChunk: return get_chunk(w, (void **)p);
    case effSetChunk: return set_chunk(w, p, v);
    default: return 0;
    }
}

extern "C" __attribute__((visibility("default"))) AEffect *VSTPluginMain(audioMasterCallback master) {
    static std::once_flag once;
    std::call_once(once, [] { IDX_SLOT = param_index("slot"); IDX_LOAD = param_index("load"); IDX_SAVE = param_index("save"); });
    { std::lock_guard<std::mutex> lk(g_bank_lock); bank_load_locked(); }
    Plugin *w = new Plugin();
    w->master = master;
    w->aw = createEffectInstance(master);
    const int nAw = (int)(sizeof AW_KEYS / sizeof AW_KEYS[0]);
    for (int i = 0; i < NPARAMS; i++) {
        w->awIndex[i] = -1;
        for (int k = 0; k < nAw; k++) if (!std::strcmp(PARAMS[i].key, AW_KEYS[k])) w->awIndex[i] = k;
        /* start values are Airwindows' own; option parameters snap to their option */
        float n = w->awIndex[i] >= 0 ? clamp01(w->aw->getParameter(w->awIndex[i])) : PARAMS[i].def;
        if (PARAMS[i].nopts > 1) n = (float)norm_to_ui(&PARAMS[i], n) / (PARAMS[i].nopts - 1);
        w->init[i] = n;
        w->cache[i].store(n);
        w->notify[i].store(1);
    }
    AEffect *e = &w->fx;
    std::memset(e, 0, sizeof *e);
    e->magic = 0x56737450;
    e->dispatcher = dispatcher;
    e->setParameter = setParameter;
    e->getParameter = getParameter;
    e->processReplacing = processReplacing;
    e->numParams = NPARAMS;
    e->numPrograms = NSLOTS;
    e->numInputs = 2;
    e->numOutputs = 2;
    e->flags = effFlagsCanReplacing | effFlagsProgramChunks;
    e->uniqueID = PLUG_UID;
    e->version = PLUG_VERSION;
    e->object = w;
    return e;
}
