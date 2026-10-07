/* mastering_vst.cpp - Airwindows Mastering as a VST2 insert effect for the MPC OS plugin host.
 * The DSP is Airwindows' own Mastering.cpp / MasteringProc.cpp, unchanged (MIT, Chris Johnson);
 * the plug-in around it is airwin_shell.h. */
static const char *const AW_KEYS[] = {"glue", "scope", "skronk", "girth", "drive", "dither"};
#define AW_CHUNK "AWM1;"
#define AW_ID "mastering"
#define AW_PRESET_ENV "MASTERING_PRESETS"
#define AW_TITLE "Mastering"
#include "airwin_shell.h"
