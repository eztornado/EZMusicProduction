#include <flutter/dart_project.h>
#include <flutter/flutter_view_controller.h>
#include <windows.h>

#include <filesystem>

#include "flutter_window.h"
#include "utils.h"

namespace {

// Releases the audio engine before process teardown — Windows twin of the
// dlsym("cadence_engine_destroy_active") call in the macOS AppDelegate.
// The DLL is resolved by name next to the exe, so the runner keeps no hard
// link to the engine (same fallback chain as cadence_bindings.dart).
void DestroyCadenceEngine() {
  wchar_t exe_path[MAX_PATH];
  if (!::GetModuleFileNameW(nullptr, exe_path, MAX_PATH)) {
    return;
  }
  const std::filesystem::path dll =
      std::filesystem::path(exe_path).parent_path() / L"cadence_engine.dll";
  HMODULE engine = ::LoadLibraryW(dll.c_str());
  if (!engine) {
    return;
  }
  auto destroy = reinterpret_cast<void (*)()>(
      ::GetProcAddress(engine, "cadence_engine_destroy_active"));
  if (destroy) {
    destroy();
  }
  ::FreeLibrary(engine);
}

}  // namespace

int APIENTRY wWinMain(_In_ HINSTANCE instance, _In_opt_ HINSTANCE prev,
                      _In_ wchar_t *command_line, _In_ int show_command) {
  // Attach to console when present (e.g., 'flutter run') or create a
  // new console when running with a debugger.
  if (!::AttachConsole(ATTACH_PARENT_PROCESS) && ::IsDebuggerPresent()) {
    CreateAndAttachConsole();
  }

  // Initialize COM, so that it is available for use in the library and/or
  // plugins.
  ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

  flutter::DartProject project(L"data");

  std::vector<std::string> command_line_arguments =
      GetCommandLineArguments();

  project.set_dart_entrypoint_arguments(std::move(command_line_arguments));

  FlutterWindow window(project);
  Win32Window::Point origin(10, 10);
  Win32Window::Size size(1280, 720);
  if (!window.Create(L"cadence_ui", origin, size)) {
    return EXIT_FAILURE;
  }
  window.SetQuitOnClose(true);

  ::MSG msg;
  while (::GetMessage(&msg, nullptr, 0, 0)) {
    ::TranslateMessage(&msg);
    ::DispatchMessage(&msg);
  }

  DestroyCadenceEngine();

  ::CoUninitialize();
  return EXIT_SUCCESS;
}
