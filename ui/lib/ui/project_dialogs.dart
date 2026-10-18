// Project dialogs (§5 toolbar): Guardar-como (name input) and Abrir
// (list of .ezmpp files under ~/Music/Cadence/Projects). Native file
// pickers would need CocoaPods plugins, which this machine avoids.
// The editor is EZMusicProduction: project files are .ezmpp.

import 'dart:io';

import 'package:flutter/material.dart';


import 'package:cadence_ui/generated/version.dart';

import '../engine/engine_service.dart';
import '../l10n/l10n.dart';
import 'app_alert.dart';

const String kProjectsDir = '/Users/ivan/Music/EZMusicProduction/Projects';

Future<void> showSaveAsDialog(BuildContext context, EngineService service) {
  final controller = TextEditingController(
      text: service.lastProjectPath != null
          ? File(service.lastProjectPath!)
              .uri
              .pathSegments
              .last
              .replaceAll('.ezmpp', '')
          : context.l10n.projectDefaultName);
  return showDialog<void>(
    context: context,
    builder: (context) => AlertDialog(
      backgroundColor: const Color(0xFF252830),
      title: Text(context.l10n.projectSaveAs,
          style: const TextStyle(fontSize: 15)),
      content: TextField(
        controller: controller,
        autofocus: true,
        decoration: InputDecoration(
            hintText: context.l10n.projectNameLabel, counterText: ''),
        maxLength: 80,
      ),
      actions: [
        TextButton(
            onPressed: () => Navigator.pop(context),
            child: Text(context.l10n.projectCancel)),
        TextButton(
          onPressed: () {
            final error = service.saveProjectAs(controller.text);
            Navigator.pop(context);
            if (error != null) showAppError(context, error);
          },
          child: Text(context.l10n.projectSave),
        ),
      ],
    ),
  );
}

/// Lists the saved projects (newest first) and opens the picked one.
Future<void> showOpenDialog(BuildContext context, EngineService service) {
  // UNIFIED open: the NATIVE file explorer (NSOpenPanel) starting in the
  // projects folder — it navigates the whole machine and the extension
  // decides what happens (.ezmpp project, .mid/.cpr import, .wav audio).
  final error = service.pickAndOpenFile();
  if (error != null) showAppError(context, error);
  return Future.value();
}


/// Diálogo "Acerca de" de escritorio: nombre, versión dinámica
/// (YY.MM.DD.N, generada en cada build) y la web, abrible en el navegador
/// con `open` de macOS (sin plugins: Process.run).
Future<void> showAboutAppDialog(BuildContext context) {
  return showDialog<void>(
    context: context,
    builder: (context) => AlertDialog(
      backgroundColor: const Color(0xFF252830),
      title: Text(
        context.l10n.projectAppName,
        style: const TextStyle(fontSize: 17, fontWeight: FontWeight.w700),
      ),
      content: Column(
        mainAxisSize: MainAxisSize.min,
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text(context.l10n.projectVersion(kAppVersion),
              style: const TextStyle(
                  fontSize: 12, color: Color(0xFFC7CBD4))),
          const SizedBox(height: 10),
          Text(context.l10n.projectAuthors,
              style: const TextStyle(fontSize: 12, color: Color(0xFF9BA1AE))),
          const SizedBox(height: 10),
          InkWell(
            onTap: () {
              Process.run('open', ['https://www.ezmusicproduction.com']);
            },
            child: Text(
              context.l10n.projectWeb,
              style: const TextStyle(
                  fontSize: 12,
                  color: Color(0xFF4D9DF7),
                  decoration: TextDecoration.underline),
            ),
          ),
        ],
      ),
      actions: [
        TextButton(
            onPressed: () => Navigator.pop(context),
            child: Text(context.l10n.projectClose)),
      ],
    ),
  );
}
