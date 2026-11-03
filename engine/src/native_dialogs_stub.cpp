// Non-macOS stand-in for native_dialogs.mm — there is no native picker yet,
// so this returns "" which the header contract already defines as "cancelled"
// (Engine::pickOpenFile callers treat it like a dismissed dialog).
// Wire a GTK/zenity (Linux) or IFileOpenDialog (Windows) backend behind the
// same signature when the unified "open" flow ships there.
#include "native_dialogs.h"

namespace native_dialogs {

juce::String pickOpenFile(const juce::String& /*title*/,
                          const juce::String& /*startDir*/,
                          const juce::String& /*extensions*/)
{
    return {};
}

} // namespace native_dialogs
