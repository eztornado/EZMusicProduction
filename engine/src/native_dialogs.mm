// NSOpenPanel bridge — see native_dialogs.h.
#import <AppKit/AppKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include "native_dialogs.h"

namespace native_dialogs {
namespace {

// juce::nsString lives in JUCE's ObjC helper header, which a bare .mm
// doesn't pull in — two tiny converters instead.
NSString* toNs(const juce::String& s)
{
    return [NSString stringWithUTF8String:s.toRawUTF8()];
}
juce::String fromNs(NSString* s)
{
    return juce::String::fromUTF8(s.UTF8String);
}

} // namespace

juce::String pickOpenFile(const juce::String& title,
                          const juce::String& startDir,
                          const juce::String& extensions)
{
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    [panel setCanChooseFiles:YES];
    [panel setCanChooseDirectories:NO];
    [panel setAllowsMultipleSelection:NO];
    [panel setTitle:toNs(title)];

    NSURL* dirURL = [NSURL fileURLWithPath:toNs(startDir)];
    if (dirURL != nullptr)
        [panel setDirectoryURL:dirURL];

    // NOTE: setAllowedFileTypes is IGNORED/partially applied on macOS 12+
    // open panels (types resolve through UTType, and unregistered
    // extensions like Cubase's .cpr get dropped — the file greys out).
    // We declare public.data EXPLICITLY (every file selectable) through
    // the modern API Ventura respects; the app dispatches by extension.
    if (@available(macOS 11, *))
        [panel setAllowedContentTypes:
                   @[[UTType typeWithIdentifier:@"public.data"]]];
    (void) extensions;

    if ([panel runModal] != NSModalResponseOK)
        return {};
    if (panel.URLs.count == 0)
        return {};
    return fromNs(panel.URLs[0].path);
}

} // namespace native_dialogs
