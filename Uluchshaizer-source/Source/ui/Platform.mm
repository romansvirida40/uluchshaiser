// macOS: honour System Settings > Accessibility > Display > Reduce motion.
#import <AppKit/AppKit.h>

namespace ulz::ui
{
bool systemPrefersReducedMotion()
{
    if (@available (macOS 10.12, *))
        return [[NSWorkspace sharedWorkspace] accessibilityDisplayShouldReduceMotion];
    return false;
}
} // namespace ulz::ui
