/*
    EqualizerAPO macOS app launcher
    Copyright (C) 2026

    This file is part of a GPL-2.0 project. See the repository License.txt.
*/

#import <Cocoa/Cocoa.h>

#include "eapo/audio_host.h"

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>

namespace {

std::unique_ptr<eapo::AudioHost> gHost;

std::string toUtf8(NSString* value) {
    return value == nil ? std::string() : std::string([value UTF8String]);
}

NSString* defaultConfigPath() {
    NSString* home = NSHomeDirectory();
    return [home stringByAppendingPathComponent:
                       @"Library/Application Support/EqualizerAPO/mac/config.txt"];
}

void showError(NSString* message) {
    NSAlert* alert = [[NSAlert alloc] init];
    alert.alertStyle = NSAlertStyleWarning;
    alert.messageText = @"EqualizerAPO could not start";
    alert.informativeText = message;
    [alert addButtonWithTitle:@"OK"];
    [alert runModal];
}

void ensureDefaultConfigExists() {
    NSString* configPath = defaultConfigPath();
    NSFileManager* manager = [NSFileManager defaultManager];
    if ([manager fileExistsAtPath:configPath]) {
        return;
    }

    NSString* directory = [configPath stringByDeletingLastPathComponent];
    [manager createDirectoryAtPath:directory
       withIntermediateDirectories:YES
                        attributes:nil
                             error:nil];

    NSString* bundledConfig =
        [[NSBundle mainBundle] pathForResource:@"config.example" ofType:@"txt"];
    if (bundledConfig != nil) {
        [manager copyItemAtPath:bundledConfig toPath:configPath error:nil];
    }
}

}  // namespace

@interface AppDelegate : NSObject <NSApplicationDelegate>
@property(nonatomic, strong) NSWindow* window;
@property(nonatomic, strong) NSTextField* configField;
@property(nonatomic, strong) NSTextField* inputField;
@property(nonatomic, strong) NSTextField* outputField;
@property(nonatomic, strong) NSTextField* statusField;
@property(nonatomic, strong) NSButton* startButton;
@property(nonatomic, strong) NSButton* stopButton;
@end

@implementation AppDelegate

- (void)applicationDidFinishLaunching:(NSNotification*)notification {
    (void)notification;
    ensureDefaultConfigExists();

    self.window = [[NSWindow alloc]
        initWithContentRect:NSMakeRect(0, 0, 680, 360)
                  styleMask:(NSWindowStyleMaskTitled |
                             NSWindowStyleMaskClosable |
                             NSWindowStyleMaskMiniaturizable)
                    backing:NSBackingStoreBuffered
                      defer:NO];
    self.window.title = @"EqualizerAPO for macOS";
    [self.window center];
    self.window.releasedWhenClosed = NO;

    NSView* content = self.window.contentView;

    NSTextField* heading = [NSTextField labelWithString:@"EqualizerAPO for macOS"];
    heading.font = [NSFont boldSystemFontOfSize:24];
    heading.frame = NSMakeRect(32, 296, 616, 32);
    [content addSubview:heading];

    NSTextField* subtitle =
        [NSTextField labelWithString:@"System-wide equalization through a virtual audio device"];
    subtitle.textColor = [NSColor secondaryLabelColor];
    subtitle.frame = NSMakeRect(34, 270, 616, 20);
    [content addSubview:subtitle];

    [self addLabel:@"Configuration" atY:224 toView:content];
    self.configField = [self textFieldWithValue:defaultConfigPath()
                                             atY:217
                                           width:500
                                           toView:content];

    NSButton* browseButton = [NSButton buttonWithTitle:@"Browse…"
                                                  target:self
                                                  action:@selector(browseConfig:)];
    browseButton.frame = NSMakeRect(546, 214, 100, 28);
    [content addSubview:browseButton];

    [self addLabel:@"Input device" atY:174 toView:content];
    self.inputField = [self textFieldWithValue:@"BlackHole 2ch"
                                            atY:167
                                          width:612
                                          toView:content];

    [self addLabel:@"Output device" atY:124 toView:content];
    self.outputField = [self textFieldWithValue:@"MacBook Pro Speakers"
                                             atY:117
                                           width:612
                                           toView:content];

    NSTextField* hint = [NSTextField
        labelWithString:@"Set macOS Sound Output to BlackHole 2ch, then send the processed audio to your speakers."];
    hint.font = [NSFont systemFontOfSize:11];
    hint.textColor = [NSColor secondaryLabelColor];
    hint.frame = NSMakeRect(34, 82, 612, 24);
    [content addSubview:hint];

    self.startButton = [NSButton buttonWithTitle:@"Start Equalizer"
                                            target:self
                                            action:@selector(startEqualizer:)];
    self.startButton.keyEquivalent = @"\r";
    self.startButton.frame = NSMakeRect(34, 36, 150, 32);
    [content addSubview:self.startButton];

    self.stopButton = [NSButton buttonWithTitle:@"Stop"
                                           target:self
                                           action:@selector(stopEqualizer:)];
    self.stopButton.frame = NSMakeRect(194, 36, 100, 32);
    self.stopButton.enabled = NO;
    [content addSubview:self.stopButton];

    self.statusField = [NSTextField labelWithString:@"Ready"];
    self.statusField.frame = NSMakeRect(316, 42, 330, 20);
    self.statusField.textColor = [NSColor secondaryLabelColor];
    [content addSubview:self.statusField];

    [self.window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
}

- (void)addLabel:(NSString*)text atY:(CGFloat)y toView:(NSView*)view {
    NSTextField* label = [NSTextField labelWithString:text];
    label.frame = NSMakeRect(34, y, 150, 20);
    label.font = [NSFont boldSystemFontOfSize:12];
    [view addSubview:label];
}

- (NSTextField*)textFieldWithValue:(NSString*)value
                               atY:(CGFloat)y
                             width:(CGFloat)width
                             toView:(NSView*)view {
    NSTextField* field = [[NSTextField alloc] initWithFrame:NSMakeRect(34, y, width, 28)];
    field.stringValue = value;
    field.placeholderString = @"Device name or file path";
    [view addSubview:field];
    return field;
}

- (void)browseConfig:(id)sender {
    (void)sender;
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    panel.canChooseFiles = YES;
    panel.canChooseDirectories = NO;
    panel.allowsMultipleSelection = NO;
    panel.allowedFileTypes = @[@"txt", @"conf", @"config"];
    [panel beginSheetModalForWindow:self.window
                  completionHandler:^(NSModalResponse response) {
                      if (response == NSModalResponseOK && panel.URL.path != nil) {
                          self.configField.stringValue = panel.URL.path;
                      }
                  }];
}

- (void)startEqualizer:(id)sender {
    (void)sender;
    if (gHost != nullptr && gHost->running()) {
        return;
    }

    gHost = std::make_unique<eapo::AudioHost>();
    std::string error;
    if (!gHost->start(toUtf8(self.configField.stringValue),
                      toUtf8(self.inputField.stringValue),
                      toUtf8(self.outputField.stringValue),
                      error)) {
        gHost.reset();
        showError([NSString stringWithUTF8String:error.c_str()]);
        self.statusField.stringValue = @"Could not start";
        return;
    }

    self.startButton.enabled = NO;
    self.stopButton.enabled = YES;
    self.statusField.stringValue = @"Running — audio is being processed";
    self.statusField.textColor = [NSColor systemGreenColor];
}

- (void)stopEqualizer:(id)sender {
    (void)sender;
    if (gHost != nullptr) {
        gHost->stop();
        gHost.reset();
    }
    self.startButton.enabled = YES;
    self.stopButton.enabled = NO;
    self.statusField.stringValue = @"Stopped";
    self.statusField.textColor = [NSColor secondaryLabelColor];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender {
    (void)sender;
    return YES;
}

- (void)applicationWillTerminate:(NSNotification*)notification {
    (void)notification;
    if (gHost != nullptr) {
        gHost->stop();
        gHost.reset();
    }
}

@end

int main() {
    @autoreleasepool {
        NSApplication* application = [NSApplication sharedApplication];
        application.activationPolicy = NSApplicationActivationPolicyRegular;
        AppDelegate* delegate = [[AppDelegate alloc] init];
        application.delegate = delegate;
        [application run];
    }
    return EXIT_SUCCESS;
}