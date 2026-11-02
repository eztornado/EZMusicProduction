// Ventana de consola del modo debug.
//
// Se abre en una ventana propia (desktop_multi_window) y sigue el fichero
// debug.log en tiempo real. Como cada ventana es un proceso distinto, el
// fichero es el canal compartido entre la ventana principal y esta consola.

import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import '../debug/debug_log.dart';
import '../l10n/l10n.dart';
import 'design.dart';

class DebugConsoleWindow extends StatefulWidget {
  const DebugConsoleWindow({super.key});

  @override
  State<DebugConsoleWindow> createState() => _DebugConsoleWindowState();
}

class _DebugConsoleWindowState extends State<DebugConsoleWindow> {
  final ScrollController _scroll = ScrollController();
  Timer? _timer;
  String _text = '';
  int _size = -1;
  bool _paused = false;
  final bool _autoScroll = true;

  @override
  void initState() {
    super.initState();
    _refresh(force: true);
    _timer = Timer.periodic(
      const Duration(milliseconds: 200),
      (_) => _refresh(),
    );
  }

  @override
  void dispose() {
    _timer?.cancel();
    _scroll.dispose();
    super.dispose();
  }

  void _refresh({bool force = false}) {
    if (_paused && !force) return;
    final size = DebugLog.size;
    if (!force && size == _size) return;
    final text = DebugLog.readText();
    if (!mounted) return;
    setState(() {
      _text = text;
      _size = size;
    });
    if (_autoScroll) {
      WidgetsBinding.instance.addPostFrameCallback((_) {
        if (!mounted || !_scroll.hasClients) return;
        _scroll.jumpTo(_scroll.position.maxScrollExtent);
      });
    }
  }

  Future<void> _copy() async {
    await Clipboard.setData(ClipboardData(text: _text));
  }

  void _clear() {
    DebugLog.clear();
    setState(() {
      _text = '';
      _size = 0;
    });
  }

  @override
  Widget build(BuildContext context) {
    final l = context.l10n;
    return Scaffold(
      backgroundColor: AppColors.bgDeep,
      body: Column(
        children: [
          Container(
            height: 44,
            padding: const EdgeInsets.symmetric(horizontal: 12),
            decoration: const BoxDecoration(
              color: AppColors.bgPanel,
              border: Border(bottom: BorderSide(color: AppColors.border)),
            ),
            child: Row(
              children: [
                const Icon(
                  Icons.terminal_rounded,
                  size: 16,
                  color: AppColors.accent,
                ),
                const SizedBox(width: 8),
                Text(
                  l.debugConsoleTitle,
                  style: const TextStyle(
                    fontSize: 12,
                    fontWeight: FontWeight.w600,
                    color: AppColors.text,
                  ),
                ),
                const SizedBox(width: 10),
                Container(
                  padding: const EdgeInsets.symmetric(
                    horizontal: 6,
                    vertical: 2,
                  ),
                  decoration: BoxDecoration(
                    color:
                        _paused
                            ? AppColors.surfaceAlt
                            : AppColors.success.withValues(alpha: 0.15),
                    borderRadius: BorderRadius.circular(4),
                  ),
                  child: Text(
                    _paused ? l.debugConsolePaused : l.debugConsoleLive,
                    style: TextStyle(
                      fontSize: 9,
                      fontWeight: FontWeight.w600,
                      color:
                          _paused ? AppColors.textDim : AppColors.success,
                    ),
                  ),
                ),
                const Spacer(),
                IconButton(
                  tooltip:
                      _paused
                          ? l.debugConsoleResume
                          : l.debugConsolePause,
                  onPressed: () {
                    setState(() => _paused = !_paused);
                    if (!_paused) _refresh(force: true);
                  },
                  icon: Icon(
                    _paused
                        ? Icons.play_arrow_rounded
                        : Icons.pause_rounded,
                    size: 17,
                  ),
                  color: AppColors.textDim,
                  visualDensity: VisualDensity.compact,
                ),
                IconButton(
                  tooltip: l.debugConsoleClear,
                  onPressed: _clear,
                  icon: const Icon(Icons.delete_sweep_outlined, size: 17),
                  color: AppColors.textDim,
                  visualDensity: VisualDensity.compact,
                ),
                IconButton(
                  tooltip: l.debugConsoleCopy,
                  onPressed: _copy,
                  icon: const Icon(Icons.copy_all_outlined, size: 17),
                  color: AppColors.textDim,
                  visualDensity: VisualDensity.compact,
                ),
              ],
            ),
          ),
          Expanded(
            child: Container(
              width: double.infinity,
              color: AppColors.bgDeep,
              child:
                  _text.isEmpty
                      ? Center(
                        child: Padding(
                          padding: const EdgeInsets.all(24),
                          child: Text(
                            l.debugConsoleEmpty,
                            textAlign: TextAlign.center,
                            style: const TextStyle(
                              fontSize: 11,
                              color: AppColors.textFaint,
                            ),
                          ),
                        ),
                      )
                      : Scrollbar(
                        controller: _scroll,
                        child: SingleChildScrollView(
                          controller: _scroll,
                          padding: const EdgeInsets.all(12),
                          child: SelectableText(
                            _text,
                            style: const TextStyle(
                              fontFamily: 'Menlo',
                              fontFamilyFallback: ['monospace'],
                              fontSize: 10.5,
                              height: 1.35,
                              color: AppColors.textDim,
                            ),
                          ),
                        ),
                      ),
            ),
          ),
          Container(
            height: 24,
            padding: const EdgeInsets.symmetric(horizontal: 12),
            decoration: const BoxDecoration(
              color: AppColors.bgPanel,
              border: Border(top: BorderSide(color: AppColors.border)),
            ),
            child: Row(
              children: [
                Expanded(
                  child: Text(
                    DebugLog.file.path,
                    overflow: TextOverflow.ellipsis,
                    style: const TextStyle(
                      fontSize: 9,
                      color: AppColors.textFaint,
                    ),
                  ),
                ),
                Text(
                  '${(_size / 1024).toStringAsFixed(1)} KB',
                  style: const TextStyle(
                    fontSize: 9,
                    color: AppColors.textFaint,
                  ),
                ),
              ],
            ),
          ),
        ],
      ),
    );
  }
}
