/* Offline x86 test (test.sh builds it with ASan/UBSan): parameter list, start values, a sine
 * through the effect at the start setting (level kept, no NaN), every control turned fully up
 * and down stays finite and bounded, both channels processed, chunk restore, the 32 preset slots
 * (SAVE/LOAD from the skin, program list, bank file read back by a fresh process state).
 * With "bench" as the second argument it only measures CPU load. Prints PASSED/FAILED. */
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <cstdlib>
#include <dlfcn.h>
#include <unistd.h>

struct AEffect;
typedef intptr_t (*audioMasterCallback)(AEffect *, int32_t, int32_t, intptr_t, void *, float);
struct AEffect {
    int32_t magic;
    intptr_t (*dispatcher)(AEffect *, int32_t, int32_t, intptr_t, void *, float);
    void (*process)(AEffect *, float **, float **, int32_t);
    void (*setParameter)(AEffect *, int32_t, float);
    float (*getParameter)(AEffect *, int32_t);
    int32_t numPrograms, numParams, numInputs, numOutputs, flags;
    intptr_t resvd1, resvd2;
    int32_t initialDelay, realQualities, offQualities;
    float ioRatio;
    void *object, *user;
    int32_t uniqueID, version;
    void (*processReplacing)(AEffect *, float **, float **, int32_t);
    void (*processDoubleReplacing)(AEffect *, double **, double **, int32_t);
    char future[56];
};
static intptr_t master(AEffect *, int32_t, int32_t, intptr_t, void *, float) { return 0; }
#define STR2(x) #x
#define STR(x) STR2(x)
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; std::printf("FAIL: " __VA_ARGS__); std::printf("\n"); } } while (0)
static const float SR = 44100;
static const int BS = 256;
static bool bad = false;

static std::string text(AEffect *e, int op, int i) { char b[64] = {0}; e->dispatcher(e, op, i, 0, b, 0); return b; }
/* a two-tone signal through the effect; returns the peak of each channel */
static void run(AEffect *e, double sec, double amp, double &pkL, double &pkR) {
    std::vector<float> il(BS), ir(BS), ol(BS), orr(BS);
    float *in[2] = {il.data(), ir.data()}, *out[2] = {ol.data(), orr.data()};
    static double ph = 0;
    pkL = pkR = 0;
    for (int b = 0; b < (int)(sec * SR / BS); b++) {
        for (int i = 0; i < BS; i++) {
            ph += 1;
            il[i] = (float)(amp * (0.6 * std::sin(ph * 2 * M_PI * 60 / SR) + 0.4 * std::sin(ph * 2 * M_PI * 3000 / SR)));
            ir[i] = (float)(amp * (0.6 * std::sin(ph * 2 * M_PI * 90 / SR) + 0.4 * std::sin(ph * 2 * M_PI * 5000 / SR)));
        }
        e->processReplacing(e, in, out, BS);
        for (int i = 0; i < BS; i++) {
            if (!std::isfinite(ol[i]) || !std::isfinite(orr[i])) bad = true;
            pkL = std::max(pkL, (double)std::fabs(ol[i])); pkR = std::max(pkR, (double)std::fabs(orr[i]));
        }
    }
}

int main(int argc, char **argv) {
    char bank[] = "/tmp/aw_presets_XXXXXX";
    { int fd = mkstemp(bank); if (fd >= 0) close(fd); }
    setenv(STR(PRESET_ENV), bank, 1);
    void *h = dlopen(argc > 1 ? argv[1] : "./plugin.so", RTLD_NOW | RTLD_LOCAL);
    if (!h) { std::printf("FAILED: dlopen %s\n", dlerror()); return 1; }
    auto mainf = (AEffect * (*)(audioMasterCallback)) dlsym(h, "VSTPluginMain");
    if (!mainf) { std::printf("FAILED: no VSTPluginMain\n"); return 1; }
    AEffect *e = mainf(master);
    e->dispatcher(e, 0, 0, 0, 0, 0);
    e->dispatcher(e, 10, 0, 0, 0, SR);
    e->dispatcher(e, 11, 0, BS, 0, 0);
    e->dispatcher(e, 12, 0, 1, 0, 0);
    double pl, pr;

    if (argc > 2 && !std::strcmp(argv[2], "bench")) {
        auto t0 = std::chrono::steady_clock::now();
        run(e, 30, 0.5, pl, pr);
        double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::printf("bench: 30 s audio in %.3f s = %.2f %% of one x86 core\n", s, s / 30 * 100);
        return 0;
    }

    CHECK(e->magic == 0x56737450, "magic");
    CHECK(e->numInputs == 2 && e->numOutputs == 2, "2 in / 2 out");
    CHECK(e->numPrograms == 32, "32 programs");
    CHECK(e->numParams >= EXPECT_PARAMS + 3, "parameter count %d", e->numParams);
    int dither = -1, glue = -1, slot = -1, load = -1, save = -1;
    for (int i = 0; i < e->numParams; i++) {
        std::string n = text(e, 8, i);
        std::printf("  %d %-10s %s\n", i, n.c_str(), text(e, 7, i).c_str());
        if (n == "Dither" && dither < 0) dither = i;
        if (n == "Glue" && glue < 0) glue = i;
        if (n == "Preset") slot = i;
        if (n == "Load") load = i;
        if (n == "Save") save = i;
    }
    CHECK(dither >= 0 && glue >= 0 && slot >= 0 && load >= 0 && save >= 0, "Glue, Dither, Preset, Load, Save present");
    if (dither < 0 || glue < 0 || slot < 0 || load < 0 || save < 0) { std::printf("FAILED\n"); return 1; }
    CHECK(text(e, 7, glue) == "0", "Glue starts at 0");

    run(e, 1.0, 0.25, pl, pr);
    CHECK(pl > 0.1 && pl < 0.6 && pr > 0.1 && pr < 0.6, "start setting keeps the level (%.3f / %.3f)", pl, pr);

    for (int i = 0; i < e->numParams; i++) {
        if (i >= EXPECT_PARAMS - 1) continue;            /* Dither, bank and popup flags belong to the host UI */
        for (float v : {1.0f, 0.0f, 0.5f}) {
            e->setParameter(e, i, v);
            run(e, 0.5, 0.9, pl, pr);
            CHECK(pl < 4.0 && pr < 4.0, "%s=%.1f output bounded (%.2f / %.2f)", text(e, 8, i).c_str(), v, pl, pr);
            CHECK(pl > 0.001 && pr > 0.001, "%s=%.1f both channels alive", text(e, 8, i).c_str(), v);
        }
    }

    /* presets: save to slot 5, change, load back; empty slot leaves the setting; slot 1 = INIT */
    e->setParameter(e, slot, 4.0f / 31);
    CHECK(text(e, 7, slot) == "05 (empty)", "slot 5 empty, is %s", text(e, 7, slot).c_str());
    e->setParameter(e, glue, 0.6f);
    e->setParameter(e, save, 1.0f); e->setParameter(e, save, 0.0f);
    CHECK(text(e, 7, slot) != "05 (empty)", "slot 5 saved");
    e->setParameter(e, glue, 0.2f);
    e->setParameter(e, load, 1.0f); e->setParameter(e, load, 0.0f);
    CHECK(std::fabs(e->getParameter(e, glue) - 0.6f) < 0.001f, "LOAD restores Glue (%.3f)", e->getParameter(e, glue));
    e->setParameter(e, glue, 0.3f);
    e->dispatcher(e, 2, 0, 9, 0, 0);                    /* program 10: empty */
    CHECK(std::fabs(e->getParameter(e, glue) - 0.3f) < 0.001f, "empty slot leaves the setting");
    e->dispatcher(e, 2, 0, 0, 0, 0);                    /* program 1: INIT */
    CHECK(text(e, 29, 0) == "INIT", "program 1 is INIT");
    CHECK(e->getParameter(e, glue) < 0.001f, "INIT resets Glue");
    e->dispatcher(e, 2, 0, 4, 0, 0);
    CHECK(std::fabs(e->getParameter(e, glue) - 0.6f) < 0.001f, "program 5 loads the saved slot");
    { FILE *f = std::fopen(bank, "r"); char l[512] = {0}; if (f) { if (!std::fgets(l, sizeof l, f)) l[0] = 0; std::fclose(f); }
      CHECK(!std::strncmp(l, "5\t", 2) && std::strstr(l, "glue=60000;"), "bank file holds slot 5"); }
    run(e, 0.2, 0.25, pl, pr);

    e->setParameter(e, glue, 0.8f);
    void *c = nullptr;
    intptr_t n = e->dispatcher(e, 23, 0, 0, &c, 0);
    std::string saved((const char *)c, (size_t)n);
    AEffect *e2 = mainf(master);
    CHECK(e2->dispatcher(e2, 24, 0, (intptr_t)saved.size(), (void *)saved.data(), 0) == 1, "chunk accepted");
    CHECK(std::fabs(e2->getParameter(e2, glue) - 0.8f) < 0.001f, "Glue restored");
    CHECK(e2->dispatcher(e2, 24, 0, 4, (void *)"junk", 0) == 0, "foreign chunk refused");
    run(e2, 0.2, 0.25, pl, pr);
    e2->dispatcher(e2, 1, 0, 0, 0, 0);
    e->dispatcher(e, 1, 0, 0, 0, 0);
    unlink(bank);
    CHECK(!bad, "no NaN/inf in the output");
    std::printf(fails ? "FAILED (%d)\n" : "PASSED\n", fails);
    return fails ? 1 : 0;
}
