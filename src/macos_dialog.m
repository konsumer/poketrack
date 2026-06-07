#import <AppKit/AppKit.h>
#include <string.h>

void macos_open_directory_dialog(const char *title, char *out, size_t out_size) {
    out[0] = '\0';
    [NSApp activateIgnoringOtherApps:YES];
    NSOpenPanel *panel = [NSOpenPanel openPanel];
    panel.canChooseFiles = YES;
    panel.canChooseDirectories = YES;
    panel.allowsMultipleSelection = NO;
    panel.level = NSModalPanelWindowLevel;
    if (title) panel.message = [NSString stringWithUTF8String:title];
    if ([panel runModal] == NSModalResponseOK) {
        const char *path = panel.URL.path.UTF8String;
        strncpy(out, path, out_size - 1);
        out[out_size - 1] = '\0';
    }
}
