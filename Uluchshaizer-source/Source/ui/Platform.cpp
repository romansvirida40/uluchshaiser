// OS "reduce motion" detection (Windows / Linux; macOS lives in Platform.mm), kept in its own translation unit so <windows.h> never leaks
// into JUCE headers.
#include <juce_core/juce_core.h>

#if JUCE_WINDOWS
 #define WIN32_LEAN_AND_MEAN
 #include <windows.h>
#endif

namespace ulz::ui
{
bool systemPrefersReducedMotion()
{
   #if JUCE_WINDOWS
    BOOL animationsEnabled = TRUE;
    if (SystemParametersInfoW (SPI_GETCLIENTAREAANIMATION, 0, &animationsEnabled, 0))
        return animationsEnabled == FALSE;
   #endif
    // Linux has no standard setting; allow forcing it via an environment variable.
    return juce::SystemStats::getEnvironmentVariable ("ULUCHSHAIZER_REDUCED_MOTION", "0") == "1";
}
} // namespace ulz::ui
