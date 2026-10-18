// Native macOS open panel (NSOpenPanel) for the unified "open" flow.
// Separated into an Objective-C++ file because JUCE 8 removed the
// synchronous FileChooser API, and the Flutter main thread — which IS the
// JUCE message thread — can run the panel's own modal loop directly.
#pragma once

#include <juce_core/juce_core.h>

namespace native_dialogs {

// Runs a modal NSOpenPanel. Blocks until the user picks or cancels.
// extensions: comma-separated list WITHOUT dots ("ezmpp,mid,cpr").
// Returns the chosen absolute path, or "" when cancelled.
juce::String pickOpenFile(const juce::String& title,
                          const juce::String& startDir,
                          const juce::String& extensions);

} // namespace native_dialogs
