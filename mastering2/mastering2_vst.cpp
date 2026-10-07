/* mastering2_vst.cpp - Airwindows Mastering2 as a VST2 insert effect for the MPC OS plugin host.
 * The DSP is Airwindows' own Mastering2.cpp / Mastering2Proc.cpp, unchanged (MIT, Chris Johnson);
 * the plug-in around it is airwin_shell.h. */
static const char *const AW_KEYS[] = {"sidepass", "glue", "scope", "skronk", "girth", "drive", "dither"};
#define AW_CHUNK "AWM2;"
#define AW_ID "mastering2"
#define AW_PRESET_ENV "MASTERING2_PRESETS"
#define AW_TITLE "Mastering 2"
#include "airwin_shell.h"
