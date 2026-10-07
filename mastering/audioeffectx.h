/* audioeffectx.h - stand-in for the Steinberg SDK header, so the Airwindows sources compile
 * unchanged. Hand-written, no Steinberg code: the publicly known VST2 ABI plus the few
 * AudioEffectX members Airwindows uses. The real plug-in shell is airwin_shell.h. MIT. */
#ifndef __audioeffect__
#define __audioeffect__
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef int32_t VstInt32;
typedef intptr_t VstIntPtr;
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

enum { kVstMaxProgNameLen = 24, kVstMaxParamStrLen = 8, kVstMaxVendorStrLen = 64,
       kVstMaxProductStrLen = 64, kVstMaxEffectNameLen = 32 };
enum VstPlugCategory { kPlugCategUnknown = 0, kPlugCategEffect, kPlugCategSynth };

inline char *vst_strncpy(char *dst, const char *src, size_t n) { strncpy(dst, src, n); dst[n] = 0; return dst; }

class AudioEffect {
public:
    AudioEffect(audioMasterCallback, VstInt32, VstInt32) {}
    virtual ~AudioEffect() {}
    virtual void processReplacing(float **, float **, VstInt32) = 0;
    virtual void processDoubleReplacing(double **, double **, VstInt32) {}
    virtual float getParameter(VstInt32) { return 0; }
    virtual void setParameter(VstInt32, float) {}
    float getSampleRate() { return sampleRate; }
    void setSampleRate(float sr) { sampleRate = sr; }
    void setNumInputs(VstInt32) {}
    void setNumOutputs(VstInt32) {}
    void setUniqueID(VstInt32) {}
    void canProcessReplacing(bool = true) {}
    void canDoubleReplacing(bool = true) {}
    void programsAreChunks(bool = true) {}
    void float2string(float v, char *t, VstInt32 n) { char b[32]; snprintf(b, sizeof b, "%.4f", v); vst_strncpy(t, b, n); }
private:
    float sampleRate = 44100.0f;
};
typedef AudioEffect AudioEffectX;

AudioEffect *createEffectInstance(audioMasterCallback audioMaster);   /* in the Airwindows .cpp */
#endif
