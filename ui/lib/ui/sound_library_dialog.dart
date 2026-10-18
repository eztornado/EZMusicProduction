// Biblioteca de sonidos gratuitos (estilo Mixcraft): catálogo indexado por
// categorías, descarga con progreso a ~/Music/EZMusicProduction/Library y
// "Añadir" = clip de audio en la pista seleccionada al playhead.
// Catálogo: assets/sound_catalog.json — sonidos de Wikimedia Commons e
// Internet Archive bajo licencias libres (cada descarga guarda su fichero de
// atribución).
import 'dart:async';
import 'dart:convert';

import 'package:flutter/material.dart';
import 'package:flutter/services.dart' show rootBundle;

import '../engine/engine_service.dart';
import '../l10n/l10n.dart';
import 'app_alert.dart';

class _CatalogEntry {
  const _CatalogEntry({
    required this.id,
    required this.name,
    required this.category,
    required this.url,
    required this.license,
    required this.author,
    required this.size,
    required this.type,
    required this.bpm,
    required this.duration,
  });

  factory _CatalogEntry.fromJson(Map<String, dynamic> m) => _CatalogEntry(
    id: m['id'] as String,
    name: m['name'] as String,
    category: m['category'] as String,
    url: m['url'] as String,
    license: m['license'] as String? ?? 'CC',
    author: m['author'] as String? ?? '',
    size: (m['size'] as num?)?.toInt() ?? 0,
    type: m['type'] as String? ?? 'oneshot',
    bpm: (m['bpm'] as num?)?.toInt(),
    duration: (m['duration'] as num?)?.toDouble() ?? 0,
  );

  final String id, name, category, url, license, author;
  final int size;

  /// "loop" (base/loop musical) or "oneshot" (golpe suelto). Newer catalog
  /// entries carry it explicitly; older ones default to "oneshot".
  final String type;
  final int? bpm;
  final double duration;

  bool get isLoop => type == 'loop';

  String get fileName {
    // Keep the real extension so the engine can pick the right decoder
    // (sources ship .wav, .ogg, .mp3 and .flac files).
    final path = Uri.parse(url).path;
    final dot = path.lastIndexOf('.');
    final ext = dot >= 0 ? path.substring(dot + 1).toLowerCase() : 'wav';
    final safe = name.replaceAll(RegExp(r'[^\w\s-]'), '').trim();
    final base = safe.replaceAll(RegExp(r'\s+'), '_');
    return '$base.$ext';
  }

  String get sizeText {
    // Some sources (e.g. bulk GitHub indexes) do not report a file size.
    if (size <= 0) return '';
    return size > 1024 * 1024
        ? '${(size / 1048576).toStringAsFixed(1)} MB'
        : '${(size / 1024).round()} KB';
  }

  String get durationText {
    if (duration <= 0) return '';
    if (duration < 60) {
      return '${duration.toStringAsFixed(duration < 10 ? 1 : 0)}s';
    }
    final m = duration ~/ 60;
    final s = (duration % 60).round();
    return '${m}m${s.toString().padLeft(2, '0')}s';
  }
}

Future<void> showSoundLibraryDialog(
  BuildContext context,
  EngineService service,
) {
  return showDialog(
    context: context,
    builder: (_) => Dialog(
      backgroundColor: const Color(0xFF16171B),
      shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(8)),
      child: _SoundLibraryDialog(service: service),
    ),
  );
}

class _SoundLibraryDialog extends StatefulWidget {
  const _SoundLibraryDialog({required this.service});

  final EngineService service;

  @override
  State<_SoundLibraryDialog> createState() => _SoundLibraryDialogState();
}

class _SoundLibraryDialogState extends State<_SoundLibraryDialog> {
  List<_CatalogEntry> catalog = [];
  String category = 'Todas';
  String typeFilter = 'todo'; // todo | loop | oneshot
  String query = '';
  Map<String, dynamic> libState = {};
  final Map<String, double> progress = {};
  final Map<String, String> errors = {};

  /// Id of the library sound being auditioned, if any.
  String? playingId;
  Timer? _previewTimer;

  @override
  void initState() {
    super.initState();
    _load();
  }

  @override
  void dispose() {
    _previewTimer?.cancel();
    widget.service.stopLibraryPreview();
    super.dispose();
  }

  Future<void> _load() async {
    final raw = await rootBundle.loadString('assets/sound_catalog.json');
    final list = (jsonDecode(raw) as List)
        .map((m) => _CatalogEntry.fromJson(m as Map<String, dynamic>))
        .toList();
    if (!mounted) return;
    setState(() {
      catalog = list;
      libState = widget.service.libraryState();
    });
  }

  /// Categories present in the catalog, most populated first ("Todas" on top).
  List<String> get categories {
    final counts = categoryCounts;
    final rest = counts.keys.toList()
      ..sort((a, b) => counts[b]!.compareTo(counts[a]!));
    return <String>['Todas', ...rest];
  }

  /// Number of catalog entries per category (used for the sidebar counters).
  Map<String, int> get categoryCounts {
    final counts = <String, int>{};
    for (final e in catalog) {
      counts[e.category] = (counts[e.category] ?? 0) + 1;
    }
    return counts;
  }

  List<_CatalogEntry> get filtered {
    final q = query.trim().toLowerCase();
    return catalog
        .where(
          (e) =>
              (category == 'Todas' || e.category == category) &&
              (typeFilter == 'todo' || e.type == typeFilter) &&
              (q.isEmpty || e.name.toLowerCase().contains(q)),
        )
        .toList();
  }

  int get loopCount => catalog.where((e) => e.isLoop).length;
  int get oneshotCount => catalog.length - loopCount;

  bool isDownloaded(_CatalogEntry e) => libState[e.id] != null;
  String pathOf(_CatalogEntry e) =>
      (libState[e.id] as Map<String, dynamic>?)?['path'] as String? ?? '';

  Future<void> _download(_CatalogEntry e) async {
    setState(() => progress[e.id] = 0);
    final err = await widget.service.downloadLibrarySound(
      url: e.url,
      category: e.category,
      fileName: e.fileName,
      attribution: <String>[
        e.name,
        context.l10n.soundLibAttributionAuthor(e.author),
        context.l10n.soundLibAttributionLicense(e.license),
        context.l10n.soundLibLicenseFile(
            e.url, DateTime.now().toIso8601String().split('T').first),
      ].join('\n'),
      onProgress: (p) => setState(() => progress[e.id] = p),
    );
    if (!mounted) return;
    if (err != null) {
      setState(() {
        errors[e.id] = err;
        progress.remove(e.id);
      });
      // La fila guarda el icono de error; el aviso explica el motivo.
      showAppError(context, context.l10n.soundLibDownloadFailed(e.name, err));
      return;
    }
    setState(() {
      libState[e.id] = {
        'path': '${widget.service.libraryDir()}/${e.category}/${e.fileName}',
        'name': e.name,
      };
      progress.remove(e.id);
    });
    widget.service.saveLibraryState(libState);
  }

  /// Plays/stops an already-downloaded [e] on the engine's preview channel.
  /// The engine ends it at EOF, so poll to flip the icon back.
  void _togglePreview(_CatalogEntry e) {
    if (playingId == e.id) {
      _previewTimer?.cancel();
      widget.service.stopLibraryPreview();
      setState(() => playingId = null);
      return;
    }
    final err = widget.service.previewLibrarySound(pathOf(e));
    if (err != null) {
      showAppError(context, err);
      return;
    }
    _previewTimer?.cancel();
    setState(() => playingId = e.id);
    _previewTimer = Timer.periodic(const Duration(milliseconds: 400), (t) {
      if (!mounted || !widget.service.isPreviewingLibrarySound()) {
        t.cancel();
        if (mounted && playingId != null) {
          setState(() => playingId = null);
        }
      }
    });
  }

  void _addToProject(_CatalogEntry e) {
    final pageContext = context;
    final err = widget.service.addLibraryClip(pathOf(e));
    if (err != null) {
      showAppError(pageContext, err);
      return;
    }
    ScaffoldMessenger.maybeOf(pageContext)?.showSnackBar(
      SnackBar(
        content: Text(context.l10n.soundLibAddedToTrack(e.name)),
      ),
    );
    if (Navigator.canPop(pageContext)) {
      Navigator.pop(pageContext);
    }
  }

  @override
  Widget build(BuildContext context) {
    final l10n = context.l10n;
    final list = filtered;
    return SizedBox(
      width: 880,
      height: 580,
      child: Padding(
        padding: const EdgeInsets.all(14),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            Row(
              children: [
                Text(
                  l10n.soundLibTitle,
                  style: const TextStyle(
                    fontSize: 16,
                    fontWeight: FontWeight.w600,
                    color: Color(0xFFE8EAEE),
                  ),
                ),
                const Spacer(),
                SizedBox(
                  width: 220,
                  height: 34,
                  child: TextField(
                    style: const TextStyle(fontSize: 13),
                    decoration: InputDecoration(
                      hintText: l10n.soundLibSearchHint,
                      isDense: true,
                      contentPadding: const EdgeInsets.symmetric(
                        horizontal: 10,
                        vertical: 8,
                      ),
                    ),
                    onChanged: (v) => setState(() => query = v),
                  ),
                ),
                IconButton(
                  onPressed: () => Navigator.pop(context),
                  icon: const Icon(Icons.close, color: Color(0xFF9AA2B1)),
                ),
              ],
            ),
            const SizedBox(height: 8),
            Row(
              children: [
                _typeChip('todo', l10n.soundLibAllTypes, catalog.length),
                const SizedBox(width: 6),
                _typeChip('loop', l10n.soundLibLoops, loopCount),
                const SizedBox(width: 6),
                _typeChip('oneshot', l10n.soundLibOneShots, oneshotCount),
              ],
            ),
            const SizedBox(height: 8),
            Expanded(
              child: Row(
                crossAxisAlignment: CrossAxisAlignment.stretch,
                children: [
                  SizedBox(
                    width: 190,
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.stretch,
                      children: [
                        Padding(
                          padding: const EdgeInsets.only(left: 6, bottom: 6),
                          child: Text(
                            l10n.soundLibCategories,
                            style: const TextStyle(
                              fontSize: 11,
                              fontWeight: FontWeight.w600,
                              letterSpacing: .4,
                              color: Color(0xFF5C6270),
                            ),
                          ),
                        ),
                        Expanded(
                          child: ListView(
                            padding: EdgeInsets.zero,
                            children: [
                              for (final c in categories)
                                _categoryTile(
                                  c,
                                  c == 'Todas'
                                      ? catalog.length
                                      : categoryCounts[c] ?? 0,
                                ),
                            ],
                          ),
                        ),
                      ],
                    ),
                  ),
                  const VerticalDivider(width: 10, color: Color(0xFF32363F)),
                  Expanded(
                    child: list.isEmpty
                        ? Center(
                            child: Text(
                              l10n.soundLibEmpty,
                              style: const TextStyle(color: Color(0xFF5C6270)),
                            ),
                          )
                        : ListView.builder(
                            itemCount: list.length,
                            itemBuilder: (_, i) => _row(list[i]),
                          ),
                  ),
                ],
              ),
            ),
            const SizedBox(height: 6),
            Text(
              l10n.soundLibFooter,
              style: const TextStyle(fontSize: 10, color: Color(0xFF5C6270)),
            ),
          ],
        ),
      ),
    );
  }

  /// One row of the category sidebar (name + count). The sidebar scrolls, so
  /// the list keeps working as the catalog grows past the modal height.
  Widget _categoryTile(String c, int count) {
    final selected = c == category;
    // 'Todas' es el centinela interno del filtro: se muestra traducido.
    final label = c == 'Todas' ? context.l10n.soundLibCategoryAll : c;
    return Padding(
      padding: const EdgeInsets.only(bottom: 2),
      child: TextButton(
        style: TextButton.styleFrom(
          alignment: Alignment.centerLeft,
          padding: const EdgeInsets.symmetric(horizontal: 8),
          minimumSize: const Size(0, 28),
          tapTargetSize: MaterialTapTargetSize.shrinkWrap,
          foregroundColor: selected
              ? const Color(0xFF4D9DF7)
              : const Color(0xFF9AA2B1),
        ),
        onPressed: () => setState(() => category = c),
        child: Row(
          children: [
            Expanded(
              child: Text(
                label,
                maxLines: 1,
                overflow: TextOverflow.ellipsis,
                style: TextStyle(
                  fontSize: 13,
                  fontWeight: selected ? FontWeight.w600 : FontWeight.normal,
                ),
              ),
            ),
            Text(
              '$count',
              style: const TextStyle(fontSize: 10, color: Color(0xFF5C6270)),
            ),
          ],
        ),
      ),
    );
  }

  Widget _typeChip(String key, String label, int count) {
    final selected = typeFilter == key;
    return InkWell(
      borderRadius: BorderRadius.circular(14),
      onTap: () => setState(() => typeFilter = key),
      child: Container(
        padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 5),
        decoration: BoxDecoration(
          color: selected
              ? const Color(0xFF4D9DF7).withValues(alpha: 0.20)
              : const Color(0xFF1B1D22),
          borderRadius: BorderRadius.circular(14),
          border: Border.all(
            color: selected ? const Color(0xFF4D9DF7) : const Color(0xFF32363F),
          ),
        ),
        child: Text(
          '$label · $count',
          style: TextStyle(
            fontSize: 11,
            fontWeight: FontWeight.w600,
            color: selected ? const Color(0xFFE8EAEE) : const Color(0xFF9AA2B1),
          ),
        ),
      ),
    );
  }

  /// Small inline tag (BPM, duration, type) shown next to a sound's name.
  Widget _badge(String text, Color color) => Container(
    margin: const EdgeInsets.only(right: 4),
    padding: const EdgeInsets.symmetric(horizontal: 5, vertical: 1),
    decoration: BoxDecoration(
      color: color.withValues(alpha: 0.16),
      borderRadius: BorderRadius.circular(4),
    ),
    child: Text(
      text,
      style: TextStyle(fontSize: 9, fontWeight: FontWeight.w600, color: color),
    ),
  );

  Widget _row(_CatalogEntry e) {
    final l10n = context.l10n;
    final downloaded = isDownloaded(e);
    final downloading = progress.containsKey(e.id);
    final typeColor = e.isLoop
        ? const Color(0xFF4D9DF7)
        : const Color(0xFFF5A524);
    return Padding(
      padding: const EdgeInsets.only(bottom: 4),
      child: Tooltip(
        // Trazabilidad: la fila muestra la licencia; el tooltip, la fuente.
        message: l10n.soundLibSourceTooltip(e.url, e.license) +
            (e.author.isNotEmpty
                ? '\n${l10n.soundLibAttributionAuthor(e.author)}'
                : ''),
        waitDuration: const Duration(milliseconds: 400),
        child: Container(
          padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 6),
          decoration: BoxDecoration(
            color: const Color(0xFF1B1D22),
            borderRadius: BorderRadius.circular(5),
          ),
          child: Row(
            children: [
              Icon(
                e.isLoop ? Icons.repeat : Icons.graphic_eq,
                size: 18,
                color: downloaded ? const Color(0xFF7FE8C8) : typeColor,
              ),
              const SizedBox(width: 10),
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(
                      e.name,
                      maxLines: 1,
                      overflow: TextOverflow.ellipsis,
                      style: const TextStyle(
                        fontSize: 13,
                        color: Color(0xFFE8EAEE),
                      ),
                    ),
                    const SizedBox(height: 2),
                    Row(
                      children: [
                        if (e.isLoop)
                          _badge(l10n.soundLibLoop, typeColor)
                        else
                          _badge(l10n.soundLibOneShot, typeColor),
                        if (e.bpm != null)
                          _badge('${e.bpm} BPM', const Color(0xFF3DD68C)),
                        if (e.durationText.isNotEmpty)
                          _badge(e.durationText, const Color(0xFF9AA2B1)),
                        Flexible(
                          child: Text(
                            '${e.category} · ${e.license}'
                            '${e.sizeText.isNotEmpty ? ' · ${e.sizeText}' : ''}'
                            '${e.author.isNotEmpty ? ' · ${e.author}' : ''}',
                            maxLines: 1,
                            overflow: TextOverflow.ellipsis,
                            style: const TextStyle(
                              fontSize: 10,
                              color: Color(0xFF5C6270),
                            ),
                          ),
                        ),
                      ],
                    ),
                  ],
                ),
              ),
              const SizedBox(width: 10),
              if (downloading)
                SizedBox(
                  width: 120,
                  child: LinearProgressIndicator(value: progress[e.id]),
                )
              else if (errors.containsKey(e.id))
                Tooltip(
                  message: errors[e.id],
                  child: const Icon(
                    Icons.error_outline,
                    color: Color(0xFFE86A6A),
                    size: 18,
                  ),
                )
              else if (downloaded) ...[
                Text(
                  l10n.soundLibInLibrary,
                  style: TextStyle(fontSize: 11, color: Colors.teal[200]),
                ),
                const SizedBox(width: 4),
                IconButton(
                  tooltip: playingId == e.id
                      ? l10n.soundLibStop
                      : l10n.soundLibListen,
                  onPressed: () => _togglePreview(e),
                  icon: Icon(
                    playingId == e.id
                        ? Icons.stop_circle
                        : Icons.play_circle_outline,
                    size: 20,
                    color: const Color(0xFF7FE8C8),
                  ),
                ),
                const SizedBox(width: 4),
                TextButton.icon(
                  onPressed: () => _addToProject(e),
                  icon: const Icon(Icons.playlist_add, size: 16),
                  label: Text(l10n.soundLibAdd),
                ),
              ] else
                TextButton.icon(
                  onPressed: () => _download(e),
                  icon: const Icon(Icons.download, size: 16),
                  label: Text(l10n.soundLibDownload),
                ),
            ],
          ),
        ),
      ),
    );
  }
}
