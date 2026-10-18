import Cocoa
import FlutterMacOS

class MainFlutterWindow: NSWindow {
  override func awakeFromNib() {
    let flutterViewController = FlutterViewController()
    // Open filling the visible screen (menu bar excluded); still a normal,
    // minimizable/resizable window. ui-design.md §3 D2.
    let windowFrame = NSScreen.main?.visibleFrame ?? self.frame
    self.contentViewController = flutterViewController
    self.setFrame(windowFrame, display: true)
    self.minSize = NSSize(width: 1000, height: 640)

    RegisterGeneratedPlugins(registry: flutterViewController)

    // Dynamic window title from Dart: 'project - EZMusicProduction'.
    let channel = FlutterMethodChannel(
        name: "com.ezmusicproduction/window",
        binaryMessenger: flutterViewController.engine.binaryMessenger)
    channel.setMethodCallHandler { (call, result) in
      if call.method == "setWindowTitle" {
        self.title = (call.arguments as? String) ?? "EZMusicProduction"
        result(nil)
      } else {
        result(FlutterMethodNotImplemented)
      }
    }

    super.awakeFromNib()
  }
}
