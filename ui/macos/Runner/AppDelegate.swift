import Cocoa
import FlutterMacOS

@main
class AppDelegate: FlutterAppDelegate {
  override func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool {
    return true
  }

  override func applicationSupportsSecureRestorableState(_ app: NSApplication) -> Bool {
    return true
  }

  // Do not wait for Flutter's asynchronous exit request: if Dart is busy
  // (e.g. a long plugin load) AppKit cancels the quit. The audio engine is
  // destroyed synchronously in applicationWillTerminate instead.
  override func applicationShouldTerminate(_ sender: NSApplication) -> NSApplication.TerminateReply {
    return .terminateNow
  }

  // Flutter does not always dispose the Dart widget tree when the window is
  // closed. Destroy the audio engine here, before process teardown, so every
  // live VST3 instance (Analog Lab V) is unloaded while the plugin host and
  // JUCE message manager are still alive. The Dart AppLifecycleListener does
  // the same thing; cadence_engine_destroy_active() is idempotent.
  override func applicationWillTerminate(_ notification: Notification) {
    destroyCadenceEngine()
    super.applicationWillTerminate(notification)
  }

  private func destroyCadenceEngine() {
    guard let exe = Bundle.main.executableURL else { return }
    let dylib = exe.deletingLastPathComponent()
      .appendingPathComponent("libcadence_engine.dylib").path
    guard let handle = dlopen(dylib, RTLD_NOW) else { return }
    defer { dlclose(handle) }
    guard let sym = dlsym(handle, "cadence_engine_destroy_active") else { return }
    typealias DestroyActive = @convention(c) () -> Void
    unsafeBitCast(sym, to: DestroyActive.self)()
  }
}
