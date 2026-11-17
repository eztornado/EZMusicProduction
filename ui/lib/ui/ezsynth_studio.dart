// EZSynth Studio — the dedicated Flutter front-end for the EZSynth 2.0
// engine. It talks to the engine exclusively through the generic plug-in
// parameter C ABI, so every gesture is also automatable and every value
// survives project save/load.
//
// Visual language: Cadence's dark surfaces with four functional accents:
// blue = audio path, violet = modulation, amber = macros, green = envelopes.
// The layout is deliberately closer to a hardware performance synth than to
// a generic parameter grid: layer rail, macro rail, full-width pages and a
// permanent performance strip.

import 'dart:math' as math;

import 'package:flutter/material.dart';

import '../engine/engine_service.dart';
import 'design.dart';

const _audio = Color(0xFF4D9DF7);
const _mod = Color(0xFF9B8CFF);
const _macro = Color(0xFFF5A524);
const _env = Color(0xFF3DD68C);
const _fx = Color(0xFF22C7D6);
const _ink = Color(0xFFF0F2F6);

Future<void> showEzSynthStudio(
  BuildContext context,
  EngineService service, {
  required int trackId,
  int slotKind = 1,
  int slotIndex = 0,
  String title = 'EZSynth',
}) {
  return showDialog<void>(
    context: context,
    barrierDismissible: true,
    barrierColor: Colors.black.withValues(alpha: 0.72),
    builder: (context) {
      final media = MediaQuery.sizeOf(context);
      return Dialog(
        backgroundColor: AppColors.bgDeep,
        insetPadding: const EdgeInsets.all(12),
        clipBehavior: Clip.antiAlias,
        shape: RoundedRectangleBorder(
          borderRadius: BorderRadius.circular(14),
          side: const BorderSide(color: AppColors.borderStrong),
        ),
        child: SizedBox(
          width: math.min(1320, media.width - 24),
          height: math.min(900, media.height - 24),
          child: EzSynthStudio(
            service: service,
            trackId: trackId,
            slotKind: slotKind,
            slotIndex: slotIndex,
            title: title,
          ),
        ),
      );
    },
  );
}

class EzParam {
  EzParam({
    required this.index,
    required this.id,
    required this.name,
    required this.group,
    required this.value,
    required this.text,
    required this.flags,
    required this.steps,
    required this.defaultValue,
    required this.labels,
  });

  final int index;
  final String id;
  final String name;
  final String group;
  double value;
  String text;
  final int flags;
  final int steps;
  final double defaultValue;
  final List<String> labels;

  bool get isBoolean => (flags & 1) != 0;
  bool get isChoice => (flags & 2) != 0;
  bool get isInteger => (flags & 4) != 0;
  bool get isContinuous => !isBoolean && !isChoice && !isInteger;

  String get shortName {
    final parts = name.split(' · ');
    return parts.length > 1 ? parts.last : name;
  }

  int get choiceIndex {
    if (steps <= 1) return 0;
    return (value * (steps - 1)).round().clamp(0, steps - 1);
  }

  String get choiceLabel {
    if (labels.isEmpty) return text;
    return labels[choiceIndex.clamp(0, labels.length - 1)];
  }
}

class _ProgramInfo {
  const _ProgramInfo(this.index, this.name);
  final int index;
  final String name;

  String get category {
    final i = name.indexOf(' · ');
    return i > 0 ? name.substring(0, i) : 'Init';
  }

  String get shortName {
    final i = name.indexOf(' · ');
    return i > 0 ? name.substring(i + 3) : name;
  }
}

class EzSynthStudio extends StatefulWidget {
  const EzSynthStudio({
    super.key,
    required this.service,
    required this.trackId,
    required this.slotKind,
    required this.slotIndex,
    required this.title,
  });

  final EngineService service;
  final int trackId;
  final int slotKind;
  final int slotIndex;
  final String title;

  @override
  State<EzSynthStudio> createState() => _EzSynthStudioState();
}

class _EzSynthStudioState extends State<EzSynthStudio>
    with SingleTickerProviderStateMixin {
  final List<EzParam> _all = [];
  final Map<String, EzParam> _byId = {};
  final List<_ProgramInfo> _programs = [];
  bool _loading = true;
  String _page = 'overview';
  String _layer = 'a';
  int _program = 0;
  late final AnimationController _pageAnim;

  EngineService get _service => widget.service;

  @override
  void initState() {
    super.initState();
    _pageAnim = AnimationController(
      vsync: this,
      duration: const Duration(milliseconds: 220),
      value: 1,
    );
    _load();
  }

  @override
  void dispose() {
    _pageAnim.dispose();
    super.dispose();
  }

  void _load() {
    final params = <EzParam>[];
    final map = <String, EzParam>{};
    final count = _service.pluginParamCount(
      widget.trackId,
      widget.slotKind,
      widget.slotIndex,
    );
    for (var i = 0; i < count; i++) {
      final name = _service.pluginParamName(
        widget.trackId,
        widget.slotKind,
        widget.slotIndex,
        i,
      );
      if (name.trim().isEmpty) continue;
      final id = _service.pluginParamId(
        widget.trackId,
        widget.slotKind,
        widget.slotIndex,
        i,
      );
      final group = _service.pluginParamGroup(
        widget.trackId,
        widget.slotKind,
        widget.slotIndex,
        i,
      );
      final flags = _service.pluginParamFlags(
        widget.trackId,
        widget.slotKind,
        widget.slotIndex,
        i,
      );
      final steps = _service.pluginParamNumSteps(
        widget.trackId,
        widget.slotKind,
        widget.slotIndex,
        i,
      );
      final value = _service.pluginParamValue(
        widget.trackId,
        widget.slotKind,
        widget.slotIndex,
        i,
      );
      final text = _service.pluginParamText(
        widget.trackId,
        widget.slotKind,
        widget.slotIndex,
        i,
      );
      final def = _service.pluginParamDefault(
        widget.trackId,
        widget.slotKind,
        widget.slotIndex,
        i,
      );
      final labels = <String>[];
      if ((flags & 2) != 0 && steps > 0) {
        final n = steps <= 1 ? 1 : steps;
        for (var s = 0; s < n; s++) {
          labels.add(
            _service.pluginParamTextAtValue(
              widget.trackId,
              widget.slotKind,
              widget.slotIndex,
              i,
              n <= 1 ? 0 : s / (n - 1),
            ),
          );
        }
      } else if ((flags & 1) != 0) {
        labels.add(
          _service.pluginParamTextAtValue(
            widget.trackId,
            widget.slotKind,
            widget.slotIndex,
            i,
            0,
          ),
        );
        labels.add(
          _service.pluginParamTextAtValue(
            widget.trackId,
            widget.slotKind,
            widget.slotIndex,
            i,
            1,
          ),
        );
      }
      final p = EzParam(
        index: i,
        id: id,
        name: name,
        group: group,
        value: value,
        text: text,
        flags: flags,
        steps: steps,
        defaultValue: def,
        labels: labels,
      );
      params.add(p);
      map[id] = p;
    }

    final programs = <_ProgramInfo>[];
    final programCount = _service.pluginProgramCount(
      widget.trackId,
      widget.slotKind,
      widget.slotIndex,
    );
    for (var i = 0; i < programCount; i++) {
      programs.add(
        _ProgramInfo(
          i,
          _service.pluginProgramName(
            widget.trackId,
            widget.slotKind,
            widget.slotIndex,
            i,
          ),
        ),
      );
    }
    final current = _service.pluginCurrentProgram(
      widget.trackId,
      widget.slotKind,
      widget.slotIndex,
    );
    if (!mounted) return;
    setState(() {
      _all
        ..clear()
        ..addAll(params);
      _byId
        ..clear()
        ..addAll(map);
      _programs
        ..clear()
        ..addAll(programs);
      _program = current.clamp(0, math.max(0, programs.length - 1));
      _loading = false;
    });
  }

  EzParam? _p(String id) => _byId[id];

  EzParam _req(String id) {
    final p = _byId[id];
    if (p == null) {
      throw StateError('EZSynth parameter "$id" not found');
    }
    return p;
  }

  void _set(EzParam p, double value) {
    var v = value.clamp(0.0, 1.0).toDouble();
    if (p.isInteger && p.steps > 1) {
      v = (v * (p.steps - 1)).round() / (p.steps - 1);
    }
    _service.setPluginParamValue(
      widget.trackId,
      widget.slotKind,
      widget.slotIndex,
      p.index,
      v,
    );
    setState(() {
      p.value = v;
      p.text = _service.pluginParamText(
        widget.trackId,
        widget.slotKind,
        widget.slotIndex,
        p.index,
      );
    });
  }

  void _reset(EzParam p) => _set(p, p.defaultValue);

  void _setChoice(EzParam p, int index) {
    final n = p.steps <= 1 ? 1 : p.steps;
    _set(p, n <= 1 ? 0 : index / (n - 1));
  }

  void _setBool(EzParam p, bool on) => _set(p, on ? 1 : 0);

  Future<void> _applyProgram(int index) async {
    if (index < 0 || index >= _programs.length) return;
    final ok = _service.setPluginProgram(
      widget.trackId,
      widget.slotKind,
      widget.slotIndex,
      index,
    );
    if (!ok) return;
    setState(() {
      _program = index;
      _loading = true;
    });
    // The engine applies presets synchronously on the message thread, but
    // reading back through the same FFI boundary keeps the UI authoritative.
    _load();
  }

  String get _programName {
    if (_program >= 0 && _program < _programs.length) {
      return _programs[_program].shortName;
    }
    return _p('preset')?.choiceLabel ?? 'Init';
  }

  String get _programCategory {
    if (_program >= 0 && _program < _programs.length) {
      return _programs[_program].category;
    }
    return 'Init';
  }

  void _go(String page, {String? layer}) {
    setState(() {
      _page = page;
      if (layer != null) _layer = layer;
    });
    _pageAnim.forward(from: 0);
  }

  @override
  Widget build(BuildContext context) {
    if (_loading) {
      return const Center(
        child: SizedBox(
          width: 26,
          height: 26,
          child: CircularProgressIndicator(strokeWidth: 2, color: _audio),
        ),
      );
    }
    return LayoutBuilder(
      builder: (context, constraints) {
        final compact = constraints.maxWidth < 1080;
        final railWidth = compact ? 132.0 : 164.0;
        return Column(
          children: [
            _buildHeader(compact),
            const Divider(height: 1, color: AppColors.border),
            Expanded(
              child: Row(
                children: [
                  SizedBox(
                    width: railWidth,
                    child: _buildRail(),
                  ),
                  const VerticalDivider(width: 1, color: AppColors.border),
                  Expanded(
                    child: FadeTransition(
                      opacity: CurvedAnimation(
                        parent: _pageAnim,
                        curve: Curves.easeOut,
                      ),
                      child: AnimatedSwitcher(
                        duration: const Duration(milliseconds: 180),
                        switchInCurve: Curves.easeOutCubic,
                        switchOutCurve: Curves.easeInCubic,
                        child: KeyedSubtree(
                          key: ValueKey('$_page-$_layer'),
                          child: _buildPage(compact),
                        ),
                      ),
                    ),
                  ),
                  if (!compact) ...[
                    const VerticalDivider(width: 1, color: AppColors.border),
                    SizedBox(width: 224, child: _buildMacroRail()),
                  ],
                ],
              ),
            ),
            const Divider(height: 1, color: AppColors.border),
            _buildPerformanceStrip(compact),
          ],
        );
      },
    );
  }

  // -----------------------------------------------------------------------
  // Header
  // -----------------------------------------------------------------------
  Widget _buildHeader(bool compact) {
    final mpe = _p('mpe');
    final isMpe = mpe != null && mpe.value > 0.5;
    return Container(
      height: compact ? 62 : 68,
      padding: const EdgeInsets.symmetric(horizontal: 16),
      color: AppColors.bgPanel,
      child: Row(
        children: [
          Container(
            width: 40,
            height: 40,
            decoration: BoxDecoration(
              gradient: const LinearGradient(
                begin: Alignment.topLeft,
                end: Alignment.bottomRight,
                colors: [Color(0xFF2D6FB8), Color(0xFF13253C)],
              ),
              borderRadius: BorderRadius.circular(11),
              border: Border.all(color: _audio.withValues(alpha: 0.55)),
            ),
            child: const Icon(Icons.graphic_eq_rounded, color: _ink, size: 22),
          ),
          const SizedBox(width: 12),
          Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            mainAxisAlignment: MainAxisAlignment.center,
            children: [
              Row(
                children: [
                  const Text(
                    'EZSynth',
                    style: TextStyle(
                      color: _ink,
                      fontSize: 20,
                      fontWeight: FontWeight.w800,
                      letterSpacing: -0.4,
                    ),
                  ),
                  const SizedBox(width: 7),
                  Container(
                    padding: const EdgeInsets.symmetric(
                      horizontal: 6,
                      vertical: 2,
                    ),
                    decoration: BoxDecoration(
                      color: _audio.withValues(alpha: 0.16),
                      borderRadius: BorderRadius.circular(5),
                      border: Border.all(color: _audio.withValues(alpha: 0.4)),
                    ),
                    child: const Text(
                      '2.0',
                      style: TextStyle(
                        color: _audio,
                        fontSize: 9,
                        fontWeight: FontWeight.w800,
                        letterSpacing: 0.5,
                      ),
                    ),
                  ),
                ],
              ),
              Text(
                widget.title.isEmpty ? 'Hybrid performance synthesizer' : widget.title,
                style: const TextStyle(
                  color: AppColors.textFaint,
                  fontSize: 10,
                ),
              ),
            ],
          ),
          const Spacer(),
          _PresetButton(
            category: _programCategory,
            name: _programName,
            onTap: _showPresetBrowser,
          ),
          const SizedBox(width: 10),
          _HeaderPill(
            icon: Icons.bolt_rounded,
            label: isMpe ? 'MPE ON' : 'MPE',
            color: isMpe ? _mod : AppColors.textDim,
            active: isMpe,
            onTap: mpe == null ? null : () => _setBool(mpe, !isMpe),
          ),
          if (!compact) ...[
            const SizedBox(width: 8),
            _HeaderPill(
              icon: Icons.waves_rounded,
              label: _p('quality')?.choiceLabel ?? 'Studio',
              color: AppColors.textDim,
              onTap: () => _go('master'),
            ),
          ],
          const SizedBox(width: 8),
          _HeaderPill(
            icon: Icons.close_rounded,
            label: 'Cerrar',
            color: AppColors.textDim,
            onTap: () => Navigator.of(context).pop(),
          ),
        ],
      ),
    );
  }

  // -----------------------------------------------------------------------
  // Left rail
  // -----------------------------------------------------------------------
  Widget _buildRail() {
    return Container(
      color: AppColors.bgPanel,
      child: ListView(
        padding: const EdgeInsets.fromLTRB(10, 12, 10, 12),
        children: [
          _RailSection('CAPAS', [
            _RailItem(
              icon: Icons.dashboard_customize_rounded,
              label: 'Resumen',
              selected: _page == 'overview',
              color: _audio,
              onTap: () => _go('overview'),
            ),
            _RailItem(
              icon: Icons.layers_rounded,
              label: 'Capa A',
              badge: _engineName('a'),
              selected: _page == 'source' && _layer == 'a',
              color: _audio,
              onTap: () => _go('source', layer: 'a'),
            ),
            _RailItem(
              icon: Icons.layers_outlined,
              label: 'Capa B',
              badge: _engineName('b'),
              selected: _page == 'source' && _layer == 'b',
              color: _mod,
              onTap: () => _go('source', layer: 'b'),
            ),
            _RailItem(
              icon: Icons.grain_rounded,
              label: 'Auxiliar',
              badge: _engineName('x'),
              selected: _page == 'source' && _layer == 'x',
              color: _fx,
              onTap: () => _go('source', layer: 'x'),
            ),
          ]),
          const SizedBox(height: 14),
          _RailSection('FORMA', [
            _RailItem(
              icon: Icons.filter_alt_rounded,
              label: 'Filtros',
              selected: _page == 'filter',
              color: _audio,
              onTap: () => _go('filter'),
            ),
            _RailItem(
              icon: Icons.show_chart_rounded,
              label: 'Envolventes',
              selected: _page == 'env',
              color: _env,
              onTap: () => _go('env'),
            ),
            _RailItem(
              icon: Icons.hub_rounded,
              label: 'Modulación',
              badge: '${_activeMatrixCount()}',
              selected: _page == 'mod',
              color: _mod,
              onTap: () => _go('mod'),
            ),
          ]),
          const SizedBox(height: 14),
          _RailSection('PERFORMANCE', [
            _RailItem(
              icon: Icons.tune_rounded,
              label: 'FX Rack',
              badge: '${_activeFxCount()}',
              selected: _page == 'fx',
              color: _fx,
              onTap: () => _go('fx'),
            ),
            _RailItem(
              icon: Icons.grid_view_rounded,
              label: 'Arp / Seq',
              selected: _page == 'arp',
              color: _macro,
              onTap: () => _go('arp'),
            ),
            _RailItem(
              icon: Icons.auto_awesome_rounded,
              label: 'Macros',
              selected: _page == 'perform',
              color: _macro,
              onTap: () => _go('perform'),
            ),
            _RailItem(
              icon: Icons.timeline_rounded,
              label: 'Motion / LINES',
              selected: _page == 'motion',
              color: _mod,
              onTap: () => _go('motion'),
            ),
            _RailItem(
              icon: Icons.settings_input_component_rounded,
              label: 'Master',
              selected: _page == 'master',
              color: AppColors.textDim,
              onTap: () => _go('master'),
            ),
          ]),
          const SizedBox(height: 14),
          _RailSection('BANCO', [
            _RailItem(
              icon: Icons.library_music_rounded,
              label: 'Presets',
              badge: '${_programs.length}',
              selected: false,
              color: _macro,
              onTap: _showPresetBrowser,
            ),
          ]),
        ],
      ),
    );
  }

  String _engineName(String layer) {
    final id = layer == 'x' ? 'x_engine' : '${layer}_engine';
    final p = _p(id);
    if (p == null) return '';
    return p.choiceLabel;
  }

  int _activeMatrixCount() {
    var n = 0;
    for (var i = 1; i <= 16; i++) {
      final src = _p('m${i}src');
      final dst = _p('m${i}dst');
      final amt = _p('m${i}amt');
      if (src != null && dst != null && amt != null) {
        if (src.choiceIndex != 0 &&
            dst.choiceIndex != 0 &&
            (amt.value - 0.5).abs() > 0.005) {
          n++;
        }
      }
    }
    return n;
  }

  int _activeFxCount() {
    var n = 0;
    for (var i = 1; i <= 6; i++) {
      if ((_p('fx${i}type')?.choiceIndex ?? 0) != 0) n++;
    }
    return n;
  }

  // -----------------------------------------------------------------------
  // Macro rail
  // -----------------------------------------------------------------------
  Widget _buildMacroRail() {
    return Container(
      color: AppColors.bgPanel,
      child: ListView(
        padding: const EdgeInsets.fromLTRB(14, 16, 14, 14),
        children: [
          _SectionTitle(
            'MACROS',
            subtitle: '8 controles · 4 rutas cada uno',
            color: _macro,
          ),
          const SizedBox(height: 10),
          ...List.generate(8, (i) {
            final p = _p('macro${i + 1}');
            if (p == null) return const SizedBox.shrink();
            return Padding(
              padding: const EdgeInsets.only(bottom: 9),
              child: _MacroFader(
                label: _macroNames[i],
                sublabel: _macroDestLabel(i),
                value: p.value,
                color: _macro,
                onChanged: (v) => _set(p, v),
                onReset: () => _set(p, 0.5),
              ),
            );
          }),
          const SizedBox(height: 10),
          const Divider(height: 1, color: AppColors.border),
          const SizedBox(height: 14),
          _SectionTitle('MORPH', subtitle: 'A ↔ B', color: _mod),
          const SizedBox(height: 8),
          Center(
            child: _ParamKnob(
              param: _req('morph'),
              size: 104,
              color: _mod,
              label: 'A ↔ B',
              onSet: _set,
              onReset: _reset,
            ),
          ),
          const SizedBox(height: 6),
          Center(
            child: Text(
              _p('morphMode')?.choiceLabel ?? 'Crossfade',
              style: const TextStyle(
                color: AppColors.textFaint,
                fontSize: 10,
              ),
            ),
          ),
          const SizedBox(height: 14),
          _Card(
            padding: const EdgeInsets.all(11),
            child: Column(
              children: [
                _MeterRow(
                  label: 'A',
                  value: (_p('a_level')?.value ?? 0) *
                      (1 - (_p('morph')?.value ?? 0) * 0.65),
                  color: _audio,
                ),
                const SizedBox(height: 7),
                _MeterRow(
                  label: 'B',
                  value: (_p('b_level')?.value ?? 0) *
                      (0.2 + (_p('morph')?.value ?? 0) * 0.8),
                  color: _mod,
                ),
                const SizedBox(height: 7),
                _MeterRow(
                  label: 'AUX',
                  value: _p('x_level')?.value ?? 0,
                  color: _fx,
                ),
              ],
            ),
          ),
        ],
      ),
    );
  }

  String _macroDestLabel(int i) {
    for (var r = 1; r <= 4; r++) {
      final dst = _p('macro${i + 1}r${r}dst');
      final amt = _p('macro${i + 1}r${r}amt');
      if (dst != null && amt != null && dst.choiceIndex != 0) {
        final sign = amt.value >= 0.5 ? '+' : '−';
        return '$sign ${dst.choiceLabel}';
      }
    }
    return 'sin ruta';
  }

  // -----------------------------------------------------------------------
  // Performance strip
  // -----------------------------------------------------------------------
  Widget _buildPerformanceStrip(bool compact) {
    final a = _p('a_level')?.value ?? 0;
    final b = _p('b_level')?.value ?? 0;
    final morph = _p('morph')?.value ?? 0;
    return Container(
      height: compact ? 58 : 66,
      color: AppColors.bgPanel,
      padding: const EdgeInsets.symmetric(horizontal: 14),
      child: Row(
        children: [
          Container(
            width: compact ? 178 : 260,
            height: 38,
            decoration: BoxDecoration(
              color: AppColors.bgDeep,
              borderRadius: BorderRadius.circular(7),
              border: Border.all(color: AppColors.border),
            ),
            child: CustomPaint(
              painter: _MiniKeyboardPainter(
                highlight: {
                  60,
                  if (morph > 0.5) 64,
                  if (morph > 0.8) 67,
                },
              ),
            ),
          ),
          const SizedBox(width: 14),
          Expanded(
            child: Row(
              children: [
                _LegendDot(color: _audio, label: 'Audio'),
                const SizedBox(width: 12),
                _LegendDot(color: _mod, label: 'Modulación'),
                const SizedBox(width: 12),
                _LegendDot(color: _macro, label: 'Macros'),
                const SizedBox(width: 12),
                _LegendDot(color: _env, label: 'Envelopes'),
              ],
            ),
          ),
          if (!compact) ...[
            _TinyReadout(label: 'CAPA A', value: '${(a * 100).round()}%'),
            const SizedBox(width: 12),
            _TinyReadout(label: 'CAPA B', value: '${(b * 100).round()}%'),
            const SizedBox(width: 12),
            _TinyReadout(label: 'MORPH', value: '${(morph * 100).round()}%'),
            const SizedBox(width: 14),
          ],
          _HeaderPill(
            icon: Icons.music_note_rounded,
            label: '${_programs.length} presets',
            color: _macro,
            onTap: _showPresetBrowser,
          ),
        ],
      ),
    );
  }

  // -----------------------------------------------------------------------
  // Page router
  // -----------------------------------------------------------------------
  Widget _buildPage(bool compact) {
    switch (_page) {
      case 'source':
        return _sourcePage(compact);
      case 'filter':
        return _filterPage(compact);
      case 'env':
        return _envPage(compact);
      case 'mod':
        return _modPage(compact);
      case 'fx':
        return _fxPage(compact);
      case 'arp':
        return _arpPage(compact);
      case 'perform':
        return _performPage(compact);
      case 'motion':
        return _motionPage(compact);
      case 'master':
        return _masterPage(compact);
      default:
        return _overviewPage(compact);
    }
  }

  // -----------------------------------------------------------------------
  // Preset browser
  // -----------------------------------------------------------------------
  Future<void> _showPresetBrowser() async {
    final selected = await showDialog<int>(
      context: context,
      barrierColor: Colors.black.withValues(alpha: 0.62),
      builder: (context) => _PresetBrowser(
        programs: _programs,
        current: _program,
      ),
    );
    if (selected != null) await _applyProgram(selected);
  }
}

// ===========================================================================
// Pages. They live in an extension so the file stays navigable; the extension
// still has access to the state's parameter map and helpers.
// ===========================================================================
extension _StudioPages on _EzSynthStudioState {
  // -----------------------------------------------------------------------
  // Overview
  // -----------------------------------------------------------------------
  Widget _overviewPage(bool compact) {
    final morph = _req('morph');
    return SingleChildScrollView(
      padding: EdgeInsets.all(compact ? 14 : 20),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          _HeroCard(
            morph: morph,
            morphMode: _p('morphMode'),
            layerA: _engineName('a'),
            layerB: _engineName('b'),
            onSetMorph: (v) => _set(morph, v),
            onResetMorph: () => _reset(morph),
            onSetMode: (i) {
              final p = _p('morphMode');
              if (p != null) _setChoice(p, i);
            },
          ),
          const SizedBox(height: 16),
          _SectionTitle(
            'CAPAS',
            subtitle: 'Dos motores completos y una capa auxiliar',
            trailing: _HeaderPill(
              icon: Icons.add_rounded,
              label: 'Editar capa',
              color: _audio,
              onTap: () => _go('source', layer: 'a'),
            ),
          ),
          const SizedBox(height: 10),
          LayoutBuilder(
            builder: (context, c) {
              final width = c.maxWidth >= 980
                  ? (c.maxWidth - 24) / 3
                  : c.maxWidth >= 620
                      ? (c.maxWidth - 12) / 2
                      : c.maxWidth;
              return Wrap(
                spacing: 12,
                runSpacing: 12,
                children: [
                  SizedBox(
                    width: width,
                    child: _LayerSummaryCard(
                      title: 'CAPA A',
                      subtitle: _engineName('a'),
                      color: _audio,
                      level: _p('a_level'),
                      pan: _p('a_pan'),
                      tune: _p('a_tune'),
                      cutoff: _p('fltcut'),
                      onOpen: () => _go('source', layer: 'a'),
                      onLevel: (v) {
                        final p = _p('a_level');
                        if (p != null) _set(p, v);
                      },
                    ),
                  ),
                  SizedBox(
                    width: width,
                    child: _LayerSummaryCard(
                      title: 'CAPA B',
                      subtitle: _engineName('b'),
                      color: _mod,
                      level: _p('b_level'),
                      pan: _p('b_pan'),
                      tune: _p('b_tune'),
                      cutoff: _p('f2cut'),
                      onOpen: () => _go('source', layer: 'b'),
                      onLevel: (v) {
                        final p = _p('b_level');
                        if (p != null) _set(p, v);
                      },
                    ),
                  ),
                  SizedBox(
                    width: width,
                    child: _LayerSummaryCard(
                      title: 'AUXILIAR',
                      subtitle: _engineName('x'),
                      color: _fx,
                      level: _p('x_level'),
                      pan: _p('x_pan'),
                      tune: _p('x_tune'),
                      cutoff: _p('x_color'),
                      onOpen: () => _go('source', layer: 'x'),
                      onLevel: (v) {
                        final p = _p('x_level');
                        if (p != null) _set(p, v);
                      },
                    ),
                  ),
                ],
              );
            },
          ),
          const SizedBox(height: 16),
          _SectionTitle(
            'RENDIMIENTO',
            subtitle: 'Los controles que importan en una toma',
            color: _macro,
          ),
          const SizedBox(height: 10),
          _Card(
            padding: const EdgeInsets.all(14),
            child: Wrap(
              spacing: 18,
              runSpacing: 14,
              alignment: WrapAlignment.spaceBetween,
              children: [
                _overviewKnob('morph', 'Morph', _mod),
                _overviewKnob('fltcut', 'Corte', _audio),
                _overviewKnob('fltres', 'Reso', _audio),
                _overviewKnob('l1depth', 'LFO 1', _mod),
                _overviewKnob('l2depth', 'LFO 2', _mod),
                _overviewKnob('fx1mix', 'FX 1', _fx),
                _overviewKnob('fx2mix', 'FX 2', _fx),
                _overviewKnob('width', 'Ancho', _audio),
              ],
            ),
          ),
          const SizedBox(height: 16),
          _SectionTitle('BANCO DE PRESETS', subtitle: '${_programs.length} sonidos originales', color: _macro),
          const SizedBox(height: 10),
          _PresetStrip(
            programs: _programs,
            current: _program,
            onSelect: (i) { _applyProgram(i); },
            onOpen: _showPresetBrowser,
          ),
        ],
      ),
    );
  }

  // -----------------------------------------------------------------------
  // Source
  // -----------------------------------------------------------------------
  Widget _sourcePage(bool compact) {
    if (_layer == 'x') return _auxSourcePage(compact);
    final isA = _layer == 'a';
    // Oscillator IDs are unprefixed for layer A and b_-prefixed for layer B;
    // engine-specific blocks always use the layer prefix (a_/b_).
    final prefix = isA ? '' : 'b_';
    final featurePrefix = isA ? 'a_' : 'b_';
    final accent = isA ? _audio : _mod;
    final engine = _p('${featurePrefix}engine');
    final engineIndex = engine?.choiceIndex ?? 0;

    return SingleChildScrollView(
      padding: EdgeInsets.all(compact ? 14 : 20),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          _PageHeading(
            title: isA ? 'Capa A' : 'Capa B',
            subtitle: isA
                ? 'Motor principal · fuente del carácter'
                : 'Segundo motor · mezcla, contraste y morph',
            color: accent,
            trailing: _EngineNameBadge(
              label: engine?.choiceLabel ?? 'Off',
              color: accent,
            ),
          ),
          const SizedBox(height: 14),
          if (engine != null)
            _EngineSelector(
              param: engine,
              accent: accent,
              onSelect: (i) => _setChoice(engine, i),
            ),
          const SizedBox(height: 14),
          if (engineIndex == 0)
            _Card(
              padding: const EdgeInsets.all(22),
              child: Row(
                children: [
                  Icon(Icons.power_settings_new_rounded, color: accent, size: 30),
                  const SizedBox(width: 14),
                  Expanded(
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        const Text(
                          'Motor apagado',
                          style: TextStyle(color: _ink, fontSize: 15, fontWeight: FontWeight.w700),
                        ),
                        const SizedBox(height: 4),
                        Text(
                          'Elige Analog, Wavetable, FM o Sample para activar esta capa.',
                          style: TextStyle(color: AppColors.textDim, fontSize: 11, height: 1.4),
                        ),
                      ],
                    ),
                  ),
                ],
              ),
            )
          else ...[
            _EnginePreview(
              engine: engineIndex,
              accent: accent,
              wave: _p(isA ? 'o1wave' : 'b_o1wave'),
              wtPos: _p('${featurePrefix}wtPos'),
              fmRatio: _p('${featurePrefix}fmRatio'),
              fmIndex: _p('${featurePrefix}fmIndex'),
              grainSize: _p('${featurePrefix}grainSize'),
              grainDensity: _p('${featurePrefix}grainDensity'),
            ),
            const SizedBox(height: 14),
            if (engineIndex == 1) _analogPage(prefix, accent, compact, isA),
            if (engineIndex == 2) _wavetablePage(featurePrefix, accent, compact, isA),
            if (engineIndex == 3) _fmPage(featurePrefix, accent, compact, isA),
            if (engineIndex == 4) _samplePage(featurePrefix, accent, compact),
            const SizedBox(height: 14),
            _LayerCommonCard(prefix: prefix, accent: accent, isA: isA, params: _byId, onSet: _set, onReset: _reset),
          ],
        ],
      ),
    );
  }

  Widget _analogPage(String prefix, Color accent, bool compact, bool isA) {
    final cards = <Widget>[];
    for (var o = 1; o <= 3; o++) {
      final waveId = isA ? 'o${o}wave' : 'b_o${o}wave';
      final octId = isA ? 'o${o}oct' : 'b_o${o}oct';
      final semiId = isA ? 'o${o}semi' : 'b_o${o}semi';
      final detId = isA ? 'o${o}det' : 'b_o${o}det';
      final lvlId = isA ? 'o${o}lvl' : 'b_o${o}lvl';
      cards.add(
        _OscCard(
          number: o,
          accent: accent,
          wave: _p(waveId),
          octave: _p(octId),
          semi: _p(semiId),
          detune: _p(detId),
          level: _p(lvlId),
          onSet: _set,
          onSetChoice: _setChoice,
          onReset: _reset,
        ),
      );
    }
    return LayoutBuilder(
      builder: (context, c) {
        final width = c.maxWidth >= 900
            ? (c.maxWidth - 24) / 3
            : c.maxWidth >= 560
                ? (c.maxWidth - 12) / 2
                : c.maxWidth;
        return Wrap(
          spacing: 12,
          runSpacing: 12,
          children: [for (final card in cards) SizedBox(width: width, child: card)],
        );
      },
    );
  }

  Widget _wavetablePage(String prefix, Color accent, bool compact, bool isA) {
    return _Card(
      padding: const EdgeInsets.all(16),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          _CardHeader(
            icon: Icons.waves_rounded,
            title: 'Wavetable',
            subtitle: 'Lectura morfada de tablas band-limited',
            color: accent,
          ),
          const SizedBox(height: 14),
          Wrap(
            spacing: 18,
            runSpacing: 12,
            crossAxisAlignment: WrapCrossAlignment.center,
            children: [
              if (_p('${prefix}wtTable') != null)
                _ParamChoiceField(
                  param: _req('${prefix}wtTable'),
                  label: 'Tabla',
                  onChoice: _setChoice,
                  width: 150,
                ),
              _ParamKnob(param: _req('${prefix}wtPos'), label: 'Posición', color: accent, onSet: _set, onReset: _reset),
              _ParamKnob(param: _req('${prefix}wtWarp'), label: 'Warp', color: accent, onSet: _set, onReset: _reset),
              _ParamKnob(param: _req(isA ? 'uni' : 'b_uni'), label: 'Unísono', color: accent, onSet: _set, onReset: _reset),
              _ParamKnob(param: _req(isA ? 'unispr' : 'b_unispr'), label: 'Apertura', color: accent, onSet: _set, onReset: _reset),
            ],
          ),
          const SizedBox(height: 12),
          ClipRRect(
            borderRadius: BorderRadius.circular(8),
            child: Container(
              height: 92,
              color: AppColors.bgDeep,
              child: CustomPaint(
                painter: _WavetablePreviewPainter(
                  position: _p('${prefix}wtPos')?.value ?? 0,
                  warp: _p('${prefix}wtWarp')?.value ?? 0,
                  table: _p('${prefix}wtTable')?.choiceIndex ?? 1,
                  color: accent,
                ),
              ),
            ),
          ),
        ],
      ),
    );
  }

  Widget _fmPage(String prefix, Color accent, bool compact, bool isA) {
    return _Card(
      padding: const EdgeInsets.all(16),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          _CardHeader(
            icon: Icons.blur_on_rounded,
            title: 'FM',
            subtitle: 'Operadores sinusoidales con feedback',
            color: accent,
          ),
          const SizedBox(height: 14),
          Wrap(
            spacing: 18,
            runSpacing: 12,
            crossAxisAlignment: WrapCrossAlignment.center,
            children: [
              if (_p('${prefix}fmMode') != null)
                _ParamChoiceField(
                  param: _req('${prefix}fmMode'),
                  label: 'Modo',
                  onChoice: _setChoice,
                  width: 130,
                ),
              _ParamKnob(param: _req('${prefix}fmRatio'), label: 'Ratio', color: accent, onSet: _set, onReset: _reset),
              _ParamKnob(param: _req('${prefix}fmIndex'), label: 'Índice', color: accent, onSet: _set, onReset: _reset),
              _ParamKnob(param: _req('${prefix}fmFeedback'), label: 'Feedback', color: accent, onSet: _set, onReset: _reset),
              _ParamKnob(param: _req(isA ? 'uni' : 'b_uni'), label: 'Unísono', color: accent, onSet: _set, onReset: _reset),
            ],
          ),
          const SizedBox(height: 12),
          ClipRRect(
            borderRadius: BorderRadius.circular(8),
            child: Container(
              height: 92,
              color: AppColors.bgDeep,
              child: CustomPaint(
                painter: _FmPreviewPainter(
                  ratio: _p('${prefix}fmRatio')?.value ?? 0.5,
                  index: _p('${prefix}fmIndex')?.value ?? 0.2,
                  feedback: _p('${prefix}fmFeedback')?.value ?? 0,
                  color: accent,
                ),
              ),
            ),
          ),
        ],
      ),
    );
  }

  Widget _samplePage(String prefix, Color accent, bool compact) {
    return _Card(
      padding: const EdgeInsets.all(16),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          _CardHeader(
            icon: Icons.grain_rounded,
            title: 'Sample / Grain',
            subtitle: 'Nubes de granos, textura y movimiento',
            color: accent,
          ),
          const SizedBox(height: 14),
          Wrap(
            spacing: 18,
            runSpacing: 12,
            crossAxisAlignment: WrapCrossAlignment.center,
            children: [
              if (_p('${prefix}sampleTable') != null)
                _ParamChoiceField(
                  param: _req('${prefix}sampleTable'),
                  label: 'Tabla',
                  onChoice: _setChoice,
                  width: 150,
                ),
              _ParamKnob(param: _req('${prefix}grainSize'), label: 'Grano', color: accent, onSet: _set, onReset: _reset),
              _ParamKnob(param: _req('${prefix}grainDensity'), label: 'Densidad', color: accent, onSet: _set, onReset: _reset),
              _ParamKnob(param: _req('${prefix}grainSpray'), label: 'Spray', color: accent, onSet: _set, onReset: _reset),
              _ParamKnob(param: _req('${prefix}grainTone'), label: 'Tono', color: accent, onSet: _set, onReset: _reset),
            ],
          ),
          const SizedBox(height: 12),
          ClipRRect(
            borderRadius: BorderRadius.circular(8),
            child: Container(
              height: 92,
              color: AppColors.bgDeep,
              child: CustomPaint(
                painter: _GrainPreviewPainter(
                  size: _p('${prefix}grainSize')?.value ?? 0.3,
                  density: _p('${prefix}grainDensity')?.value ?? 0.3,
                  spray: _p('${prefix}grainSpray')?.value ?? 0.2,
                  color: accent,
                ),
              ),
            ),
          ),
        ],
      ),
    );
  }

  Widget _auxSourcePage(bool compact) {
    final engine = _p('x_engine');
    return SingleChildScrollView(
      padding: EdgeInsets.all(compact ? 14 : 20),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          _PageHeading(
            title: 'Capa auxiliar',
            subtitle: 'Ruido, transitorios y texturas para completar el gesto',
            color: _fx,
            trailing: _EngineNameBadge(label: engine?.choiceLabel ?? 'Off', color: _fx),
          ),
          const SizedBox(height: 14),
          _Card(
            padding: const EdgeInsets.all(16),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                _CardHeader(
                  icon: Icons.grain_rounded,
                  title: 'Auxiliar',
                  subtitle: 'Una tercera fuente independiente',
                  color: _fx,
                ),
                const SizedBox(height: 14),
                if (engine != null)
                  _EngineSelector(
                    param: engine,
                    accent: _fx,
                    choices: const ['Off', 'Noise', 'Sine', 'Click', 'Texture'],
                    onSelect: (i) => _setChoice(engine, i),
                  ),
                const SizedBox(height: 16),
                Wrap(
                  spacing: 18,
                  runSpacing: 12,
                  children: [
                    _ParamKnob(param: _req('x_level'), label: 'Nivel', color: _fx, onSet: _set, onReset: _reset),
                    _ParamKnob(param: _req('x_pan'), label: 'Pan', color: _fx, onSet: _set, onReset: _reset),
                    _ParamKnob(param: _req('x_tune'), label: 'Tono', color: _fx, onSet: _set, onReset: _reset),
                    _ParamKnob(param: _req('x_decay'), label: 'Decay', color: _fx, onSet: _set, onReset: _reset),
                    _ParamKnob(param: _req('x_color'), label: 'Color', color: _fx, onSet: _set, onReset: _reset),
                  ],
                ),
              ],
            ),
          ),
        ],
      ),
    );
  }

  // -----------------------------------------------------------------------
  // Filter
  // -----------------------------------------------------------------------
  Widget _filterPage(bool compact) {
    return SingleChildScrollView(
      padding: EdgeInsets.all(compact ? 14 : 20),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          _PageHeading(
            title: 'Filtros',
            subtitle: 'Dos filtros multimodo con ruteo serie, paralelo o único',
            color: _audio,
            trailing: _EngineNameBadge(label: _p('froute')?.choiceLabel ?? 'Single', color: _audio),
          ),
          const SizedBox(height: 14),
          LayoutBuilder(
            builder: (context, c) {
              final width = c.maxWidth >= 940 ? (c.maxWidth - 14) / 2 : c.maxWidth;
              return Wrap(
                spacing: 14,
                runSpacing: 14,
                children: [
                  SizedBox(
                    width: width,
                    child: _FilterCard(
                      number: 1,
                      type: _p('flttype'),
                      cutoff: _p('fltcut'),
                      reso: _p('fltres'),
                      keytrack: _p('fltkey'),
                      env: _p('fltenv'),
                      drive: _p('fltdrive'),
                      color: _audio,
                      onSet: _set,
                      onReset: _reset,
                      onChoice: _setChoice,
                    ),
                  ),
                  SizedBox(
                    width: width,
                    child: _FilterCard(
                      number: 2,
                      type: _p('f2type'),
                      cutoff: _p('f2cut'),
                      reso: _p('f2res'),
                      keytrack: _p('f2key'),
                      env: _p('f2env'),
                      drive: _p('f2drive'),
                      color: _mod,
                      onSet: _set,
                      onReset: _reset,
                      onChoice: _setChoice,
                    ),
                  ),
                ],
              );
            },
          ),
          const SizedBox(height: 14),
          _Card(
            padding: const EdgeInsets.all(16),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                _CardHeader(
                  icon: Icons.call_split_rounded,
                  title: 'Ruteo y balance',
                  subtitle: 'Cómo se combinan los dos filtros',
                  color: _audio,
                ),
                const SizedBox(height: 12),
                Wrap(
                  spacing: 20,
                  runSpacing: 12,
                  crossAxisAlignment: WrapCrossAlignment.center,
                  children: [
                    if (_p('froute') != null)
                      _ParamChoiceField(
                        param: _req('froute'),
                        label: 'Ruteo',
                        onChoice: _setChoice,
                        width: 150,
                      ),
                    _ParamKnob(param: _req('fbalance'), label: 'Balance', color: _audio, onSet: _set, onReset: _reset),
                    _ParamKnob(param: _req('fltdrive'), label: 'Drive 1', color: _audio, onSet: _set, onReset: _reset),
                    _ParamKnob(param: _req('f2drive'), label: 'Drive 2', color: _mod, onSet: _set, onReset: _reset),
                  ],
                ),
                const SizedBox(height: 12),
                Container(
                  height: 110,
                  decoration: BoxDecoration(
                    color: AppColors.bgDeep,
                    borderRadius: BorderRadius.circular(8),
                    border: Border.all(color: AppColors.border),
                  ),
                  child: CustomPaint(
                    painter: _DualFilterResponsePainter(
                      type1: _p('flttype')?.choiceIndex ?? 1,
                      cutoff1: _p('fltcut')?.value ?? 0.7,
                      reso1: _p('fltres')?.value ?? 0.2,
                      type2: _p('f2type')?.choiceIndex ?? 0,
                      cutoff2: _p('f2cut')?.value ?? 0.5,
                      reso2: _p('f2res')?.value ?? 0.1,
                      route: _p('froute')?.choiceIndex ?? 0,
                      balance: _p('fbalance')?.value ?? 0.5,
                    ),
                  ),
                ),
              ],
            ),
          ),
          const SizedBox(height: 14),
          _SectionTitle('ENVOLVENTE DE FILTRO 1', subtitle: 'Env 1 · movimiento de corte', color: _env),
          const SizedBox(height: 10),
          _EnvCard(
            title: 'Env 1',
            subtitle: 'Filtro 1',
            a: _p('fenvA'),
            d: _p('fenvD'),
            s: _p('fenvS'),
            r: _p('fenvR'),
            color: _env,
            onSet: _set,
            onReset: _reset,
          ),
        ],
      ),
    );
  }

  // -----------------------------------------------------------------------
  // Envelopes
  // -----------------------------------------------------------------------
  Widget _envPage(bool compact) {
    final cards = <Widget>[
      _EnvCard(
        title: 'Env 1',
        subtitle: 'Filtro 1',
        a: _p('fenvA'),
        d: _p('fenvD'),
        s: _p('fenvS'),
        r: _p('fenvR'),
        color: _env,
        onSet: _set,
        onReset: _reset,
      ),
      _EnvCard(
        title: 'Env 2',
        subtitle: 'Amplitud',
        a: _p('aenvA'),
        d: _p('aenvD'),
        s: _p('aenvS'),
        r: _p('aenvR'),
        color: _env,
        onSet: _set,
        onReset: _reset,
      ),
      _EnvCard(
        title: 'Env 3',
        subtitle: 'Filtro 2 / matriz',
        a: _p('e3A'),
        d: _p('e3D'),
        s: _p('e3S'),
        r: _p('e3R'),
        color: _env,
        onSet: _set,
        onReset: _reset,
      ),
      _EnvCard(
        title: 'Env 4',
        subtitle: 'Matriz de modulación',
        a: _p('e4A'),
        d: _p('e4D'),
        s: _p('e4S'),
        r: _p('e4R'),
        color: _env,
        onSet: _set,
        onReset: _reset,
      ),
    ];
    return SingleChildScrollView(
      padding: EdgeInsets.all(compact ? 14 : 20),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          _PageHeading(
            title: 'Envolventes',
            subtitle: 'Cuatro generadores ADSR independientes',
            color: _env,
            trailing: const _EngineNameBadge(label: '4 × ADSR', color: _env),
          ),
          const SizedBox(height: 14),
          LayoutBuilder(
            builder: (context, c) {
              final width = c.maxWidth >= 940 ? (c.maxWidth - 14) / 2 : c.maxWidth;
              return Wrap(
                spacing: 14,
                runSpacing: 14,
                children: [for (final card in cards) SizedBox(width: width, child: card)],
              );
            },
          ),
        ],
      ),
    );
  }

  // -----------------------------------------------------------------------
  // Modulation
  // -----------------------------------------------------------------------
  Widget _modPage(bool compact) {
    final lfoCards = <Widget>[];
    for (var i = 1; i <= 4; i++) {
      lfoCards.add(
        _LfoCard(
          number: i,
          shape: _p('l${i}shape'),
          rate: _p('l${i}rate'),
          depth: _p('l${i}depth'),
          dest: _p('l${i}dest'),
          color: _mod,
          onSet: _set,
          onReset: _reset,
          onChoice: _setChoice,
        ),
      );
    }
    final randomCards = <Widget>[
      _RandomCard(
        number: 1,
        rate: _p('rnd1rate'),
        depth: _p('rnd1depth'),
        color: _mod,
        onSet: _set,
        onReset: _reset,
      ),
      _RandomCard(
        number: 2,
        rate: _p('rnd2rate'),
        depth: _p('rnd2depth'),
        color: _mod,
        onSet: _set,
        onReset: _reset,
      ),
    ];
    final matrixRows = <Widget>[];
    for (var i = 1; i <= 16; i++) {
      final src = _p('m${i}src');
      final dst = _p('m${i}dst');
      final amt = _p('m${i}amt');
      if (src == null || dst == null || amt == null) continue;
      matrixRows.add(
        _MatrixRow(
          number: i,
          source: src,
          dest: dst,
          amount: amt,
          onChoice: _setChoice,
          onSet: _set,
        ),
      );
    }
    return SingleChildScrollView(
      padding: EdgeInsets.all(compact ? 14 : 20),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          _PageHeading(
            title: 'Modulación',
            subtitle: '4 LFOs, 2 fuentes aleatorias y una matriz de 16 destinos',
            color: _mod,
            trailing: _EngineNameBadge(label: '${_activeMatrixCount()} rutas', color: _mod),
          ),
          const SizedBox(height: 14),
          LayoutBuilder(
            builder: (context, c) {
              final side = c.maxWidth >= 1000 ? 330.0 : c.maxWidth;
              if (c.maxWidth >= 1000) {
                return Row(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    SizedBox(
                      width: side,
                      child: Column(
                        children: [
                          ...lfoCards,
                          const SizedBox(height: 12),
                          Row(
                            children: [
                              Expanded(child: randomCards[0]),
                              const SizedBox(width: 12),
                              Expanded(child: randomCards[1]),
                            ],
                          ),
                        ],
                      ),
                    ),
                    const SizedBox(width: 16),
                    Expanded(child: _MatrixPanel(rows: matrixRows)),
                  ],
                );
              }
              return Column(
                children: [
                  Wrap(
                    spacing: 12,
                    runSpacing: 12,
                    children: [for (final card in lfoCards) SizedBox(width: (c.maxWidth - 12) / 2, child: card)],
                  ),
                  const SizedBox(height: 12),
                  Row(
                    children: [
                      Expanded(child: randomCards[0]),
                      const SizedBox(width: 12),
                      Expanded(child: randomCards[1]),
                    ],
                  ),
                  const SizedBox(height: 14),
                  _MatrixPanel(rows: matrixRows),
                ],
              );
            },
          ),
        ],
      ),
    );
  }

  // -----------------------------------------------------------------------
  // FX
  // -----------------------------------------------------------------------
  Widget _fxPage(bool compact) {
    final cards = <Widget>[];
    for (var i = 1; i <= 6; i++) {
      final type = _p('fx${i}type');
      if (type == null) continue;
      cards.add(
        _FxSlotCard(
          number: i,
          type: type,
          mix: _p('fx${i}mix'),
          p1: _p('fx${i}p1'),
          p2: _p('fx${i}p2'),
          p3: _p('fx${i}p3'),
          p4: _p('fx${i}p4'),
          onSet: _set,
          onReset: _reset,
          onChoice: _setChoice,
        ),
      );
    }
    return SingleChildScrollView(
      padding: EdgeInsets.all(compact ? 14 : 20),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          _PageHeading(
            title: 'FX Rack',
            subtitle: 'Seis slots seriales, propios de este instrumento',
            color: _fx,
            trailing: _EngineNameBadge(label: '${_activeFxCount()} activos', color: _fx),
          ),
          const SizedBox(height: 12),
          _FxChainStrip(
            active: [
              for (var i = 1; i <= 6; i++)
                (_p('fx${i}type')?.choiceIndex ?? 0) != 0,
            ],
            labels: [
              for (var i = 1; i <= 6; i++)
                _p('fx${i}type')?.choiceLabel ?? 'Off',
            ],
          ),
          const SizedBox(height: 14),
          LayoutBuilder(
            builder: (context, c) {
              final width = c.maxWidth >= 980 ? (c.maxWidth - 14) / 2 : c.maxWidth;
              return Wrap(
                spacing: 14,
                runSpacing: 14,
                children: [for (final card in cards) SizedBox(width: width, child: card)],
              );
            },
          ),
        ],
      ),
    );
  }

  // -----------------------------------------------------------------------
  // Arp / Seq
  // -----------------------------------------------------------------------
  Widget _arpPage(bool compact) {
    final on = _p('arpOn');
    final isOn = on != null && on.value > 0.5;
    final steps = _p('arpSteps')?.text ?? '16';
    return SingleChildScrollView(
      padding: EdgeInsets.all(compact ? 14 : 20),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          _PageHeading(
            title: 'Arp / Seq',
            subtitle: 'Patrón de 16 pasos sincronizado al tempo del host',
            color: _macro,
            trailing: _HeaderPill(
              icon: isOn ? Icons.pause_rounded : Icons.play_arrow_rounded,
              label: isOn ? 'ACTIVO' : 'OFF',
              color: isOn ? _macro : AppColors.textDim,
              active: isOn,
              onTap: on == null ? null : () => _setBool(on, !isOn),
            ),
          ),
          const SizedBox(height: 14),
          _Card(
            padding: const EdgeInsets.all(16),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                _CardHeader(
                  icon: Icons.grid_view_rounded,
                  title: 'Patrón',
                  subtitle: 'Los pasos apagados crean el groove',
                  color: _macro,
                ),
                const SizedBox(height: 14),
                Wrap(
                  spacing: 18,
                  runSpacing: 12,
                  crossAxisAlignment: WrapCrossAlignment.center,
                  children: [
                    if (_p('arpMode') != null)
                      _ParamChoiceField(param: _req('arpMode'), label: 'Modo', onChoice: _setChoice, width: 140),
                    if (_p('arpRate') != null)
                      _ParamChoiceField(param: _req('arpRate'), label: 'Rate', onChoice: _setChoice, width: 110),
                    _ParamKnob(param: _req('arpOct'), label: 'Octavas', color: _macro, onSet: _set, onReset: _reset),
                    _ParamKnob(param: _req('arpGate'), label: 'Gate', color: _macro, onSet: _set, onReset: _reset),
                    _ParamKnob(param: _req('arpSwing'), label: 'Swing', color: _macro, onSet: _set, onReset: _reset),
                    _ParamKnob(param: _req('arpSteps'), label: 'Pasos', color: _macro, onSet: _set, onReset: _reset),
                    if (_p('arpLatch') != null)
                      _ParamSwitch(
                        param: _req('arpLatch'),
                        label: 'Latch',
                        color: _macro,
                        onChanged: _setBool,
                      ),
                  ],
                ),
                const SizedBox(height: 16),
                _StepGrid(
                  on: on,
                  steps: [
                    for (var i = 1; i <= 16; i++) _p('arpStep$i'),
                  ],
                  active: isOn,
                  onSet: _set,
                  length: _p('arpSteps')?.value ?? 1,
                ),
                const SizedBox(height: 10),
                Text(
                  'Arp $steps pasos · rate ${_p('arpRate')?.choiceLabel ?? '1/16'}',
                  style: const TextStyle(color: AppColors.textFaint, fontSize: 10),
                ),
              ],
            ),
          ),
        ],
      ),
    );
  }

  // -----------------------------------------------------------------------
  // Perform / macros
  // -----------------------------------------------------------------------
  Widget _performPage(bool compact) {
    final cards = <Widget>[];
    for (var m = 1; m <= 8; m++) {
      cards.add(
        _MacroEditor(
          number: m,
          name: _macroNames[m - 1],
          value: _p('macro$m'),
          routes: [
            for (var r = 1; r <= 4; r++)
              (
                _p('macro${m}r${r}dst'),
                _p('macro${m}r${r}amt'),
              ),
          ],
          color: _macro,
          onSet: _set,
          onChoice: _setChoice,
        ),
      );
    }
    return SingleChildScrollView(
      padding: EdgeInsets.all(compact ? 14 : 20),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          _PageHeading(
            title: 'Macros & Performance',
            subtitle: 'Ocho controles con cuatro rutas cada uno',
            color: _macro,
            trailing: const _EngineNameBadge(label: '8 × 4', color: _macro),
          ),
          const SizedBox(height: 14),
          LayoutBuilder(
            builder: (context, c) {
              final width = c.maxWidth >= 1040 ? (c.maxWidth - 14) / 2 : c.maxWidth;
              return Wrap(
                spacing: 14,
                runSpacing: 14,
                children: [for (final card in cards) SizedBox(width: width, child: card)],
              );
            },
          ),
          const SizedBox(height: 14),
          _SectionTitle('EXPRESIÓN', subtitle: 'MPE, glide y respuesta', color: _mod),
          const SizedBox(height: 10),
          _Card(
            padding: const EdgeInsets.all(16),
            child: Wrap(
              spacing: 20,
              runSpacing: 14,
              crossAxisAlignment: WrapCrossAlignment.center,
              children: [
                if (_p('mpe') != null)
                  _ParamSwitch(
                    param: _req('mpe'),
                    label: 'MPE',
                    color: _mod,
                    onChanged: _setBool,
                  ),
                _ParamKnob(param: _req('mpeBend'), label: 'Bend MPE', color: _mod, onSet: _set, onReset: _reset),
                _ParamKnob(param: _req('mpePress'), label: 'Presión', color: _mod, onSet: _set, onReset: _reset),
                _ParamKnob(param: _req('mpeTimbre'), label: 'Timbre', color: _mod, onSet: _set, onReset: _reset),
                _ParamKnob(param: _req('glide'), label: 'Glide', color: _audio, onSet: _set, onReset: _reset),
                _ParamKnob(param: _req('bend'), label: 'Bend', color: _audio, onSet: _set, onReset: _reset),
                _ParamKnob(param: _req('vel'), label: 'Velocidad', color: _audio, onSet: _set, onReset: _reset),
              ],
            ),
          ),
        ],
      ),
    );
  }

  // -----------------------------------------------------------------------
  // Motion / LINES
  // -----------------------------------------------------------------------
  Widget _motionPage(bool compact) {
    return SingleChildScrollView(
      padding: EdgeInsets.all(compact ? 14 : 20),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          _PageHeading(
            title: 'Motion / LINES',
            subtitle: 'Distorsión polifónica de altura dibujada sobre el piano roll',
            color: _mod,
            trailing: _EngineNameBadge(label: 'LINES', color: _mod),
          ),
          const SizedBox(height: 14),
          _Card(
            padding: const EdgeInsets.all(16),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                _CardHeader(
                  icon: Icons.timeline_rounded,
                  title: 'Curvas de altura',
                  subtitle: 'Cada voz se acerca a la curva según su distancia',
                  color: _mod,
                ),
                const SizedBox(height: 14),
                Wrap(
                  spacing: 20,
                  runSpacing: 12,
                  children: [
                    _ParamKnob(param: _req('warpamt'), label: 'Cantidad', color: _mod, onSet: _set, onReset: _reset),
                    _ParamKnob(param: _req('warpR'), label: 'Rango', color: _mod, onSet: _set, onReset: _reset),
                  ],
                ),
                const SizedBox(height: 14),
                Container(
                  height: 200,
                  decoration: BoxDecoration(
                    color: AppColors.bgDeep,
                    borderRadius: BorderRadius.circular(8),
                    border: Border.all(color: AppColors.border),
                  ),
                  child: CustomPaint(
                    painter: _MotionPreviewPainter(
                      amount: _p('warpamt')?.value ?? 0.8,
                      range: _p('warpR')?.value ?? 0.5,
                      color: _mod,
                    ),
                  ),
                ),
                const SizedBox(height: 12),
                const Text(
                  'Dibuja las áreas en el piano roll. Mientras el playhead cruza una curva, cada nota se desplaza de forma independiente: los acordes se comprimen o se abren sin colapsar a una sola voz.',
                  style: TextStyle(color: AppColors.textDim, fontSize: 11, height: 1.5),
                ),
              ],
            ),
          ),
        ],
      ),
    );
  }

  // -----------------------------------------------------------------------
  // Master
  // -----------------------------------------------------------------------
  Widget _masterPage(bool compact) {
    return SingleChildScrollView(
      padding: EdgeInsets.all(compact ? 14 : 20),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          _PageHeading(
            title: 'Master',
            subtitle: 'Salida, calidad y compatibilidad del instrumento',
            color: _audio,
            trailing: _EngineNameBadge(label: '${_all.length} parámetros', color: _audio),
          ),
          const SizedBox(height: 14),
          _Card(
            padding: const EdgeInsets.all(16),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                _CardHeader(
                  icon: Icons.tune_rounded,
                  title: 'Salida',
                  subtitle: 'Nivel, panorama y ancho estéreo',
                  color: _audio,
                ),
                const SizedBox(height: 14),
                Wrap(
                  spacing: 20,
                  runSpacing: 12,
                  children: [
                    _ParamKnob(param: _req('level'), label: 'Nivel', color: _audio, onSet: _set, onReset: _reset),
                    _ParamKnob(param: _req('pan'), label: 'Pan', color: _audio, onSet: _set, onReset: _reset),
                    _ParamKnob(param: _req('width'), label: 'Ancho', color: _audio, onSet: _set, onReset: _reset),
                    if (_p('limiter') != null)
                      _ParamSwitch(param: _req('limiter'), label: 'Limitador', color: _audio, onChanged: _setBool),
                  ],
                ),
              ],
            ),
          ),
          const SizedBox(height: 14),
          _Card(
            padding: const EdgeInsets.all(16),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                _CardHeader(
                  icon: Icons.memory_rounded,
                  title: 'Motor y calidad',
                  subtitle: 'Compatibilidad con proyectos antiguos',
                  color: AppColors.textDim,
                ),
                const SizedBox(height: 14),
                Wrap(
                  spacing: 20,
                  runSpacing: 12,
                  crossAxisAlignment: WrapCrossAlignment.center,
                  children: [
                    if (_p('quality') != null)
                      _ParamChoiceField(param: _req('quality'), label: 'Calidad', onChoice: _setChoice, width: 150),
                    if (_p('motor') != null)
                      _ParamChoiceField(param: _req('motor'), label: 'Motor', onChoice: _setChoice, width: 150),
                    _ParamKnob(param: _req('glide'), label: 'Glide', color: _audio, onSet: _set, onReset: _reset),
                    _ParamKnob(param: _req('bend'), label: 'Bend', color: _audio, onSet: _set, onReset: _reset),
                  ],
                ),
                const SizedBox(height: 14),
                Container(
                  padding: const EdgeInsets.all(12),
                  decoration: BoxDecoration(
                    color: AppColors.bgDeep,
                    borderRadius: BorderRadius.circular(8),
                    border: Border.all(color: AppColors.border),
                  ),
                  child: Row(
                    children: [
                      const Icon(Icons.verified_rounded, color: _env, size: 18),
                      const SizedBox(width: 10),
                      Expanded(
                        child: Text(
                          'Estado guardado como EZSYNTH3. Los proyectos con EZSYNTH2 se migran al cargar y el motor Piano sigue disponible.',
                          style: TextStyle(color: AppColors.textDim, fontSize: 10, height: 1.45),
                        ),
                      ),
                    ],
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

const _macroNames = [
  'Brillo',
  'Movimiento',
  'Espacio',
  'Mordida',
  'Forma',
  'Ancho',
  'Elevación',
  'Caos',
];

// ===========================================================================
// Shared visual components
// ===========================================================================

class _Card extends StatelessWidget {
  const _Card({
    required this.child,
    this.padding = const EdgeInsets.all(14),
  });

  final Widget child;
  final EdgeInsetsGeometry padding;

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: padding,
      decoration: BoxDecoration(
        color: AppColors.surface,
        borderRadius: BorderRadius.circular(10),
        border: Border.all(color: AppColors.border),
      ),
      child: child,
    );
  }
}

class _SectionTitle extends StatelessWidget {
  const _SectionTitle(
    this.title, {
    this.subtitle,
    this.color = _audio,
    this.trailing,
  });

  final String title;
  final String? subtitle;
  final Color color;
  final Widget? trailing;

  @override
  Widget build(BuildContext context) {
    return Row(
      crossAxisAlignment: CrossAxisAlignment.center,
      children: [
        Container(
          width: 3,
          height: 26,
          decoration: BoxDecoration(
            color: color,
            borderRadius: BorderRadius.circular(2),
          ),
        ),
        const SizedBox(width: 9),
        Expanded(
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text(
                title,
                style: const TextStyle(
                  color: _ink,
                  fontSize: 11,
                  fontWeight: FontWeight.w800,
                  letterSpacing: 1.1,
                ),
              ),
              if (subtitle != null) ...[
                const SizedBox(height: 2),
                Text(
                  subtitle!,
                  style: const TextStyle(
                    color: AppColors.textFaint,
                    fontSize: 10,
                    height: 1.2,
                  ),
                ),
              ],
            ],
          ),
        ),
        if (trailing != null) trailing!,
      ],
    );
  }
}

class _PageHeading extends StatelessWidget {
  const _PageHeading({
    required this.title,
    required this.subtitle,
    required this.color,
    this.trailing,
  });

  final String title;
  final String subtitle;
  final Color color;
  final Widget? trailing;

  @override
  Widget build(BuildContext context) {
    return Row(
      crossAxisAlignment: CrossAxisAlignment.center,
      children: [
        Container(
          width: 5,
          height: 42,
          decoration: BoxDecoration(
            gradient: LinearGradient(
              begin: Alignment.topCenter,
              end: Alignment.bottomCenter,
              colors: [color, color.withValues(alpha: 0.18)],
            ),
            borderRadius: BorderRadius.circular(3),
          ),
        ),
        const SizedBox(width: 12),
        Expanded(
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text(
                title,
                style: const TextStyle(
                  color: _ink,
                  fontSize: 24,
                  fontWeight: FontWeight.w800,
                  letterSpacing: -0.6,
                  height: 1.05,
                ),
              ),
              const SizedBox(height: 4),
              Text(
                subtitle,
                style: const TextStyle(
                  color: AppColors.textDim,
                  fontSize: 11,
                  height: 1.3,
                ),
              ),
            ],
          ),
        ),
        if (trailing != null) trailing!,
      ],
    );
  }
}

class _CardHeader extends StatelessWidget {
  const _CardHeader({
    required this.icon,
    required this.title,
    this.subtitle,
    this.color = _audio,
    this.trailing,
  });

  final IconData icon;
  final String title;
  final String? subtitle;
  final Color color;
  final Widget? trailing;

  @override
  Widget build(BuildContext context) {
    return Row(
      children: [
        Container(
          width: 28,
          height: 28,
          decoration: BoxDecoration(
            color: color.withValues(alpha: 0.14),
            borderRadius: BorderRadius.circular(7),
            border: Border.all(color: color.withValues(alpha: 0.28)),
          ),
          child: Icon(icon, color: color, size: 15),
        ),
        const SizedBox(width: 10),
        Expanded(
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text(
                title,
                style: const TextStyle(
                  color: _ink,
                  fontSize: 13,
                  fontWeight: FontWeight.w700,
                ),
              ),
              if (subtitle != null) ...[
                const SizedBox(height: 1),
                Text(
                  subtitle!,
                  style: const TextStyle(
                    color: AppColors.textFaint,
                    fontSize: 10,
                  ),
                ),
              ],
            ],
          ),
        ),
        if (trailing != null) trailing!,
      ],
    );
  }
}

class _HeaderPill extends StatelessWidget {
  const _HeaderPill({
    required this.icon,
    required this.label,
    required this.color,
    this.onTap,
    this.active = false,
  });

  final IconData icon;
  final String label;
  final Color color;
  final VoidCallback? onTap;
  final bool active;

  @override
  Widget build(BuildContext context) {
    return MouseRegion(
      cursor: onTap == null ? SystemMouseCursors.basic : SystemMouseCursors.click,
      child: GestureDetector(
        onTap: onTap,
        child: Container(
          height: 30,
          padding: const EdgeInsets.symmetric(horizontal: 10),
          decoration: BoxDecoration(
            color: active
                ? color.withValues(alpha: 0.18)
                : AppColors.surfaceAlt,
            borderRadius: BorderRadius.circular(7),
            border: Border.all(
              color: active ? color.withValues(alpha: 0.55) : AppColors.border,
            ),
          ),
          child: Row(
            mainAxisSize: MainAxisSize.min,
            children: [
              Icon(icon, size: 13, color: active ? color : AppColors.textDim),
              const SizedBox(width: 5),
              Text(
                label,
                style: TextStyle(
                  color: active ? color : AppColors.textDim,
                  fontSize: 10,
                  fontWeight: FontWeight.w600,
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }
}

class _PresetButton extends StatelessWidget {
  const _PresetButton({
    required this.category,
    required this.name,
    required this.onTap,
  });

  final String category;
  final String name;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    return MouseRegion(
      cursor: SystemMouseCursors.click,
      child: GestureDetector(
        onTap: onTap,
        child: Container(
          height: 38,
          constraints: const BoxConstraints(minWidth: 156, maxWidth: 246),
          padding: const EdgeInsets.symmetric(horizontal: 11),
          decoration: BoxDecoration(
            color: AppColors.bgDeep,
            borderRadius: BorderRadius.circular(8),
            border: Border.all(color: AppColors.borderStrong),
          ),
          child: Row(
            children: [
              Container(
                width: 6,
                height: 18,
                decoration: BoxDecoration(
                  color: _macro,
                  borderRadius: BorderRadius.circular(3),
                ),
              ),
              const SizedBox(width: 9),
              Expanded(
                child: Column(
                  mainAxisAlignment: MainAxisAlignment.center,
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(
                      category.toUpperCase(),
                      style: const TextStyle(
                        color: _macro,
                        fontSize: 8,
                        fontWeight: FontWeight.w700,
                        letterSpacing: 0.8,
                      ),
                    ),
                    Text(
                      name,
                      maxLines: 1,
                      overflow: TextOverflow.ellipsis,
                      style: const TextStyle(
                        color: _ink,
                        fontSize: 11,
                        fontWeight: FontWeight.w600,
                      ),
                    ),
                  ],
                ),
              ),
              const Icon(
                Icons.unfold_more_rounded,
                size: 15,
                color: AppColors.textFaint,
              ),
            ],
          ),
        ),
      ),
    );
  }
}

class _EngineNameBadge extends StatelessWidget {
  const _EngineNameBadge({required this.label, required this.color});
  final String label;
  final Color color;

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 5),
      decoration: BoxDecoration(
        color: color.withValues(alpha: 0.12),
        borderRadius: BorderRadius.circular(6),
        border: Border.all(color: color.withValues(alpha: 0.3)),
      ),
      child: Text(
        label,
        style: TextStyle(
          color: color,
          fontSize: 9,
          fontWeight: FontWeight.w700,
          letterSpacing: 0.5,
        ),
      ),
    );
  }
}

class _RailSection extends StatelessWidget {
  const _RailSection(this.title, this.children);
  final String title;
  final List<Widget> children;

  @override
  Widget build(BuildContext context) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        Padding(
          padding: const EdgeInsets.only(left: 7, bottom: 5),
          child: Text(
            title,
            style: const TextStyle(
              color: AppColors.textFaint,
              fontSize: 8,
              fontWeight: FontWeight.w800,
              letterSpacing: 1.3,
            ),
          ),
        ),
        ...children,
      ],
    );
  }
}

class _RailItem extends StatefulWidget {
  const _RailItem({
    required this.icon,
    required this.label,
    required this.color,
    required this.onTap,
    this.selected = false,
    this.badge,
  });

  final IconData icon;
  final String label;
  final Color color;
  final VoidCallback onTap;
  final bool selected;
  final String? badge;

  @override
  State<_RailItem> createState() => _RailItemState();
}

class _RailItemState extends State<_RailItem> {
  bool _hover = false;

  @override
  Widget build(BuildContext context) {
    final selected = widget.selected;
    return MouseRegion(
      cursor: SystemMouseCursors.click,
      onEnter: (_) => setState(() => _hover = true),
      onExit: (_) => setState(() => _hover = false),
      child: GestureDetector(
        onTap: widget.onTap,
        child: Container(
          margin: const EdgeInsets.only(bottom: 2),
          height: 31,
          decoration: BoxDecoration(
            color: selected
                ? widget.color.withValues(alpha: 0.14)
                : _hover
                    ? AppColors.surfaceAlt
                    : Colors.transparent,
            borderRadius: BorderRadius.circular(6),
            border: Border(
              left: BorderSide(
                width: 2,
                color: selected ? widget.color : Colors.transparent,
              ),
            ),
          ),
          padding: const EdgeInsets.symmetric(horizontal: 7),
          child: Row(
            children: [
              Icon(
                widget.icon,
                size: 14,
                color: selected
                    ? widget.color
                    : _hover
                        ? AppColors.textDim
                        : AppColors.textFaint,
              ),
              const SizedBox(width: 8),
              Expanded(
                child: Text(
                  widget.label,
                  maxLines: 1,
                  overflow: TextOverflow.ellipsis,
                  style: TextStyle(
                    color: selected ? _ink : AppColors.textDim,
                    fontSize: 10.5,
                    fontWeight: selected ? FontWeight.w700 : FontWeight.w500,
                  ),
                ),
              ),
              if (widget.badge != null && widget.badge!.isNotEmpty)
                Flexible(
                  child: Container(
                    padding: const EdgeInsets.symmetric(
                      horizontal: 4,
                      vertical: 1,
                    ),
                    decoration: BoxDecoration(
                      color: selected
                          ? widget.color.withValues(alpha: 0.18)
                          : AppColors.bgDeep,
                      borderRadius: BorderRadius.circular(3),
                    ),
                    child: Text(
                      widget.badge!,
                      maxLines: 1,
                      overflow: TextOverflow.ellipsis,
                      style: TextStyle(
                        color: selected ? widget.color : AppColors.textFaint,
                        fontSize: 7.5,
                        fontWeight: FontWeight.w600,
                      ),
                    ),
                  ),
                ),
            ],
          ),
        ),
      ),
    );
  }
}

class _MacroFader extends StatelessWidget {
  const _MacroFader({
    required this.label,
    required this.sublabel,
    required this.value,
    required this.color,
    required this.onChanged,
    required this.onReset,
  });

  final String label;
  final String sublabel;
  final double value;
  final Color color;
  final ValueChanged<double> onChanged;
  final VoidCallback onReset;

  @override
  Widget build(BuildContext context) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        Row(
          children: [
            Text(
              label,
              style: const TextStyle(
                color: _ink,
                fontSize: 10,
                fontWeight: FontWeight.w600,
              ),
            ),
            const Spacer(),
            Text(
              '${(value * 100).round()}',
              style: TextStyle(
                color: color,
                fontSize: 9,
                fontWeight: FontWeight.w700,
              ),
            ),
          ],
        ),
        const SizedBox(height: 1),
        Text(
          sublabel,
          maxLines: 1,
          overflow: TextOverflow.ellipsis,
          style: const TextStyle(color: AppColors.textFaint, fontSize: 8),
        ),
        SizedBox(
          height: 18,
          child: SliderTheme(
            data: SliderTheme.of(context).copyWith(
              trackHeight: 3,
              thumbShape: const RoundSliderThumbShape(enabledThumbRadius: 5),
              overlayShape: const RoundSliderOverlayShape(overlayRadius: 10),
              activeTrackColor: color,
              inactiveTrackColor: AppColors.bgDeep,
              thumbColor: color,
              overlayColor: color.withValues(alpha: 0.18),
            ),
            child: Slider(
              value: value.clamp(0.0, 1.0),
              onChanged: onChanged,
            ),
          ),
        ),
      ],
    );
  }
}

class _MeterRow extends StatelessWidget {
  const _MeterRow({
    required this.label,
    required this.value,
    required this.color,
  });

  final String label;
  final double value;
  final Color color;

  @override
  Widget build(BuildContext context) {
    final v = value.clamp(0.0, 1.0);
    return Row(
      children: [
        SizedBox(
          width: 28,
          child: Text(
            label,
            style: const TextStyle(
              color: AppColors.textFaint,
              fontSize: 8,
              fontWeight: FontWeight.w700,
            ),
          ),
        ),
        Expanded(
          child: ClipRRect(
            borderRadius: BorderRadius.circular(3),
            child: Container(
              height: 5,
              color: AppColors.bgDeep,
              child: FractionallySizedBox(
                alignment: Alignment.centerLeft,
                widthFactor: v,
                child: Container(color: color),
              ),
            ),
          ),
        ),
        const SizedBox(width: 6),
        SizedBox(
          width: 24,
          child: Text(
            '${(v * 100).round()}',
            textAlign: TextAlign.right,
            style: const TextStyle(color: AppColors.textFaint, fontSize: 8),
          ),
        ),
      ],
    );
  }
}

class _LegendDot extends StatelessWidget {
  const _LegendDot({required this.color, required this.label});
  final Color color;
  final String label;

  @override
  Widget build(BuildContext context) {
    return Row(
      mainAxisSize: MainAxisSize.min,
      children: [
        Container(
          width: 5,
          height: 5,
          decoration: BoxDecoration(color: color, shape: BoxShape.circle),
        ),
        const SizedBox(width: 4),
        Text(
          label,
          style: const TextStyle(color: AppColors.textFaint, fontSize: 8.5),
        ),
      ],
    );
  }
}

class _TinyReadout extends StatelessWidget {
  const _TinyReadout({required this.label, required this.value});
  final String label;
  final String value;

  @override
  Widget build(BuildContext context) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.end,
      children: [
        Text(
          label,
          style: const TextStyle(
            color: AppColors.textFaint,
            fontSize: 7,
            fontWeight: FontWeight.w700,
            letterSpacing: 0.5,
          ),
        ),
        Text(
          value,
          style: const TextStyle(
            color: AppColors.textDim,
            fontSize: 10,
            fontWeight: FontWeight.w600,
          ),
        ),
      ],
    );
  }
}

// ===========================================================================
// Parameter controls
// ===========================================================================

typedef _SetParam = void Function(EzParam param, double value);
typedef _ResetParam = void Function(EzParam param);
typedef _SetChoice = void Function(EzParam param, int index);
typedef _SetBool = void Function(EzParam param, bool value);

class _ParamKnob extends StatelessWidget {
  const _ParamKnob({
    required this.param,
    required this.color,
    required this.onSet,
    required this.onReset,
    this.label,
    this.size = 58,
  });

  final EzParam param;
  final Color color;
  final _SetParam onSet;
  final _ResetParam onReset;
  final String? label;
  final double size;

  @override
  Widget build(BuildContext context) {
    final caption = label ?? param.shortName;
    final valueText = param.isChoice ? param.choiceLabel : param.text;
    return Tooltip(
      message: '${param.name}\n${param.text}\nDoble clic para reiniciar',
      waitDuration: const Duration(milliseconds: 500),
      child: SizedBox(
        width: size + 20,
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            _KnobControl(
              value: param.value,
              color: color,
              size: size,
              caption: valueText,
              onChanged: (v) => onSet(param, v),
              onReset: () => onReset(param),
            ),
            const SizedBox(height: 4),
            Text(
              caption,
              maxLines: 1,
              overflow: TextOverflow.ellipsis,
              textAlign: TextAlign.center,
              style: const TextStyle(
                color: AppColors.textDim,
                fontSize: 9,
                fontWeight: FontWeight.w600,
              ),
            ),
          ],
        ),
      ),
    );
  }
}

class _KnobControl extends StatefulWidget {
  const _KnobControl({
    required this.value,
    required this.color,
    required this.size,
    required this.caption,
    required this.onChanged,
    required this.onReset,
  });

  final double value;
  final Color color;
  final double size;
  final String caption;
  final ValueChanged<double> onChanged;
  final VoidCallback onReset;

  @override
  State<_KnobControl> createState() => _KnobControlState();
}

class _KnobControlState extends State<_KnobControl> {
  double _dragValue = 0;

  void _start(DragStartDetails details) => _dragValue = widget.value;

  void _update(DragUpdateDetails details) {
    _dragValue = (_dragValue - details.delta.dy / 180).clamp(0.0, 1.0);
    widget.onChanged(_dragValue);
  }

  @override
  Widget build(BuildContext context) {
    return MouseRegion(
      cursor: SystemMouseCursors.resizeUpDown,
      child: GestureDetector(
        behavior: HitTestBehavior.opaque,
        onVerticalDragStart: _start,
        onVerticalDragUpdate: _update,
        onDoubleTap: widget.onReset,
        child: SizedBox(
          width: widget.size,
          height: widget.size,
          child: Stack(
            alignment: Alignment.center,
            children: [
              CustomPaint(
                size: Size.square(widget.size),
                painter: _KnobPainter(
                  value: widget.value,
                  color: widget.color,
                ),
              ),
              Padding(
                padding: EdgeInsets.symmetric(horizontal: widget.size * 0.12),
                child: Text(
                  widget.caption,
                  maxLines: 2,
                  overflow: TextOverflow.ellipsis,
                  textAlign: TextAlign.center,
                  style: TextStyle(
                    color: _ink,
                    fontSize: widget.size < 54 ? 8 : 9,
                    height: 1.1,
                    fontWeight: FontWeight.w700,
                  ),
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }
}

class _KnobPainter extends CustomPainter {
  _KnobPainter({required this.value, required this.color});
  final double value;
  final Color color;

  @override
  void paint(Canvas canvas, Size size) {
    final center = size.center(Offset.zero);
    final radius = size.width / 2 - 5;
    final rect = Rect.fromCircle(center: center, radius: radius);
    const start = math.pi * 0.75;
    const sweep = math.pi * 1.5;
    final track = Paint()
      ..style = PaintingStyle.stroke
      ..strokeWidth = 4
      ..strokeCap = StrokeCap.round
      ..color = AppColors.bgDeep;
    canvas.drawArc(rect, start, sweep, false, track);
    final active = Paint()
      ..style = PaintingStyle.stroke
      ..strokeWidth = 4
      ..strokeCap = StrokeCap.round
      ..shader = SweepGradient(
        startAngle: start,
        endAngle: start + sweep,
        colors: [
          color.withValues(alpha: 0.35),
          color,
        ],
      ).createShader(rect);
    canvas.drawArc(rect, start, sweep * value.clamp(0.0, 1.0), false, active);

    // Inner well gives the control a physical feel without a photographic
    // background: a dark disc, a faint rim and a pointer line.
    canvas.drawCircle(
      center,
      radius - 8,
      Paint()..color = AppColors.surfaceAlt,
    );
    canvas.drawCircle(
      center,
      radius - 8,
      Paint()
        ..style = PaintingStyle.stroke
        ..strokeWidth = 1
        ..color = AppColors.border,
    );
    final angle = start + sweep * value.clamp(0.0, 1.0);
    final inner = radius - 12;
    final outer = radius - 5;
    canvas.drawLine(
      center + Offset(math.cos(angle), math.sin(angle)) * inner,
      center + Offset(math.cos(angle), math.sin(angle)) * outer,
      Paint()
        ..color = color
        ..strokeWidth = 2
        ..strokeCap = StrokeCap.round,
    );
  }

  @override
  bool shouldRepaint(covariant _KnobPainter oldDelegate) =>
      oldDelegate.value != value || oldDelegate.color != color;
}

class _ParamChoiceField extends StatelessWidget {
  const _ParamChoiceField({
    required this.param,
    required this.onChoice,
    this.label,
    this.width = 150,
  });

  final EzParam param;
  final _SetChoice onChoice;
  final String? label;
  final double width;

  @override
  Widget build(BuildContext context) {
    final labels = param.labels.isNotEmpty
        ? param.labels
        : [param.text.isEmpty ? '—' : param.text];
    final value = param.choiceIndex.clamp(0, labels.length - 1);
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      mainAxisSize: MainAxisSize.min,
      children: [
        if (label != null) ...[
          Text(
            label!,
            style: const TextStyle(
              color: AppColors.textFaint,
              fontSize: 9,
              fontWeight: FontWeight.w600,
            ),
          ),
          const SizedBox(height: 4),
        ],
        SizedBox(
          width: width,
          height: 31,
          child: Container(
            padding: const EdgeInsets.symmetric(horizontal: 9),
            decoration: BoxDecoration(
              color: AppColors.bgDeep,
              borderRadius: BorderRadius.circular(6),
              border: Border.all(color: AppColors.border),
            ),
            child: DropdownButtonHideUnderline(
              child: DropdownButton<int>(
                value: value,
                isExpanded: true,
                dropdownColor: AppColors.surfaceHigh,
                borderRadius: BorderRadius.circular(8),
                icon: const Icon(
                  Icons.expand_more_rounded,
                  size: 16,
                  color: AppColors.textFaint,
                ),
                style: const TextStyle(
                  color: _ink,
                  fontSize: 10,
                  fontWeight: FontWeight.w600,
                ),
                items: [
                  for (var i = 0; i < labels.length; i++)
                    DropdownMenuItem<int>(
                      value: i,
                      child: Text(
                        labels[i],
                        maxLines: 1,
                        overflow: TextOverflow.ellipsis,
                      ),
                    ),
                ],
                onChanged: (i) {
                  if (i != null) onChoice(param, i);
                },
              ),
            ),
          ),
        ),
      ],
    );
  }
}

class _ParamSwitch extends StatelessWidget {
  const _ParamSwitch({
    required this.param,
    required this.label,
    required this.color,
    required this.onChanged,
  });

  final EzParam param;
  final String label;
  final Color color;
  final _SetBool onChanged;

  @override
  Widget build(BuildContext context) {
    final on = param.value > 0.5;
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      mainAxisSize: MainAxisSize.min,
      children: [
        Text(
          label,
          style: const TextStyle(
            color: AppColors.textFaint,
            fontSize: 9,
            fontWeight: FontWeight.w600,
          ),
        ),
        const SizedBox(height: 4),
        MouseRegion(
          cursor: SystemMouseCursors.click,
          child: GestureDetector(
            onTap: () => onChanged(param, !on),
            child: AnimatedContainer(
              duration: const Duration(milliseconds: 150),
              width: 62,
              height: 31,
              padding: const EdgeInsets.all(3),
              decoration: BoxDecoration(
                color: on
                    ? color.withValues(alpha: 0.2)
                    : AppColors.bgDeep,
                borderRadius: BorderRadius.circular(6),
                border: Border.all(
                  color: on ? color.withValues(alpha: 0.55) : AppColors.border,
                ),
              ),
              child: Stack(
                children: [
                  AnimatedAlign(
                    duration: const Duration(milliseconds: 150),
                    curve: Curves.easeOutCubic,
                    alignment: on ? Alignment.centerRight : Alignment.centerLeft,
                    child: Container(
                      width: 25,
                      height: 25,
                      decoration: BoxDecoration(
                        color: on ? color : AppColors.surfaceHigh,
                        borderRadius: BorderRadius.circular(4),
                      ),
                      child: Icon(
                        on ? Icons.check_rounded : Icons.remove_rounded,
                        size: 14,
                        color: on ? AppColors.bgDeep : AppColors.textFaint,
                      ),
                    ),
                  ),
                ],
              ),
            ),
          ),
        ),
      ],
    );
  }
}

class _AmountSlider extends StatelessWidget {
  const _AmountSlider({
    required this.param,
    required this.color,
    required this.onSet,
    this.compact = false,
  });

  final EzParam param;
  final Color color;
  final _SetParam onSet;
  final bool compact;

  @override
  Widget build(BuildContext context) {
    return SizedBox(
      height: compact ? 24 : 34,
      child: Stack(
        alignment: Alignment.center,
        children: [
          Positioned.fill(
            child: SliderTheme(
              data: SliderTheme.of(context).copyWith(
                trackHeight: 3,
                activeTrackColor: color,
                inactiveTrackColor: AppColors.bgDeep,
                thumbColor: color,
                thumbShape: RoundSliderThumbShape(
                  enabledThumbRadius: compact ? 5 : 6,
                ),
                overlayShape: RoundSliderOverlayShape(
                  overlayRadius: compact ? 10 : 13,
                ),
                overlayColor: color.withValues(alpha: 0.16),
              ),
              child: Slider(
                value: param.value.clamp(0.0, 1.0),
                onChanged: (v) => onSet(param, v),
              ),
            ),
          ),
          IgnorePointer(
            child: Container(
              width: 1,
              height: 9,
              color: AppColors.textFaint.withValues(alpha: 0.5),
            ),
          ),
        ],
      ),
    );
  }
}

class _EngineSelector extends StatelessWidget {
  const _EngineSelector({
    required this.param,
    required this.accent,
    required this.onSelect,
    this.choices,
  });

  final EzParam param;
  final Color accent;
  final ValueChanged<int> onSelect;
  final List<String>? choices;

  static const _engineIcons = [
    Icons.power_settings_new_rounded,
    Icons.show_chart_rounded,
    Icons.waves_rounded,
    Icons.blur_on_rounded,
    Icons.grain_rounded,
  ];

  static const _auxIcons = [
    Icons.block_rounded,
    Icons.graphic_eq_rounded,
    Icons.circle_outlined,
    Icons.touch_app_rounded,
    Icons.grain_rounded,
  ];

  @override
  Widget build(BuildContext context) {
    final labels = choices ??
        (param.labels.isNotEmpty
            ? param.labels
            : const ['Off', 'Analog', 'Wavetable', 'FM', 'Sample']);
    final icons = choices == null ? _engineIcons : _auxIcons;
    final selected = param.choiceIndex.clamp(0, labels.length - 1);
    return Container(
      height: 42,
      padding: const EdgeInsets.all(3),
      decoration: BoxDecoration(
        color: AppColors.bgDeep,
        borderRadius: BorderRadius.circular(8),
        border: Border.all(color: AppColors.border),
      ),
      child: Row(
        children: [
          for (var i = 0; i < labels.length; i++)
            Expanded(
              child: MouseRegion(
                cursor: SystemMouseCursors.click,
                child: GestureDetector(
                  onTap: () => onSelect(i),
                  child: AnimatedContainer(
                    duration: const Duration(milliseconds: 140),
                    height: double.infinity,
                    margin: const EdgeInsets.symmetric(horizontal: 1),
                    decoration: BoxDecoration(
                      color: selected == i
                          ? accent.withValues(alpha: 0.17)
                          : Colors.transparent,
                      borderRadius: BorderRadius.circular(5),
                      border: Border.all(
                        color: selected == i
                            ? accent.withValues(alpha: 0.5)
                            : Colors.transparent,
                      ),
                    ),
                    child: Row(
                      mainAxisAlignment: MainAxisAlignment.center,
                      children: [
                        Icon(
                          icons[i % icons.length],
                          size: 13,
                          color: selected == i ? accent : AppColors.textFaint,
                        ),
                        const SizedBox(width: 5),
                        Flexible(
                          child: Text(
                            labels[i],
                            maxLines: 1,
                            overflow: TextOverflow.ellipsis,
                            style: TextStyle(
                              color: selected == i ? accent : AppColors.textDim,
                              fontSize: 9.5,
                              fontWeight: selected == i
                                  ? FontWeight.w700
                                  : FontWeight.w500,
                            ),
                          ),
                        ),
                      ],
                    ),
                  ),
                ),
              ),
            ),
        ],
      ),
    );
  }
}

// ===========================================================================
// Composite cards
// ===========================================================================

extension _StudioHelpers on _EzSynthStudioState {
  Widget _overviewKnob(String id, String label, Color color) {
    final p = _p(id);
    if (p == null) return const SizedBox.shrink();
    return _ParamKnob(
      param: p,
      label: label,
      color: color,
      onSet: _set,
      onReset: _reset,
    );
  }
}

class _HeroCard extends StatelessWidget {
  const _HeroCard({
    required this.morph,
    required this.morphMode,
    required this.layerA,
    required this.layerB,
    required this.onSetMorph,
    required this.onResetMorph,
    required this.onSetMode,
  });

  final EzParam morph;
  final EzParam? morphMode;
  final String layerA;
  final String layerB;
  final ValueChanged<double> onSetMorph;
  final VoidCallback onResetMorph;
  final ValueChanged<int> onSetMode;

  @override
  Widget build(BuildContext context) {
    return Container(
      decoration: BoxDecoration(
        gradient: const LinearGradient(
          begin: Alignment.topLeft,
          end: Alignment.bottomRight,
          colors: [Color(0xFF172536), Color(0xFF11151B)],
        ),
        borderRadius: BorderRadius.circular(12),
        border: Border.all(color: const Color(0xFF2C435B)),
      ),
      child: Stack(
        children: [
          Positioned(
            right: -46,
            top: -58,
            child: Container(
              width: 210,
              height: 210,
              decoration: BoxDecoration(
                shape: BoxShape.circle,
                gradient: RadialGradient(
                  colors: [
                    _mod.withValues(alpha: 0.12),
                    Colors.transparent,
                  ],
                ),
              ),
            ),
          ),
          Positioned(
            left: 120,
            bottom: -80,
            child: Container(
              width: 220,
              height: 150,
              decoration: BoxDecoration(
                shape: BoxShape.circle,
                gradient: RadialGradient(
                  colors: [
                    _audio.withValues(alpha: 0.1),
                    Colors.transparent,
                  ],
                ),
              ),
            ),
          ),
          Padding(
            padding: const EdgeInsets.all(20),
            child: LayoutBuilder(
              builder: (context, c) {
                final compact = c.maxWidth < 720;
                final left = Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  mainAxisSize: MainAxisSize.min,
                  children: [
                    const Text(
                      'HYBRID PERFORMANCE',
                      style: TextStyle(
                        color: _audio,
                        fontSize: 9,
                        fontWeight: FontWeight.w800,
                        letterSpacing: 1.8,
                      ),
                    ),
                    const SizedBox(height: 7),
                    const Text(
                      'Un gesto, dos mundos',
                      style: TextStyle(
                        color: _ink,
                        fontSize: 26,
                        fontWeight: FontWeight.w800,
                        letterSpacing: -0.9,
                        height: 1.0,
                      ),
                    ),
                    const SizedBox(height: 7),
                    const Text(
                      'Mueve el morph para mezclar las capas A y B sin perder detalle.',
                      style: TextStyle(
                        color: AppColors.textDim,
                        fontSize: 11,
                        height: 1.4,
                      ),
                    ),
                    const SizedBox(height: 16),
                    Row(
                      mainAxisSize: MainAxisSize.min,
                      children: [
                        _LayerChip(label: 'A', engine: layerA, color: _audio),
                        const Padding(
                          padding: EdgeInsets.symmetric(horizontal: 7),
                          child: Icon(
                            Icons.swap_horiz_rounded,
                            size: 16,
                            color: AppColors.textFaint,
                          ),
                        ),
                        _LayerChip(label: 'B', engine: layerB, color: _mod),
                      ],
                    ),
                  ],
                );
                final knob = Column(
                  mainAxisSize: MainAxisSize.min,
                  children: [
                    _ParamKnob(
                      param: morph,
                      size: 116,
                      color: _mod,
                      label: 'MORPH A ↔ B',
                      onSet: (_, v) => onSetMorph(v),
                      onReset: (_) => onResetMorph(),
                    ),
                    if (morphMode != null) ...[
                      const SizedBox(height: 8),
                      _ParamChoiceField(
                        param: morphMode!,
                        label: 'Modo',
                        width: 136,
                        onChoice: (_, i) => onSetMode(i),
                      ),
                    ],
                  ],
                );
                if (compact) {
                  return Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      left,
                      const SizedBox(height: 18),
                      Center(child: knob),
                    ],
                  );
                }
                return Row(
                  crossAxisAlignment: CrossAxisAlignment.center,
                  children: [
                    Expanded(child: left),
                    const SizedBox(width: 18),
                    knob,
                  ],
                );
              },
            ),
          ),
        ],
      ),
    );
  }
}

class _LayerChip extends StatelessWidget {
  const _LayerChip({
    required this.label,
    required this.engine,
    required this.color,
  });

  final String label;
  final String engine;
  final Color color;

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 9, vertical: 6),
      decoration: BoxDecoration(
        color: color.withValues(alpha: 0.1),
        borderRadius: BorderRadius.circular(6),
        border: Border.all(color: color.withValues(alpha: 0.3)),
      ),
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          Text(
            label,
            style: TextStyle(
              color: color,
              fontSize: 10,
              fontWeight: FontWeight.w800,
            ),
          ),
          const SizedBox(width: 6),
          Text(
            engine,
            style: const TextStyle(color: AppColors.textDim, fontSize: 9),
          ),
        ],
      ),
    );
  }
}

class _LayerSummaryCard extends StatelessWidget {
  const _LayerSummaryCard({
    required this.title,
    required this.subtitle,
    required this.color,
    required this.level,
    required this.pan,
    required this.tune,
    required this.cutoff,
    required this.onOpen,
    required this.onLevel,
  });

  final String title;
  final String subtitle;
  final Color color;
  final EzParam? level;
  final EzParam? pan;
  final EzParam? tune;
  final EzParam? cutoff;
  final VoidCallback onOpen;
  final ValueChanged<double> onLevel;

  @override
  Widget build(BuildContext context) {
    return MouseRegion(
      cursor: SystemMouseCursors.click,
      child: GestureDetector(
        onTap: onOpen,
        child: Container(
          decoration: BoxDecoration(
            color: AppColors.surface,
            borderRadius: BorderRadius.circular(10),
            border: Border.all(color: AppColors.border),
          ),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              Container(
                height: 3,
                decoration: BoxDecoration(
                  color: color,
                  borderRadius: const BorderRadius.vertical(
                    top: Radius.circular(9),
                  ),
                ),
              ),
              Padding(
                padding: const EdgeInsets.fromLTRB(13, 11, 13, 12),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Row(
                      children: [
                        Text(
                          title,
                          style: TextStyle(
                            color: color,
                            fontSize: 9,
                            fontWeight: FontWeight.w800,
                            letterSpacing: 1.1,
                          ),
                        ),
                        const Spacer(),
                        const Icon(
                          Icons.open_in_new_rounded,
                          size: 12,
                          color: AppColors.textFaint,
                        ),
                      ],
                    ),
                    const SizedBox(height: 3),
                    Text(
                      subtitle.isEmpty ? 'Off' : subtitle,
                      maxLines: 1,
                      overflow: TextOverflow.ellipsis,
                      style: const TextStyle(
                        color: _ink,
                        fontSize: 15,
                        fontWeight: FontWeight.w700,
                      ),
                    ),
                    const SizedBox(height: 10),
                    Container(
                      height: 42,
                      decoration: BoxDecoration(
                        color: AppColors.bgDeep,
                        borderRadius: BorderRadius.circular(6),
                      ),
                      child: CustomPaint(
                        painter: _DecorWavePainter(
                          color: color,
                          seed: title.hashCode,
                        ),
                      ),
                    ),
                    const SizedBox(height: 10),
                    Row(
                      children: [
                        Expanded(
                          child: _MiniStat(
                            label: 'PAN',
                            value: pan?.text ?? 'C',
                          ),
                        ),
                        Expanded(
                          child: _MiniStat(
                            label: 'TONO',
                            value: tune?.text ?? '0',
                          ),
                        ),
                        Expanded(
                          child: _MiniStat(
                            label: 'CORTE',
                            value: cutoff?.text ?? '—',
                          ),
                        ),
                      ],
                    ),
                    if (level != null) ...[
                      const SizedBox(height: 9),
                      Row(
                        children: [
                          const Text(
                            'NIVEL',
                            style: TextStyle(
                              color: AppColors.textFaint,
                              fontSize: 8,
                              fontWeight: FontWeight.w700,
                            ),
                          ),
                          Expanded(
                            child: SliderTheme(
                              data: SliderTheme.of(context).copyWith(
                                trackHeight: 3,
                                activeTrackColor: color,
                                inactiveTrackColor: AppColors.bgDeep,
                                thumbColor: color,
                                thumbShape: const RoundSliderThumbShape(
                                  enabledThumbRadius: 5,
                                ),
                                overlayShape: const RoundSliderOverlayShape(
                                  overlayRadius: 10,
                                ),
                                overlayColor: color.withValues(alpha: 0.16),
                              ),
                              child: Slider(
                                value: level!.value.clamp(0.0, 1.0),
                                onChanged: onLevel,
                              ),
                            ),
                          ),
                          SizedBox(
                            width: 28,
                            child: Text(
                              '${(level!.value * 100).round()}',
                              textAlign: TextAlign.right,
                              style: const TextStyle(
                                color: AppColors.textFaint,
                                fontSize: 8,
                              ),
                            ),
                          ),
                        ],
                      ),
                    ],
                  ],
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }
}

class _MiniStat extends StatelessWidget {
  const _MiniStat({required this.label, required this.value});
  final String label;
  final String value;

  @override
  Widget build(BuildContext context) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Text(
          label,
          style: const TextStyle(
            color: AppColors.textFaint,
            fontSize: 7,
            fontWeight: FontWeight.w700,
            letterSpacing: 0.5,
          ),
        ),
        const SizedBox(height: 1),
        Text(
          value,
          maxLines: 1,
          overflow: TextOverflow.ellipsis,
          style: const TextStyle(
            color: AppColors.textDim,
            fontSize: 9.5,
            fontWeight: FontWeight.w600,
          ),
        ),
      ],
    );
  }
}

class _EnginePreview extends StatelessWidget {
  const _EnginePreview({
    required this.engine,
    required this.accent,
    this.wave,
    this.wtPos,
    this.fmRatio,
    this.fmIndex,
    this.grainSize,
    this.grainDensity,
  });

  final int engine;
  final Color accent;
  final EzParam? wave;
  final EzParam? wtPos;
  final EzParam? fmRatio;
  final EzParam? fmIndex;
  final EzParam? grainSize;
  final EzParam? grainDensity;

  String get _name {
    switch (engine) {
      case 1:
        return 'Analog';
      case 2:
        return 'Wavetable';
      case 3:
        return 'FM';
      case 4:
        return 'Sample / Grain';
      default:
        return 'Off';
    }
  }

  @override
  Widget build(BuildContext context) {
    return Container(
      height: 120,
      decoration: BoxDecoration(
        color: AppColors.bgDeep,
        borderRadius: BorderRadius.circular(9),
        border: Border.all(color: AppColors.border),
      ),
      child: Row(
        children: [
          Expanded(
            child: ClipRRect(
              borderRadius: const BorderRadius.horizontal(
                left: Radius.circular(8),
              ),
              child: CustomPaint(
                painter: _EngineWavePainter(
                  engine: engine,
                  color: accent,
                  wave: wave?.choiceIndex ?? 2,
                  wtPos: wtPos?.value ?? 0.4,
                  fmRatio: fmRatio?.value ?? 0.5,
                  fmIndex: fmIndex?.value ?? 0.2,
                  grainSize: grainSize?.value ?? 0.3,
                  grainDensity: grainDensity?.value ?? 0.3,
                ),
              ),
            ),
          ),
          Container(width: 1, color: AppColors.border),
          SizedBox(
            width: 190,
            child: Padding(
              padding: const EdgeInsets.all(13),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                mainAxisAlignment: MainAxisAlignment.center,
                children: [
                  Text(
                    _name.toUpperCase(),
                    style: TextStyle(
                      color: accent,
                      fontSize: 8,
                      fontWeight: FontWeight.w800,
                      letterSpacing: 1.1,
                    ),
                  ),
                  const SizedBox(height: 5),
                  Text(
                    switch (engine) {
                      1 => 'Tres osciladores, sub y ruido',
                      2 => 'Tablas con posición y warp',
                      3 => 'Operadores con feedback',
                      4 => 'Granos y textura dinámica',
                      _ => 'Selecciona un motor',
                    },
                    style: const TextStyle(
                      color: AppColors.textDim,
                      fontSize: 10,
                      height: 1.35,
                    ),
                  ),
                  const SizedBox(height: 7),
                  Row(
                    children: [
                      _TinyReadout(
                        label: 'FUENTE',
                        value: wave?.choiceLabel ?? '—',
                      ),
                      const Spacer(),
                      _TinyReadout(
                        label: 'PERFIL',
                        value: switch (engine) {
                          1 => 'VA',
                          2 => 'WT',
                          3 => 'FM',
                          4 => 'GR',
                          _ => '—',
                        },
                      ),
                    ],
                  ),
                ],
              ),
            ),
          ),
        ],
      ),
    );
  }
}

class _OscCard extends StatelessWidget {
  const _OscCard({
    required this.number,
    required this.accent,
    required this.wave,
    required this.octave,
    required this.semi,
    required this.detune,
    required this.level,
    required this.onSet,
    required this.onSetChoice,
    required this.onReset,
  });

  final int number;
  final Color accent;
  final EzParam? wave;
  final EzParam? octave;
  final EzParam? semi;
  final EzParam? detune;
  final EzParam? level;
  final _SetParam onSet;
  final _SetChoice onSetChoice;
  final _ResetParam onReset;

  @override
  Widget build(BuildContext context) {
    return _Card(
      padding: const EdgeInsets.all(13),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              Container(
                width: 22,
                height: 22,
                decoration: BoxDecoration(
                  color: accent.withValues(alpha: 0.14),
                  borderRadius: BorderRadius.circular(5),
                  border: Border.all(color: accent.withValues(alpha: 0.3)),
                ),
                child: Center(
                  child: Text(
                    '$number',
                    style: TextStyle(
                      color: accent,
                      fontSize: 10,
                      fontWeight: FontWeight.w800,
                    ),
                  ),
                ),
              ),
              const SizedBox(width: 8),
              const Text(
                'Oscilador',
                style: TextStyle(
                  color: _ink,
                  fontSize: 11,
                  fontWeight: FontWeight.w700,
                ),
              ),
              const Spacer(),
              Container(
                width: 44,
                height: 25,
                decoration: BoxDecoration(
                  color: AppColors.bgDeep,
                  borderRadius: BorderRadius.circular(4),
                ),
                child: CustomPaint(
                  painter: _OscGlyphPainter(
                    color: accent,
                    wave: wave?.choiceIndex ?? 2,
                  ),
                ),
              ),
            ],
          ),
          const SizedBox(height: 10),
          if (wave != null)
            _ParamChoiceField(
              param: wave!,
              label: 'Onda',
              width: 132,
              onChoice: onSetChoice,
            ),
          const SizedBox(height: 10),
          Wrap(
            spacing: 4,
            runSpacing: 2,
            children: [
              if (octave != null)
                _ParamKnob(
                  param: octave!,
                  label: 'Octava',
                  size: 46,
                  color: accent,
                  onSet: onSet,
                  onReset: onReset,
                ),
              if (semi != null)
                _ParamKnob(
                  param: semi!,
                  label: 'Semi',
                  size: 46,
                  color: accent,
                  onSet: onSet,
                  onReset: onReset,
                ),
              if (detune != null)
                _ParamKnob(
                  param: detune!,
                  label: 'Detune',
                  size: 46,
                  color: accent,
                  onSet: onSet,
                  onReset: onReset,
                ),
              if (level != null)
                _ParamKnob(
                  param: level!,
                  label: 'Nivel',
                  size: 46,
                  color: accent,
                  onSet: onSet,
                  onReset: onReset,
                ),
            ],
          ),
        ],
      ),
    );
  }
}

class _LayerCommonCard extends StatelessWidget {
  const _LayerCommonCard({
    required this.prefix,
    required this.accent,
    required this.isA,
    required this.params,
    required this.onSet,
    required this.onReset,
  });

  final String prefix;
  final Color accent;
  final bool isA;
  final Map<String, EzParam> params;
  final _SetParam onSet;
  final _ResetParam onReset;

  @override
  Widget build(BuildContext context) {
    return _Card(
      padding: const EdgeInsets.all(16),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          _CardHeader(
            icon: Icons.tune_rounded,
            title: 'Mezcla de capa',
            subtitle: 'Nivel, unísono y componentes auxiliares',
            color: accent,
          ),
          const SizedBox(height: 14),
          Wrap(
            spacing: 14,
            runSpacing: 8,
            crossAxisAlignment: WrapCrossAlignment.center,
            children: [
              ..._knob(isA ? 'a_level' : 'b_level', 'Nivel'),
              ..._knob(isA ? 'a_pan' : 'b_pan', 'Pan'),
              ..._knob(isA ? 'a_tune' : 'b_tune', 'Tono'),
              ..._knob(isA ? 'a_fine' : 'b_fine', 'Fine'),
              ..._knob(isA ? 'uni' : 'b_uni', 'Unísono'),
              ..._knob(isA ? 'unispr' : 'b_unispr', 'Apertura'),
              ..._knob(isA ? 'sublvl' : 'b_sublvl', 'Sub'),
              ..._knob(isA ? 'noise' : 'b_noise', 'Ruido'),
            ],
          ),
        ],
      ),
    );
  }

  List<Widget> _knob(String id, String label) {
    final p = _find(id);
    if (p == null) return const [];
    return [
      _ParamKnob(
        param: p,
        label: label,
        size: 54,
        color: accent,
        onSet: onSet,
        onReset: onReset,
      ),
    ];
  }

  EzParam? _find(String id) => params[id];
}

class _FilterCard extends StatelessWidget {
  const _FilterCard({
    required this.number,
    required this.type,
    required this.cutoff,
    required this.reso,
    required this.keytrack,
    required this.env,
    required this.drive,
    required this.color,
    required this.onSet,
    required this.onReset,
    required this.onChoice,
  });

  final int number;
  final EzParam? type;
  final EzParam? cutoff;
  final EzParam? reso;
  final EzParam? keytrack;
  final EzParam? env;
  final EzParam? drive;
  final Color color;
  final _SetParam onSet;
  final _ResetParam onReset;
  final _SetChoice onChoice;

  @override
  Widget build(BuildContext context) {
    final t = type?.choiceIndex ?? 0;
    return _Card(
      padding: const EdgeInsets.all(16),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          _CardHeader(
            icon: Icons.filter_alt_rounded,
            title: 'Filtro $number',
            subtitle: '${type?.choiceLabel ?? 'LP 12'} multimodo',
            color: color,
            trailing: Container(
              width: 24,
              height: 24,
              decoration: BoxDecoration(
                color: color.withValues(alpha: 0.12),
                borderRadius: BorderRadius.circular(5),
              ),
              child: Center(
                child: Text(
                  '$number',
                  style: TextStyle(
                    color: color,
                    fontSize: 10,
                    fontWeight: FontWeight.w800,
                  ),
                ),
              ),
            ),
          ),
          const SizedBox(height: 12),
          if (type != null)
            _ParamChoiceField(
              param: type!,
              label: 'Respuesta',
              width: 150,
              onChoice: onChoice,
            ),
          const SizedBox(height: 10),
          Container(
            height: 92,
            decoration: BoxDecoration(
              color: AppColors.bgDeep,
              borderRadius: BorderRadius.circular(7),
            ),
            child: CustomPaint(
              painter: _FilterResponsePainter(
                type: t,
                cutoff: cutoff?.value ?? 0.6,
                reso: reso?.value ?? 0.2,
                color: color,
              ),
            ),
          ),
          const SizedBox(height: 8),
          Wrap(
            spacing: 4,
            runSpacing: 2,
            children: [
              if (cutoff != null)
                _ParamKnob(
                  param: cutoff!,
                  label: 'Corte',
                  size: 50,
                  color: color,
                  onSet: onSet,
                  onReset: onReset,
                ),
              if (reso != null)
                _ParamKnob(
                  param: reso!,
                  label: 'Reso',
                  size: 50,
                  color: color,
                  onSet: onSet,
                  onReset: onReset,
                ),
              if (keytrack != null)
                _ParamKnob(
                  param: keytrack!,
                  label: 'Key',
                  size: 50,
                  color: color,
                  onSet: onSet,
                  onReset: onReset,
                ),
              if (env != null)
                _ParamKnob(
                  param: env!,
                  label: 'Env',
                  size: 50,
                  color: color,
                  onSet: onSet,
                  onReset: onReset,
                ),
              if (drive != null)
                _ParamKnob(
                  param: drive!,
                  label: 'Drive',
                  size: 50,
                  color: color,
                  onSet: onSet,
                  onReset: onReset,
                ),
            ],
          ),
        ],
      ),
    );
  }
}

class _EnvCard extends StatelessWidget {
  const _EnvCard({
    required this.title,
    required this.subtitle,
    required this.a,
    required this.d,
    required this.s,
    required this.r,
    required this.color,
    required this.onSet,
    required this.onReset,
  });

  final String title;
  final String subtitle;
  final EzParam? a;
  final EzParam? d;
  final EzParam? s;
  final EzParam? r;
  final Color color;
  final _SetParam onSet;
  final _ResetParam onReset;

  @override
  Widget build(BuildContext context) {
    final av = a?.value ?? 0.05;
    final dv = d?.value ?? 0.3;
    final sv = s?.value ?? 0.6;
    final rv = r?.value ?? 0.3;
    return _Card(
      padding: const EdgeInsets.all(15),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          _CardHeader(
            icon: Icons.show_chart_rounded,
            title: title,
            subtitle: subtitle,
            color: color,
            trailing: _EngineNameBadge(label: 'ADSR', color: color),
          ),
          const SizedBox(height: 10),
          Container(
            height: 88,
            decoration: BoxDecoration(
              color: AppColors.bgDeep,
              borderRadius: BorderRadius.circular(7),
            ),
            child: CustomPaint(
              painter: _EnvelopePainter(
                attack: av,
                decay: dv,
                sustain: sv,
                release: rv,
                color: color,
              ),
            ),
          ),
          const SizedBox(height: 6),
          Wrap(
            spacing: 4,
            children: [
              if (a != null)
                _ParamKnob(param: a!, label: 'A', size: 50, color: color, onSet: onSet, onReset: onReset),
              if (d != null)
                _ParamKnob(param: d!, label: 'D', size: 50, color: color, onSet: onSet, onReset: onReset),
              if (s != null)
                _ParamKnob(param: s!, label: 'S', size: 50, color: color, onSet: onSet, onReset: onReset),
              if (r != null)
                _ParamKnob(param: r!, label: 'R', size: 50, color: color, onSet: onSet, onReset: onReset),
            ],
          ),
        ],
      ),
    );
  }
}

class _LfoCard extends StatelessWidget {
  const _LfoCard({
    required this.number,
    required this.shape,
    required this.rate,
    required this.depth,
    required this.dest,
    required this.color,
    required this.onSet,
    required this.onReset,
    required this.onChoice,
  });

  final int number;
  final EzParam? shape;
  final EzParam? rate;
  final EzParam? depth;
  final EzParam? dest;
  final Color color;
  final _SetParam onSet;
  final _ResetParam onReset;
  final _SetChoice onChoice;

  @override
  Widget build(BuildContext context) {
    return _Card(
      padding: const EdgeInsets.all(12),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              Container(
                width: 22,
                height: 22,
                decoration: BoxDecoration(
                  color: color.withValues(alpha: 0.14),
                  borderRadius: BorderRadius.circular(5),
                  border: Border.all(color: color.withValues(alpha: 0.3)),
                ),
                child: Center(
                  child: Text(
                    '$number',
                    style: TextStyle(
                      color: color,
                      fontSize: 10,
                      fontWeight: FontWeight.w800,
                    ),
                  ),
                ),
              ),
              const SizedBox(width: 7),
              const Text(
                'LFO',
                style: TextStyle(
                  color: _ink,
                  fontSize: 11,
                  fontWeight: FontWeight.w700,
                ),
              ),
              const Spacer(),
              Text(
                rate?.text ?? '—',
                style: const TextStyle(
                  color: AppColors.textFaint,
                  fontSize: 8.5,
                ),
              ),
            ],
          ),
          const SizedBox(height: 8),
          Container(
            height: 38,
            decoration: BoxDecoration(
              color: AppColors.bgDeep,
              borderRadius: BorderRadius.circular(5),
            ),
            child: CustomPaint(
              painter: _LfoPreviewPainter(
                shape: shape?.choiceIndex ?? 0,
                rate: rate?.value ?? 0.3,
                depth: depth?.value ?? 0.5,
                color: color,
              ),
            ),
          ),
          const SizedBox(height: 8),
          if (shape != null)
            _ParamChoiceField(
              param: shape!,
              width: 112,
              onChoice: onChoice,
            ),
          const SizedBox(height: 7),
          Wrap(
            spacing: 2,
            runSpacing: 2,
            children: [
              if (rate != null)
                _ParamKnob(param: rate!, label: 'Rate', size: 44, color: color, onSet: onSet, onReset: onReset),
              if (depth != null)
                _ParamKnob(param: depth!, label: 'Depth', size: 44, color: color, onSet: onSet, onReset: onReset),
            ],
          ),
          const SizedBox(height: 7),
          if (dest != null)
            _ParamChoiceField(
              param: dest!,
              label: 'Destino',
              width: 142,
              onChoice: onChoice,
            ),
        ],
      ),
    );
  }
}

class _RandomCard extends StatelessWidget {
  const _RandomCard({
    required this.number,
    required this.rate,
    required this.depth,
    required this.color,
    required this.onSet,
    required this.onReset,
  });

  final int number;
  final EzParam? rate;
  final EzParam? depth;
  final Color color;
  final _SetParam onSet;
  final _ResetParam onReset;

  @override
  Widget build(BuildContext context) {
    return _Card(
      padding: const EdgeInsets.all(12),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              Icon(Icons.casino_rounded, color: color, size: 15),
              const SizedBox(width: 6),
              Text(
                'Random $number',
                style: const TextStyle(
                  color: _ink,
                  fontSize: 10.5,
                  fontWeight: FontWeight.w700,
                ),
              ),
            ],
          ),
          const SizedBox(height: 7),
          Container(
            height: 28,
            decoration: BoxDecoration(
              color: AppColors.bgDeep,
              borderRadius: BorderRadius.circular(5),
            ),
            child: CustomPaint(
              painter: _RandomPreviewPainter(
                rate: rate?.value ?? 0.2,
                depth: depth?.value ?? 0.5,
                color: color,
                seed: number,
              ),
            ),
          ),
          const SizedBox(height: 6),
          Row(
            mainAxisAlignment: MainAxisAlignment.spaceEvenly,
            children: [
              if (rate != null)
                _ParamKnob(param: rate!, label: 'Rate', size: 44, color: color, onSet: onSet, onReset: onReset),
              if (depth != null)
                _ParamKnob(param: depth!, label: 'Depth', size: 44, color: color, onSet: onSet, onReset: onReset),
            ],
          ),
        ],
      ),
    );
  }
}

class _MatrixPanel extends StatelessWidget {
  const _MatrixPanel({required this.rows});
  final List<Widget> rows;

  @override
  Widget build(BuildContext context) {
    return _Card(
      padding: const EdgeInsets.all(14),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          _CardHeader(
            icon: Icons.hub_rounded,
            title: 'Matriz de modulación',
            subtitle: '16 rutas · cantidad bipolar',
            color: _mod,
            trailing: _EngineNameBadge(label: '${rows.length}/16', color: _mod),
          ),
          const SizedBox(height: 12),
          if (rows.isEmpty)
            const Padding(
              padding: EdgeInsets.all(18),
              child: Text(
                'No hay fuentes de modulación disponibles.',
                style: TextStyle(color: AppColors.textFaint, fontSize: 10),
              ),
            )
          else ...[
            const Padding(
              padding: EdgeInsets.fromLTRB(2, 0, 2, 6),
              child: Row(
                children: [
                  SizedBox(width: 24),
                  SizedBox(width: 154, child: _MatrixHeader('FUENTE')),
                  SizedBox(width: 22),
                  SizedBox(width: 188, child: _MatrixHeader('DESTINO')),
                  Expanded(child: _MatrixHeader('CANTIDAD')),
                  SizedBox(width: 42, child: _MatrixHeader('VALOR', end: true)),
                ],
              ),
            ),
            ...rows,
          ],
        ],
      ),
    );
  }
}

class _MatrixHeader extends StatelessWidget {
  const _MatrixHeader(this.text, {this.end = false});
  final String text;
  final bool end;

  @override
  Widget build(BuildContext context) {
    return Text(
      text,
      textAlign: end ? TextAlign.right : TextAlign.left,
      style: const TextStyle(
        color: AppColors.textFaint,
        fontSize: 7.5,
        fontWeight: FontWeight.w800,
        letterSpacing: 0.7,
      ),
    );
  }
}

class _MatrixRow extends StatelessWidget {
  const _MatrixRow({
    required this.number,
    required this.source,
    required this.dest,
    required this.amount,
    required this.onChoice,
    required this.onSet,
  });

  final int number;
  final EzParam source;
  final EzParam dest;
  final EzParam amount;
  final _SetChoice onChoice;
  final _SetParam onSet;

  bool get _active => source.choiceIndex != 0 && dest.choiceIndex != 0;

  @override
  Widget build(BuildContext context) {
    return Container(
      height: 38,
      margin: const EdgeInsets.only(bottom: 3),
      padding: const EdgeInsets.symmetric(horizontal: 2),
      decoration: BoxDecoration(
        color: _active ? _mod.withValues(alpha: 0.045) : Colors.transparent,
        borderRadius: BorderRadius.circular(5),
      ),
      child: Row(
        children: [
          SizedBox(
            width: 24,
            child: Text(
              '$number',
              style: TextStyle(
                color: _active ? _mod : AppColors.textFaint,
                fontSize: 8.5,
                fontWeight: FontWeight.w700,
              ),
            ),
          ),
          SizedBox(
            width: 154,
            child: _ParamChoiceField(
              param: source,
              width: 148,
              onChoice: onChoice,
            ),
          ),
          const SizedBox(
            width: 22,
            child: Icon(
              Icons.arrow_forward_rounded,
              size: 12,
              color: AppColors.textFaint,
            ),
          ),
          SizedBox(
            width: 188,
            child: _ParamChoiceField(
              param: dest,
              width: 182,
              onChoice: onChoice,
            ),
          ),
          const SizedBox(width: 6),
          Expanded(
            child: _AmountSlider(
              param: amount,
              color: _active ? _mod : AppColors.textFaint,
              onSet: onSet,
              compact: true,
            ),
          ),
          SizedBox(
            width: 42,
            child: Text(
              amount.text,
              textAlign: TextAlign.right,
              style: TextStyle(
                color: _active ? _mod : AppColors.textFaint,
                fontSize: 8,
                fontWeight: FontWeight.w600,
              ),
            ),
          ),
        ],
      ),
    );
  }
}

class _FxSlotCard extends StatelessWidget {
  const _FxSlotCard({
    required this.number,
    required this.type,
    required this.mix,
    required this.p1,
    required this.p2,
    required this.p3,
    required this.p4,
    required this.onSet,
    required this.onReset,
    required this.onChoice,
  });

  final int number;
  final EzParam type;
  final EzParam? mix;
  final EzParam? p1;
  final EzParam? p2;
  final EzParam? p3;
  final EzParam? p4;
  final _SetParam onSet;
  final _ResetParam onReset;
  final _SetChoice onChoice;

  static const _icons = [
    Icons.block_rounded,
    Icons.local_fire_department_rounded,
    Icons.waves_rounded,
    Icons.blur_circular_rounded,
    Icons.timer_rounded,
    Icons.cloud_rounded,
    Icons.compress_rounded,
    Icons.equalizer_rounded,
  ];

  static const _labels = <List<String>>[
    ['—', '—', '—', '—'],
    ['Drive', 'Tono', 'Sesgo', 'Salida'],
    ['Delay', 'Prof.', 'Rate', 'Ancho'],
    ['Rate', 'Prof.', 'Feedback', 'Spread'],
    ['Tiempo', 'Feedback', 'Tono', 'Ping'],
    ['Tamaño', 'Damping', 'Ancho', 'Pre-delay'],
    ['Umbral', 'Ratio', 'Ataque', 'Release'],
    ['Graves', 'Medios', 'Agudos', 'Frec.'],
  ];

  @override
  Widget build(BuildContext context) {
    final t = type.choiceIndex.clamp(0, _icons.length - 1).toInt();
    final isOff = t == 0;
    final params = [p1, p2, p3, p4];
    final labels = _labels[t];
    return _Card(
      padding: const EdgeInsets.all(14),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              Container(
                width: 28,
                height: 28,
                decoration: BoxDecoration(
                  color: isOff
                      ? AppColors.surfaceAlt
                      : _fx.withValues(alpha: 0.14),
                  borderRadius: BorderRadius.circular(7),
                  border: Border.all(
                    color: isOff
                        ? AppColors.border
                        : _fx.withValues(alpha: 0.35),
                  ),
                ),
                child: Icon(
                  _icons[t],
                  size: 15,
                  color: isOff ? AppColors.textFaint : _fx,
                ),
              ),
              const SizedBox(width: 9),
              Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(
                    'FX $number',
                    style: const TextStyle(
                      color: _ink,
                      fontSize: 11,
                      fontWeight: FontWeight.w800,
                    ),
                  ),
                  Text(
                    isOff ? 'Slot libre' : type.choiceLabel,
                    style: TextStyle(
                      color: isOff ? AppColors.textFaint : _fx,
                      fontSize: 9,
                      fontWeight: FontWeight.w600,
                    ),
                  ),
                ],
              ),
              const Spacer(),
              if (mix != null)
                _ParamKnob(
                  param: mix!,
                  label: 'Mix',
                  size: 48,
                  color: _fx,
                  onSet: onSet,
                  onReset: onReset,
                ),
            ],
          ),
          const SizedBox(height: 9),
          _ParamChoiceField(
            param: type,
            width: 136,
            onChoice: onChoice,
          ),
          const SizedBox(height: 8),
          Container(
            height: 1,
            color: AppColors.border,
            margin: const EdgeInsets.only(bottom: 6),
          ),
          Opacity(
            opacity: isOff ? 0.33 : 1,
            child: Wrap(
              spacing: 2,
              runSpacing: 2,
              children: [
                for (var i = 0; i < 4; i++)
                  if (params[i] != null)
                    _ParamKnob(
                      param: params[i]!,
                      label: labels[i],
                      size: 48,
                      color: _fx,
                      onSet: onSet,
                      onReset: onReset,
                    ),
              ],
            ),
          ),
        ],
      ),
    );
  }
}

class _FxChainStrip extends StatelessWidget {
  const _FxChainStrip({required this.active, required this.labels});
  final List<bool> active;
  final List<String> labels;

  @override
  Widget build(BuildContext context) {
    return Container(
      height: 44,
      padding: const EdgeInsets.symmetric(horizontal: 10),
      decoration: BoxDecoration(
        color: AppColors.bgPanel,
        borderRadius: BorderRadius.circular(8),
        border: Border.all(color: AppColors.border),
      ),
      child: Row(
        children: [
          const _ChainTerminal(label: 'IN'),
          for (var i = 0; i < 6; i++) ...[
            const Padding(
              padding: EdgeInsets.symmetric(horizontal: 3),
              child: Icon(
                Icons.chevron_right_rounded,
                size: 13,
                color: AppColors.textFaint,
              ),
            ),
            Expanded(
              child: Container(
                height: 25,
                padding: const EdgeInsets.symmetric(horizontal: 5),
                decoration: BoxDecoration(
                  color: active[i]
                      ? _fx.withValues(alpha: 0.14)
                      : AppColors.bgDeep,
                  borderRadius: BorderRadius.circular(5),
                  border: Border.all(
                    color: active[i]
                        ? _fx.withValues(alpha: 0.4)
                        : AppColors.border,
                  ),
                ),
                child: Row(
                  mainAxisAlignment: MainAxisAlignment.center,
                  children: [
                    Text(
                      '${i + 1}',
                      style: TextStyle(
                        color: active[i] ? _fx : AppColors.textFaint,
                        fontSize: 8,
                        fontWeight: FontWeight.w800,
                      ),
                    ),
                    const SizedBox(width: 4),
                    Flexible(
                      child: Text(
                        labels[i],
                        maxLines: 1,
                        overflow: TextOverflow.ellipsis,
                        style: TextStyle(
                          color: active[i] ? _ink : AppColors.textFaint,
                          fontSize: 8.5,
                          fontWeight: FontWeight.w600,
                        ),
                      ),
                    ),
                  ],
                ),
              ),
            ),
          ],
          const Padding(
            padding: EdgeInsets.symmetric(horizontal: 3),
            child: Icon(
              Icons.chevron_right_rounded,
              size: 13,
              color: AppColors.textFaint,
            ),
          ),
          const _ChainTerminal(label: 'OUT'),
        ],
      ),
    );
  }
}

class _ChainTerminal extends StatelessWidget {
  const _ChainTerminal({required this.label});
  final String label;

  @override
  Widget build(BuildContext context) {
    return Text(
      label,
      style: const TextStyle(
        color: AppColors.textFaint,
        fontSize: 8,
        fontWeight: FontWeight.w800,
        letterSpacing: 0.6,
      ),
    );
  }
}

class _StepGrid extends StatelessWidget {
  const _StepGrid({
    required this.on,
    required this.steps,
    required this.active,
    required this.onSet,
    required this.length,
  });

  final EzParam? on;
  final List<EzParam?> steps;
  final bool active;
  final _SetParam onSet;
  final double length;

  @override
  Widget build(BuildContext context) {
    final count = (1 + (length.clamp(0.0, 1.0) * 15).round()).clamp(1, 16);
    return Wrap(
      spacing: 5,
      runSpacing: 5,
      children: [
        for (var i = 0; i < steps.length; i++)
          if (steps[i] != null)
            _StepButton(
              number: i + 1,
              param: steps[i]!,
              enabled: i < count,
              active: active && steps[i]!.value > 0.5,
              onSet: onSet,
            ),
      ],
    );
  }
}

class _StepButton extends StatelessWidget {
  const _StepButton({
    required this.number,
    required this.param,
    required this.enabled,
    required this.active,
    required this.onSet,
  });

  final int number;
  final EzParam param;
  final bool enabled;
  final bool active;
  final _SetParam onSet;

  @override
  Widget build(BuildContext context) {
    final on = param.value > 0.5;
    return MouseRegion(
      cursor: SystemMouseCursors.click,
      child: GestureDetector(
        onTap: () => onSet(param, on ? 0 : 1),
        child: Opacity(
          opacity: enabled ? 1 : 0.24,
          child: AnimatedContainer(
            duration: const Duration(milliseconds: 120),
            width: 42,
            height: 56,
            decoration: BoxDecoration(
              color: on
                  ? _macro.withValues(alpha: 0.2)
                  : AppColors.bgDeep,
              borderRadius: BorderRadius.circular(6),
              border: Border.all(
                color: on
                    ? _macro.withValues(alpha: 0.7)
                    : AppColors.border,
              ),
            ),
            child: Column(
              children: [
                const SizedBox(height: 5),
                Text(
                  '$number',
                  style: TextStyle(
                    color: on ? _macro : AppColors.textFaint,
                    fontSize: 8,
                    fontWeight: FontWeight.w700,
                  ),
                ),
                const Spacer(),
                Container(
                  width: 20,
                  height: on ? 20 : 8,
                  decoration: BoxDecoration(
                    color: on
                        ? _macro.withValues(alpha: 0.85)
                        : AppColors.surfaceHigh,
                    borderRadius: BorderRadius.circular(3),
                  ),
                ),
                const Spacer(),
                if (number % 4 == 0)
                  Container(
                    width: 3,
                    height: 3,
                    decoration: const BoxDecoration(
                      color: AppColors.textFaint,
                      shape: BoxShape.circle,
                    ),
                  ),
                const SizedBox(height: 4),
              ],
            ),
          ),
        ),
      ),
    );
  }
}

class _MacroEditor extends StatelessWidget {
  const _MacroEditor({
    required this.number,
    required this.name,
    required this.value,
    required this.routes,
    required this.color,
    required this.onSet,
    required this.onChoice,
  });

  final int number;
  final String name;
  final EzParam? value;
  final List<(EzParam?, EzParam?)> routes;
  final Color color;
  final _SetParam onSet;
  final _SetChoice onChoice;

  @override
  Widget build(BuildContext context) {
    return _Card(
      padding: const EdgeInsets.all(13),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          Row(
            children: [
              Container(
                width: 26,
                height: 26,
                decoration: BoxDecoration(
                  color: color.withValues(alpha: 0.14),
                  borderRadius: BorderRadius.circular(6),
                  border: Border.all(color: color.withValues(alpha: 0.35)),
                ),
                child: Center(
                  child: Text(
                    '$number',
                    style: TextStyle(
                      color: color,
                      fontSize: 10,
                      fontWeight: FontWeight.w800,
                    ),
                  ),
                ),
              ),
              const SizedBox(width: 9),
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(
                      name,
                      style: const TextStyle(
                        color: _ink,
                        fontSize: 12,
                        fontWeight: FontWeight.w700,
                      ),
                    ),
                    const Text(
                      '4 rutas',
                      style: TextStyle(
                        color: AppColors.textFaint,
                        fontSize: 8.5,
                      ),
                    ),
                  ],
                ),
              ),
              if (value != null)
                SizedBox(
                  width: 180,
                  child: SliderTheme(
                    data: SliderTheme.of(context).copyWith(
                      trackHeight: 3,
                      activeTrackColor: color,
                      inactiveTrackColor: AppColors.bgDeep,
                      thumbColor: color,
                      thumbShape: const RoundSliderThumbShape(
                        enabledThumbRadius: 5,
                      ),
                      overlayShape: const RoundSliderOverlayShape(
                        overlayRadius: 10,
                      ),
                      overlayColor: color.withValues(alpha: 0.16),
                    ),
                    child: Slider(
                      value: value!.value.clamp(0.0, 1.0),
                      onChanged: (v) => onSet(value!, v),
                    ),
                  ),
                ),
              if (value != null)
                SizedBox(
                  width: 20,
                  child: Text(
                    '${(value!.value * 100).round()}',
                    textAlign: TextAlign.right,
                    style: TextStyle(
                      color: color,
                      fontSize: 9,
                      fontWeight: FontWeight.w700,
                    ),
                  ),
                ),
            ],
          ),
          const SizedBox(height: 8),
          for (var r = 0; r < routes.length; r++)
            Padding(
              padding: const EdgeInsets.only(bottom: 3),
              child: Row(
                children: [
                  SizedBox(
                    width: 18,
                    child: Text(
                      '${r + 1}',
                      style: const TextStyle(
                        color: AppColors.textFaint,
                        fontSize: 8,
                      ),
                    ),
                  ),
                  SizedBox(
                    width: 150,
                    child: routes[r].$1 == null
                        ? const Text(
                            '—',
                            style: TextStyle(
                              color: AppColors.textFaint,
                              fontSize: 9,
                            ),
                          )
                        : _ParamChoiceField(
                            param: routes[r].$1!,
                            width: 146,
                            onChoice: onChoice,
                          ),
                  ),
                  const SizedBox(width: 6),
                  Expanded(
                    child: routes[r].$2 == null
                        ? const SizedBox.shrink()
                        : _AmountSlider(
                            param: routes[r].$2!,
                            color: color,
                            onSet: onSet,
                            compact: true,
                          ),
                  ),
                  SizedBox(
                    width: 34,
                    child: Text(
                      routes[r].$2?.text ?? '',
                      textAlign: TextAlign.right,
                      style: const TextStyle(
                        color: AppColors.textFaint,
                        fontSize: 8,
                      ),
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

class _PresetStrip extends StatelessWidget {
  const _PresetStrip({
    required this.programs,
    required this.current,
    required this.onSelect,
    required this.onOpen,
  });

  final List<_ProgramInfo> programs;
  final int current;
  final ValueChanged<int> onSelect;
  final VoidCallback onOpen;

  @override
  Widget build(BuildContext context) {
    if (programs.isEmpty) {
      return const _Card(
        padding: EdgeInsets.all(16),
        child: Text(
          'No hay presets disponibles.',
          style: TextStyle(color: AppColors.textFaint, fontSize: 10),
        ),
      );
    }
    final start = (current - 2).clamp(0, math.max(0, programs.length - 6)).toInt();
    final visible = programs.skip(start).take(6).toList();
    return SizedBox(
      height: 74,
      child: Row(
        children: [
          Expanded(
            child: ListView.separated(
              scrollDirection: Axis.horizontal,
              itemCount: visible.length,
              separatorBuilder: (_, __) => const SizedBox(width: 8),
              itemBuilder: (context, i) {
                final p = visible[i];
                final selected = p.index == current;
                return _PresetMiniCard(
                  program: p,
                  selected: selected,
                  onTap: () => onSelect(p.index),
                );
              },
            ),
          ),
          const SizedBox(width: 8),
          MouseRegion(
            cursor: SystemMouseCursors.click,
            child: GestureDetector(
              onTap: onOpen,
              child: Container(
                width: 82,
                height: 64,
                decoration: BoxDecoration(
                  color: AppColors.surfaceAlt,
                  borderRadius: BorderRadius.circular(7),
                  border: Border.all(color: AppColors.border),
                ),
                child: const Column(
                  mainAxisAlignment: MainAxisAlignment.center,
                  children: [
                    Icon(
                      Icons.library_music_rounded,
                      size: 17,
                      color: _macro,
                    ),
                    SizedBox(height: 4),
                    Text(
                      'Ver banco',
                      style: TextStyle(
                        color: AppColors.textDim,
                        fontSize: 8,
                      ),
                    ),
                  ],
                ),
              ),
            ),
          ),
        ],
      ),
    );
  }
}

class _PresetMiniCard extends StatelessWidget {
  const _PresetMiniCard({
    required this.program,
    required this.selected,
    required this.onTap,
  });

  final _ProgramInfo program;
  final bool selected;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    return MouseRegion(
      cursor: SystemMouseCursors.click,
      child: GestureDetector(
        onTap: onTap,
        child: Container(
          width: 166,
          padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 8),
          decoration: BoxDecoration(
            color: selected
                ? _macro.withValues(alpha: 0.12)
                : AppColors.surface,
            borderRadius: BorderRadius.circular(7),
            border: Border.all(
              color: selected
                  ? _macro.withValues(alpha: 0.55)
                  : AppColors.border,
            ),
          ),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            mainAxisAlignment: MainAxisAlignment.center,
            children: [
              Text(
                program.category.toUpperCase(),
                maxLines: 1,
                overflow: TextOverflow.ellipsis,
                style: TextStyle(
                  color: selected ? _macro : AppColors.textFaint,
                  fontSize: 7.5,
                  fontWeight: FontWeight.w800,
                  letterSpacing: 0.7,
                ),
              ),
              const SizedBox(height: 3),
              Text(
                program.shortName,
                maxLines: 1,
                overflow: TextOverflow.ellipsis,
                style: TextStyle(
                  color: selected ? _ink : AppColors.textDim,
                  fontSize: 10,
                  fontWeight: selected ? FontWeight.w700 : FontWeight.w500,
                ),
              ),
              const SizedBox(height: 3),
              const Text(
                'EZSynth 2.0',
                style: TextStyle(color: AppColors.textFaint, fontSize: 7.5),
              ),
            ],
          ),
        ),
      ),
    );
  }
}

// ===========================================================================
// Preset browser
// ===========================================================================

class _PresetBrowser extends StatefulWidget {
  const _PresetBrowser({required this.programs, required this.current});
  final List<_ProgramInfo> programs;
  final int current;

  @override
  State<_PresetBrowser> createState() => _PresetBrowserState();
}

class _PresetBrowserState extends State<_PresetBrowser> {
  late final TextEditingController _search;
  String _category = 'Todos';
  String _query = '';

  @override
  void initState() {
    super.initState();
    _search = TextEditingController();
  }

  @override
  void dispose() {
    _search.dispose();
    super.dispose();
  }

  List<String> get _categories {
    final set = <String>{'Todos'};
    for (final p in widget.programs) {
      set.add(p.category);
    }
    final list = set.toList()..sort();
    if (list.remove('Todos')) list.insert(0, 'Todos');
    return list;
  }

  int _countFor(String category) {
    if (category == 'Todos') return widget.programs.length;
    return widget.programs.where((p) => p.category == category).length;
  }

  List<_ProgramInfo> get _filtered {
    final q = _query.trim().toLowerCase();
    return widget.programs.where((p) {
      if (_category != 'Todos' && p.category != _category) return false;
      if (q.isEmpty) return true;
      return p.name.toLowerCase().contains(q);
    }).toList();
  }

  @override
  Widget build(BuildContext context) {
    final media = MediaQuery.sizeOf(context);
    final filtered = _filtered;
    return Dialog(
      backgroundColor: AppColors.bgPanel,
      insetPadding: const EdgeInsets.all(28),
      clipBehavior: Clip.antiAlias,
      shape: RoundedRectangleBorder(
        borderRadius: BorderRadius.circular(13),
        side: const BorderSide(color: AppColors.borderStrong),
      ),
      child: SizedBox(
        width: math.min(920, media.width - 56),
        height: math.min(650, media.height - 56),
        child: Column(
          children: [
            Container(
              height: 64,
              padding: const EdgeInsets.symmetric(horizontal: 18),
              color: AppColors.bgDeep,
              child: Row(
                children: [
                  Container(
                    width: 34,
                    height: 34,
                    decoration: BoxDecoration(
                      color: _macro.withValues(alpha: 0.14),
                      borderRadius: BorderRadius.circular(8),
                      border: Border.all(color: _macro.withValues(alpha: 0.35)),
                    ),
                    child: const Icon(
                      Icons.library_music_rounded,
                      color: _macro,
                      size: 18,
                    ),
                  ),
                  const SizedBox(width: 11),
                  const Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    mainAxisAlignment: MainAxisAlignment.center,
                    children: [
                      Text(
                        'Banco EZSynth',
                        style: TextStyle(
                          color: _ink,
                          fontSize: 17,
                          fontWeight: FontWeight.w800,
                          letterSpacing: -0.3,
                        ),
                      ),
                      Text(
                        'Sonidos originales organizados por carácter',
                        style: TextStyle(
                          color: AppColors.textFaint,
                          fontSize: 9.5,
                        ),
                      ),
                    ],
                  ),
                  const Spacer(),
                  _EngineNameBadge(
                    label: '${widget.programs.length} presets',
                    color: _macro,
                  ),
                  const SizedBox(width: 8),
                  _HeaderPill(
                    icon: Icons.close_rounded,
                    label: 'Cerrar',
                    color: AppColors.textDim,
                    onTap: () => Navigator.of(context).pop(),
                  ),
                ],
              ),
            ),
            const Divider(height: 1, color: AppColors.border),
            Expanded(
              child: Row(
                children: [
                  SizedBox(
                    width: 176,
                    child: ListView(
                      padding: const EdgeInsets.fromLTRB(10, 12, 10, 12),
                      children: [
                        const Padding(
                          padding: EdgeInsets.only(left: 6, bottom: 8),
                          child: Text(
                            'CATEGORÍAS',
                            style: TextStyle(
                              color: AppColors.textFaint,
                              fontSize: 8,
                              fontWeight: FontWeight.w800,
                              letterSpacing: 1.1,
                            ),
                          ),
                        ),
                        for (final c in _categories)
                          _CategoryTile(
                            label: c,
                            count: _countFor(c),
                            selected: _category == c,
                            onTap: () => setState(() => _category = c),
                          ),
                      ],
                    ),
                  ),
                  const VerticalDivider(width: 1, color: AppColors.border),
                  Expanded(
                    child: Column(
                      children: [
                        Padding(
                          padding: const EdgeInsets.fromLTRB(14, 12, 14, 8),
                          child: TextField(
                            controller: _search,
                            autofocus: true,
                            onChanged: (v) => setState(() => _query = v),
                            style: const TextStyle(color: _ink, fontSize: 12),
                            decoration: InputDecoration(
                              isDense: true,
                              filled: true,
                              fillColor: AppColors.bgDeep,
                              hintText: 'Buscar por nombre o categoría…',
                              hintStyle: const TextStyle(
                                color: AppColors.textFaint,
                                fontSize: 11,
                              ),
                              prefixIcon: const Icon(
                                Icons.search_rounded,
                                color: AppColors.textFaint,
                                size: 17,
                              ),
                              suffixIcon: _query.isEmpty
                                  ? null
                                  : IconButton(
                                      onPressed: () {
                                        _search.clear();
                                        setState(() => _query = '');
                                      },
                                      icon: const Icon(
                                        Icons.close_rounded,
                                        size: 15,
                                        color: AppColors.textFaint,
                                      ),
                                    ),
                              border: OutlineInputBorder(
                                borderRadius: BorderRadius.circular(7),
                                borderSide: const BorderSide(
                                  color: AppColors.border,
                                ),
                              ),
                              enabledBorder: OutlineInputBorder(
                                borderRadius: BorderRadius.circular(7),
                                borderSide: const BorderSide(
                                  color: AppColors.border,
                                ),
                              ),
                              focusedBorder: OutlineInputBorder(
                                borderRadius: BorderRadius.circular(7),
                                borderSide: const BorderSide(color: _macro),
                              ),
                            ),
                          ),
                        ),
                        Padding(
                          padding: const EdgeInsets.symmetric(horizontal: 15),
                          child: Row(
                            children: [
                              Text(
                                '${filtered.length} resultados',
                                style: const TextStyle(
                                  color: AppColors.textFaint,
                                  fontSize: 9,
                                ),
                              ),
                              const Spacer(),
                              const Text(
                                'Un clic para cargar',
                                style: TextStyle(
                                  color: AppColors.textFaint,
                                  fontSize: 9,
                                ),
                              ),
                            ],
                          ),
                        ),
                        const SizedBox(height: 6),
                        Expanded(
                          child: filtered.isEmpty
                              ? const Center(
                                  child: Text(
                                    'Sin coincidencias',
                                    style: TextStyle(
                                      color: AppColors.textFaint,
                                      fontSize: 11,
                                    ),
                                  ),
                                )
                              : ListView.builder(
                                  padding: const EdgeInsets.fromLTRB(
                                    10,
                                    2,
                                    10,
                                    12,
                                  ),
                                  itemCount: filtered.length,
                                  itemBuilder: (context, i) {
                                    final p = filtered[i];
                                    return _PresetRow(
                                      program: p,
                                      selected: p.index == widget.current,
                                      onTap: () => Navigator.of(context).pop(p.index),
                                    );
                                  },
                                ),
                        ),
                      ],
                    ),
                  ),
                ],
              ),
            ),
          ],
        ),
      ),
    );
  }
}

class _CategoryTile extends StatefulWidget {
  const _CategoryTile({
    required this.label,
    required this.count,
    required this.selected,
    required this.onTap,
  });

  final String label;
  final int count;
  final bool selected;
  final VoidCallback onTap;

  @override
  State<_CategoryTile> createState() => _CategoryTileState();
}

class _CategoryTileState extends State<_CategoryTile> {
  bool _hover = false;

  @override
  Widget build(BuildContext context) {
    return MouseRegion(
      cursor: SystemMouseCursors.click,
      onEnter: (_) => setState(() => _hover = true),
      onExit: (_) => setState(() => _hover = false),
      child: GestureDetector(
        onTap: widget.onTap,
        child: Container(
          height: 31,
          margin: const EdgeInsets.only(bottom: 2),
          padding: const EdgeInsets.symmetric(horizontal: 8),
          decoration: BoxDecoration(
            color: widget.selected
                ? _macro.withValues(alpha: 0.13)
                : _hover
                    ? AppColors.surfaceAlt
                    : Colors.transparent,
            borderRadius: BorderRadius.circular(6),
          ),
          child: Row(
            children: [
              Expanded(
                child: Text(
                  widget.label,
                  maxLines: 1,
                  overflow: TextOverflow.ellipsis,
                  style: TextStyle(
                    color: widget.selected ? _macro : AppColors.textDim,
                    fontSize: 10,
                    fontWeight:
                        widget.selected ? FontWeight.w700 : FontWeight.w500,
                  ),
                ),
              ),
              Text(
                '${widget.count}',
                style: TextStyle(
                  color: widget.selected ? _macro : AppColors.textFaint,
                  fontSize: 8,
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }
}

class _PresetRow extends StatefulWidget {
  const _PresetRow({
    required this.program,
    required this.selected,
    required this.onTap,
  });

  final _ProgramInfo program;
  final bool selected;
  final VoidCallback onTap;

  @override
  State<_PresetRow> createState() => _PresetRowState();
}

class _PresetRowState extends State<_PresetRow> {
  bool _hover = false;

  @override
  Widget build(BuildContext context) {
    return MouseRegion(
      cursor: SystemMouseCursors.click,
      onEnter: (_) => setState(() => _hover = true),
      onExit: (_) => setState(() => _hover = false),
      child: GestureDetector(
        onTap: widget.onTap,
        child: Container(
          height: 46,
          margin: const EdgeInsets.only(bottom: 3),
          padding: const EdgeInsets.symmetric(horizontal: 10),
          decoration: BoxDecoration(
            color: widget.selected
                ? _macro.withValues(alpha: 0.1)
                : _hover
                    ? AppColors.surfaceAlt
                    : AppColors.surface,
            borderRadius: BorderRadius.circular(6),
            border: Border.all(
              color: widget.selected
                  ? _macro.withValues(alpha: 0.5)
                  : AppColors.border,
            ),
          ),
          child: Row(
            children: [
              SizedBox(
                width: 28,
                child: Text(
                  '${widget.program.index + 1}'.padLeft(3, '0'),
                  style: TextStyle(
                    color: widget.selected ? _macro : AppColors.textFaint,
                    fontSize: 8,
                    fontWeight: FontWeight.w700,
                  ),
                ),
              ),
              Container(
                width: 3,
                height: 22,
                decoration: BoxDecoration(
                  color: widget.selected ? _macro : AppColors.borderStrong,
                  borderRadius: BorderRadius.circular(2),
                ),
              ),
              const SizedBox(width: 10),
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  mainAxisAlignment: MainAxisAlignment.center,
                  children: [
                    Text(
                      widget.program.shortName,
                      maxLines: 1,
                      overflow: TextOverflow.ellipsis,
                      style: TextStyle(
                        color: widget.selected ? _ink : AppColors.textDim,
                        fontSize: 11,
                        fontWeight:
                            widget.selected ? FontWeight.w700 : FontWeight.w500,
                      ),
                    ),
                    Text(
                      widget.program.category,
                      style: const TextStyle(
                        color: AppColors.textFaint,
                        fontSize: 8.5,
                      ),
                    ),
                  ],
                ),
              ),
              if (widget.selected)
                const _EngineNameBadge(label: 'ACTUAL', color: _macro),
              const SizedBox(width: 8),
              const Icon(
                Icons.chevron_right_rounded,
                size: 16,
                color: AppColors.textFaint,
              ),
            ],
          ),
        ),
      ),
    );
  }
}

// ===========================================================================
// Painters
// ===========================================================================

double _oscWave(double phase, int wave) {
  final t = (phase / (2 * math.pi)) % 1.0;
  switch (wave) {
    case 0:
      return math.sin(phase);
    case 1:
      return 1 - 4 * (t - 0.5).abs();
    case 2:
      return 2 * t - 1;
    case 3:
      return t < 0.5 ? 1 : -1;
    case 4:
      return t < 0.25 ? 1 : -1;
    default:
      return math.sin(phase);
  }
}

Path _wavePath(Size size, double Function(double x) f, {double padding = 8}) {
  final path = Path();
  const steps = 160;
  for (var i = 0; i <= steps; i++) {
    final x = i / steps;
    final y = size.height / 2 - f(x) * (size.height / 2 - padding);
    if (i == 0) {
      path.moveTo(x * size.width, y);
    } else {
      path.lineTo(x * size.width, y);
    }
  }
  return path;
}

class _DecorWavePainter extends CustomPainter {
  _DecorWavePainter({required this.color, required this.seed});
  final Color color;
  final int seed;

  @override
  void paint(Canvas canvas, Size size) {
    canvas.drawLine(
      Offset(0, size.height / 2),
      Offset(size.width, size.height / 2),
      Paint()
        ..color = AppColors.border.withValues(alpha: 0.6)
        ..strokeWidth = 1,
    );
    for (var layer = 0; layer < 3; layer++) {
      final path = Path();
      const steps = 80;
      final amplitude = size.height * (0.18 + layer * 0.09);
      for (var i = 0; i <= steps; i++) {
        final x = i / steps;
        final phase = x * math.pi * 4 + layer * 1.4 + (seed % 10) * 0.13;
        final y = size.height / 2 +
            math.sin(phase) * amplitude * (1 - x * 0.25);
        if (i == 0) {
          path.moveTo(x * size.width, y);
        } else {
          path.lineTo(x * size.width, y);
        }
      }
      canvas.drawPath(
        path,
        Paint()
          ..style = PaintingStyle.stroke
          ..strokeWidth = layer == 0 ? 1.7 : 1
          ..color = color.withValues(alpha: layer == 0 ? 0.9 : 0.28),
      );
    }
  }

  @override
  bool shouldRepaint(covariant _DecorWavePainter oldDelegate) =>
      oldDelegate.color != color || oldDelegate.seed != seed;
}

class _OscGlyphPainter extends CustomPainter {
  _OscGlyphPainter({required this.color, required this.wave});
  final Color color;
  final int wave;

  @override
  void paint(Canvas canvas, Size size) {
    final path = _wavePath(
      size,
      (x) => _oscWave(x * math.pi * 4, wave),
      padding: 4,
    );
    canvas.drawPath(
      path,
      Paint()
        ..style = PaintingStyle.stroke
        ..strokeWidth = 1.4
        ..color = color,
    );
  }

  @override
  bool shouldRepaint(covariant _OscGlyphPainter oldDelegate) =>
      oldDelegate.color != color || oldDelegate.wave != wave;
}

class _EngineWavePainter extends CustomPainter {
  _EngineWavePainter({
    required this.engine,
    required this.color,
    required this.wave,
    required this.wtPos,
    required this.fmRatio,
    required this.fmIndex,
    required this.grainSize,
    required this.grainDensity,
  });

  final int engine;
  final Color color;
  final int wave;
  final double wtPos;
  final double fmRatio;
  final double fmIndex;
  final double grainSize;
  final double grainDensity;

  @override
  void paint(Canvas canvas, Size size) {
    final grid = Paint()
      ..color = AppColors.border.withValues(alpha: 0.55)
      ..strokeWidth = 1;
    for (var i = 1; i < 6; i++) {
      final x = size.width * i / 6;
      canvas.drawLine(Offset(x, 0), Offset(x, size.height), grid);
    }
    canvas.drawLine(
      Offset(0, size.height / 2),
      Offset(size.width, size.height / 2),
      grid,
    );

    Path path;
    if (engine == 4) {
      path = _grainPath(size);
    } else if (engine == 3) {
      path = _wavePath(
        size,
        (x) {
          final phase = x * math.pi * 6;
          return math.sin(
            phase + fmIndex * 5.0 * math.sin(phase * (0.6 + fmRatio * 3.2)),
          );
        },
      );
    } else if (engine == 2) {
      path = _wavePath(
        size,
        (x) {
          final shape = _tableShape((x * 2 + wtPos) % 1, wave);
          return shape * (0.75 + wtPos * 0.2);
        },
      );
    } else {
      path = _wavePath(
        size,
        (x) => _oscWave(x * math.pi * 4, wave),
      );
    }
    final fill = Path.from(path)
      ..lineTo(size.width, size.height)
      ..lineTo(0, size.height)
      ..close();
    canvas.drawPath(
      fill,
      Paint()
        ..shader = LinearGradient(
          begin: Alignment.topCenter,
          end: Alignment.bottomCenter,
          colors: [
            color.withValues(alpha: 0.22),
            color.withValues(alpha: 0.0),
          ],
        ).createShader(Offset.zero & size),
    );
    canvas.drawPath(
      path,
      Paint()
        ..style = PaintingStyle.stroke
        ..strokeWidth = 1.8
        ..color = color,
    );
  }

  double _tableShape(double t, int fallback) {
    // Table index is not available here; use the oscillator fallback plus a
    // harmonic blend so the preview still moves with position and warp.
    final base = _oscWave(t * math.pi * 2, fallback);
    return base * 0.75 + math.sin(t * math.pi * 6 + wtPos * 5) * 0.25;
  }

  Path _grainPath(Size size) {
    final path = Path()..moveTo(0, size.height / 2);
    const steps = 90;
    for (var i = 0; i <= steps; i++) {
      final x = i / steps;
      final envelope = math.sin(x * math.pi);
      final density = 0.4 + grainDensity * 0.9;
      final y = size.height / 2 -
          math.sin(x * math.pi * (4 + grainSize * 14)) *
              size.height *
              0.3 *
              envelope *
              density;
      path.lineTo(x * size.width, y);
    }
    return path;
  }

  @override
  bool shouldRepaint(covariant _EngineWavePainter oldDelegate) =>
      oldDelegate.engine != engine ||
      oldDelegate.color != color ||
      oldDelegate.wave != wave ||
      oldDelegate.wtPos != wtPos ||
      oldDelegate.fmRatio != fmRatio ||
      oldDelegate.fmIndex != fmIndex ||
      oldDelegate.grainSize != grainSize ||
      oldDelegate.grainDensity != grainDensity;
}

double _filterGain(
  int type,
  double frequency,
  double cutoff,
  double resonance,
) {
  final fc = 20 * math.pow(1000, cutoff.clamp(0.0, 1.0));
  final ratio = frequency / fc;
  double gain;
  switch (type) {
    case 1:
      gain = 1 / math.sqrt(1 + math.pow(ratio, 8));
      break;
    case 2:
      gain = 1 / math.sqrt(1 + math.pow(1 / ratio, 4));
      break;
    case 3:
      gain = 1 / math.sqrt(1 + math.pow(1 / ratio, 8));
      break;
    case 4:
      final lr = math.log(ratio + 1e-9) / 0.28;
      gain = math.exp(-lr * lr);
      break;
    case 5:
      final lr = math.log(ratio + 1e-9) / 0.2;
      gain = 1 - 0.92 * math.exp(-lr * lr);
      break;
    default:
      gain = 1 / math.sqrt(1 + math.pow(ratio, 4));
      break;
  }
  final peak = math.exp(-math.pow(math.log(ratio + 1e-9) / 0.16, 2));
  gain = (gain + resonance * peak * 0.85).clamp(0.0, 1.25);
  return gain;
}

class _FilterResponsePainter extends CustomPainter {
  _FilterResponsePainter({
    required this.type,
    required this.cutoff,
    required this.reso,
    required this.color,
  });

  final int type;
  final double cutoff;
  final double reso;
  final Color color;

  @override
  void paint(Canvas canvas, Size size) {
    final grid = Paint()
      ..color = AppColors.border.withValues(alpha: 0.55)
      ..strokeWidth = 1;
    for (var i = 1; i < 6; i++) {
      final x = size.width * i / 6;
      canvas.drawLine(Offset(x, 0), Offset(x, size.height), grid);
    }
    for (var i = 1; i < 4; i++) {
      final y = size.height * i / 4;
      canvas.drawLine(Offset(0, y), Offset(size.width, y), grid);
    }
    final path = Path();
    const steps = 150;
    for (var i = 0; i <= steps; i++) {
      final x = i / steps;
      final f = (20 * math.pow(1000, x)).toDouble();
      final gain = _filterGain(type, f, cutoff, reso);
      final y = size.height - (gain / 1.25) * (size.height - 6) - 3;
      if (i == 0) {
        path.moveTo(x * size.width, y);
      } else {
        path.lineTo(x * size.width, y);
      }
    }
    final fill = Path.from(path)
      ..lineTo(size.width, size.height)
      ..lineTo(0, size.height)
      ..close();
    canvas.drawPath(
      fill,
      Paint()
        ..shader = LinearGradient(
          begin: Alignment.topCenter,
          end: Alignment.bottomCenter,
          colors: [
            color.withValues(alpha: 0.2),
            color.withValues(alpha: 0.0),
          ],
        ).createShader(Offset.zero & size),
    );
    canvas.drawPath(
      path,
      Paint()
        ..style = PaintingStyle.stroke
        ..strokeWidth = 1.8
        ..color = color,
    );
    final fc = 20 * math.pow(1000, cutoff.clamp(0.0, 1.0));
    final fx = math.log(fc / 20) / math.log(1000) * size.width;
    canvas.drawLine(
      Offset(fx, 0),
      Offset(fx, size.height),
      Paint()
        ..color = color.withValues(alpha: 0.35)
        ..strokeWidth = 1,
    );
  }

  @override
  bool shouldRepaint(covariant _FilterResponsePainter oldDelegate) =>
      oldDelegate.type != type ||
      oldDelegate.cutoff != cutoff ||
      oldDelegate.reso != reso ||
      oldDelegate.color != color;
}

class _DualFilterResponsePainter extends CustomPainter {
  _DualFilterResponsePainter({
    required this.type1,
    required this.cutoff1,
    required this.reso1,
    required this.type2,
    required this.cutoff2,
    required this.reso2,
    required this.route,
    required this.balance,
  });

  final int type1;
  final double cutoff1;
  final double reso1;
  final int type2;
  final double cutoff2;
  final double reso2;
  final int route;
  final double balance;

  double _combined(double f) {
    final g1 = _filterGain(type1, f, cutoff1, reso1);
    final g2 = _filterGain(type2, f, cutoff2, reso2);
    if (route == 1) return g1 * g2;
    if (route == 2) return g1 * (1 - balance) + g2 * balance;
    return g1;
  }

  @override
  void paint(Canvas canvas, Size size) {
    final grid = Paint()
      ..color = AppColors.border.withValues(alpha: 0.5)
      ..strokeWidth = 1;
    for (var i = 1; i < 6; i++) {
      final x = size.width * i / 6;
      canvas.drawLine(Offset(x, 0), Offset(x, size.height), grid);
    }
    for (var i = 1; i < 4; i++) {
      final y = size.height * i / 4;
      canvas.drawLine(Offset(0, y), Offset(size.width, y), grid);
    }
    const steps = 160;
    Path makePath(double Function(double) gainFn) {
      final p = Path();
      for (var i = 0; i <= steps; i++) {
        final x = i / steps;
        final f = (20 * math.pow(1000, x)).toDouble();
        final g = gainFn(f).clamp(0.0, 1.25).toDouble();
        final y = size.height - (g / 1.25) * (size.height - 6) - 3;
        if (i == 0) {
          p.moveTo(x * size.width, y);
        } else {
          p.lineTo(x * size.width, y);
        }
      }
      return p;
    }

    final p1 = makePath((f) => _filterGain(type1, f, cutoff1, reso1));
    final p2 = makePath((f) => _filterGain(type2, f, cutoff2, reso2));
    canvas.drawPath(
      p1,
      Paint()
        ..style = PaintingStyle.stroke
        ..strokeWidth = 1.1
        ..color = _audio.withValues(alpha: 0.5),
    );
    canvas.drawPath(
      p2,
      Paint()
        ..style = PaintingStyle.stroke
        ..strokeWidth = 1.1
        ..color = _mod.withValues(alpha: 0.45),
    );

    final combined = makePath(_combined);
    final fill = Path.from(combined)
      ..lineTo(size.width, size.height)
      ..lineTo(0, size.height)
      ..close();
    canvas.drawPath(
      fill,
      Paint()
        ..shader = LinearGradient(
          begin: Alignment.topCenter,
          end: Alignment.bottomCenter,
          colors: [
            _env.withValues(alpha: 0.16),
            _env.withValues(alpha: 0.0),
          ],
        ).createShader(Offset.zero & size),
    );
    canvas.drawPath(
      combined,
      Paint()
        ..style = PaintingStyle.stroke
        ..strokeWidth = 2
        ..color = _env,
    );
  }

  @override
  bool shouldRepaint(covariant _DualFilterResponsePainter oldDelegate) =>
      oldDelegate.type1 != type1 ||
      oldDelegate.cutoff1 != cutoff1 ||
      oldDelegate.reso1 != reso1 ||
      oldDelegate.type2 != type2 ||
      oldDelegate.cutoff2 != cutoff2 ||
      oldDelegate.reso2 != reso2 ||
      oldDelegate.route != route ||
      oldDelegate.balance != balance;
}

class _EnvelopePainter extends CustomPainter {
  _EnvelopePainter({
    required this.attack,
    required this.decay,
    required this.sustain,
    required this.release,
    required this.color,
  });

  final double attack;
  final double decay;
  final double sustain;
  final double release;
  final Color color;

  @override
  void paint(Canvas canvas, Size size) {
    final grid = Paint()
      ..color = AppColors.border.withValues(alpha: 0.5)
      ..strokeWidth = 1;
    for (var i = 1; i < 5; i++) {
      final x = size.width * i / 5;
      canvas.drawLine(Offset(x, 0), Offset(x, size.height), grid);
    }
    for (var i = 1; i < 3; i++) {
      final y = size.height * i / 3;
      canvas.drawLine(Offset(0, y), Offset(size.width, y), grid);
    }

    const pad = 7.0;
    final bottom = size.height - pad;
    final top = pad;
    final aW = 0.04 + attack.clamp(0.0, 1.0) * 0.24;
    final dW = 0.04 + decay.clamp(0.0, 1.0) * 0.18;
    final sW = 0.24;
    final rW = 0.04 + release.clamp(0.0, 1.0) * 0.24;
    final total = aW + dW + sW + rW;
    final aX = aW / total * size.width;
    final dX = (aW + dW) / total * size.width;
    final sX = (aW + dW + sW) / total * size.width;
    final rX = size.width;
    final sustainY = bottom - sustain.clamp(0.0, 1.0) * (bottom - top);

    final path = Path()
      ..moveTo(0, bottom)
      ..lineTo(aX, top)
      ..lineTo(dX, sustainY)
      ..lineTo(sX, sustainY)
      ..lineTo(rX, bottom);
    final fill = Path.from(path)
      ..lineTo(size.width, size.height)
      ..lineTo(0, size.height)
      ..close();
    canvas.drawPath(
      fill,
      Paint()
        ..shader = LinearGradient(
          begin: Alignment.topCenter,
          end: Alignment.bottomCenter,
          colors: [
            color.withValues(alpha: 0.22),
            color.withValues(alpha: 0.015),
          ],
        ).createShader(Offset.zero & size),
    );
    canvas.drawPath(
      path,
      Paint()
        ..style = PaintingStyle.stroke
        ..strokeWidth = 1.8
        ..color = color,
    );

    final dot = Paint()..color = color;
    for (final p in [
      Offset(aX, top),
      Offset(dX, sustainY),
      Offset(sX, sustainY),
      Offset(rX, bottom),
    ]) {
      canvas.drawCircle(p, 2.4, dot);
    }
    _label(canvas, 'A', aX * 0.5, top + 8, color);
    _label(canvas, 'D', (aX + dX) / 2, sustainY - 8, color);
    _label(canvas, 'S', (dX + sX) / 2, sustainY - 8, color);
    _label(canvas, 'R', (sX + rX) / 2, (sustainY + bottom) / 2, color);
  }

  void _label(Canvas canvas, String text, double x, double y, Color color) {
    final tp = TextPainter(
      text: TextSpan(
        text: text,
        style: TextStyle(
          color: color.withValues(alpha: 0.75),
          fontSize: 7,
          fontWeight: FontWeight.w700,
        ),
      ),
      textDirection: TextDirection.ltr,
    )..layout();
    tp.paint(canvas, Offset(x - tp.width / 2, y - tp.height / 2));
  }

  @override
  bool shouldRepaint(covariant _EnvelopePainter oldDelegate) =>
      oldDelegate.attack != attack ||
      oldDelegate.decay != decay ||
      oldDelegate.sustain != sustain ||
      oldDelegate.release != release ||
      oldDelegate.color != color;
}

class _LfoPreviewPainter extends CustomPainter {
  _LfoPreviewPainter({
    required this.shape,
    required this.rate,
    required this.depth,
    required this.color,
  });

  final int shape;
  final double rate;
  final double depth;
  final Color color;

  @override
  void paint(Canvas canvas, Size size) {
    final grid = Paint()
      ..color = AppColors.border.withValues(alpha: 0.35)
      ..strokeWidth = 1;
    canvas.drawLine(
      Offset(0, size.height / 2),
      Offset(size.width, size.height / 2),
      grid,
    );
    final cycles = 1.0 + rate.clamp(0.0, 1.0) * 3.5;
    final amp = size.height * 0.38 * depth.clamp(0.0, 1.0);
    final path = Path();
    const steps = 120;
    for (var i = 0; i <= steps; i++) {
      final x = i / steps;
      late double value;
      if (shape == 4) {
        final step = (x * cycles * 5).floor();
        final pseudo = math.sin(step * 12.9898) * 43758.5453;
        value = (pseudo - pseudo.floor()) * 2 - 1;
      } else {
        value = _oscWave(x * math.pi * 2 * cycles, shape);
      }
      final y = size.height / 2 - value * amp;
      if (i == 0) {
        path.moveTo(x * size.width, y);
      } else {
        path.lineTo(x * size.width, y);
      }
    }
    canvas.drawPath(
      path,
      Paint()
        ..style = PaintingStyle.stroke
        ..strokeWidth = 1.5
        ..color = color,
    );
  }

  @override
  bool shouldRepaint(covariant _LfoPreviewPainter oldDelegate) =>
      oldDelegate.shape != shape ||
      oldDelegate.rate != rate ||
      oldDelegate.depth != depth ||
      oldDelegate.color != color;
}

class _RandomPreviewPainter extends CustomPainter {
  _RandomPreviewPainter({
    required this.rate,
    required this.depth,
    required this.color,
    required this.seed,
  });

  final double rate;
  final double depth;
  final Color color;
  final int seed;

  @override
  void paint(Canvas canvas, Size size) {
    final grid = Paint()
      ..color = AppColors.border.withValues(alpha: 0.35)
      ..strokeWidth = 1;
    canvas.drawLine(
      Offset(0, size.height / 2),
      Offset(size.width, size.height / 2),
      grid,
    );
    final steps = 5 + (rate.clamp(0.0, 1.0) * 10).round();
    final amp = size.height * 0.36 * depth.clamp(0.0, 1.0);
    final path = Path();
    for (var i = 0; i < steps; i++) {
      final pseudo = math.sin((i + seed * 7) * 12.9898) * 43758.5453;
      final value = (pseudo - pseudo.floor()) * 2 - 1;
      final x0 = size.width * i / steps;
      final x1 = size.width * (i + 1) / steps;
      final y = size.height / 2 - value * amp;
      if (i == 0) {
        path.moveTo(x0, y);
      } else {
        path.lineTo(x0, y);
      }
      path.lineTo(x1, y);
    }
    canvas.drawPath(
      path,
      Paint()
        ..style = PaintingStyle.stroke
        ..strokeWidth = 1.4
        ..color = color,
    );
  }

  @override
  bool shouldRepaint(covariant _RandomPreviewPainter oldDelegate) =>
      oldDelegate.rate != rate ||
      oldDelegate.depth != depth ||
      oldDelegate.color != color ||
      oldDelegate.seed != seed;
}

class _WavetablePreviewPainter extends CustomPainter {
  _WavetablePreviewPainter({
    required this.position,
    required this.warp,
    required this.table,
    required this.color,
  });

  final double position;
  final double warp;
  final int table;
  final Color color;

  double _tableWave(double phase) {
    final t = (phase / (2 * math.pi)) % 1.0;
    switch (table) {
      case 1:
        return 2 * t - 1;
      case 2:
        return t < 0.5 ? 1 : -1;
      case 3:
        return math.sin(phase) +
            0.42 * math.sin(phase * 3) +
            0.2 * math.sin(phase * 5);
      default:
        return math.sin(phase);
    }
  }

  @override
  void paint(Canvas canvas, Size size) {
    final grid = Paint()
      ..color = AppColors.border.withValues(alpha: 0.4)
      ..strokeWidth = 1;
    for (var i = 1; i < 6; i++) {
      final x = size.width * i / 6;
      canvas.drawLine(Offset(x, 0), Offset(x, size.height), grid);
    }
    for (var row = 0; row < 5; row++) {
      final path = Path();
      const steps = 120;
      final baseY = size.height * (0.18 + row * 0.16);
      final amp = size.height * 0.13;
      for (var i = 0; i <= steps; i++) {
        final x = i / steps;
        final phase = x * math.pi * 2 + position * 2.5 + row * 0.45;
        var value = _tableWave(phase);
        if (warp > 0.01) {
          value = value.sign * math.pow(value.abs(), 1 - warp * 0.55);
        }
        final y = baseY - value * amp;
        if (i == 0) {
          path.moveTo(x * size.width, y);
        } else {
          path.lineTo(x * size.width, y);
        }
      }
      final highlight = row == 2;
      canvas.drawPath(
        path,
        Paint()
          ..style = PaintingStyle.stroke
          ..strokeWidth = highlight ? 1.7 : 1
          ..color = color.withValues(alpha: highlight ? 0.95 : 0.22),
      );
    }
    final px = size.width * position.clamp(0.0, 1.0);
    canvas.drawLine(
      Offset(px, 0),
      Offset(px, size.height),
      Paint()
        ..color = color.withValues(alpha: 0.35)
        ..strokeWidth = 1,
    );
  }

  @override
  bool shouldRepaint(covariant _WavetablePreviewPainter oldDelegate) =>
      oldDelegate.position != position ||
      oldDelegate.warp != warp ||
      oldDelegate.table != table ||
      oldDelegate.color != color;
}

class _FmPreviewPainter extends CustomPainter {
  _FmPreviewPainter({
    required this.ratio,
    required this.index,
    required this.feedback,
    required this.color,
  });

  final double ratio;
  final double index;
  final double feedback;
  final Color color;

  @override
  void paint(Canvas canvas, Size size) {
    final grid = Paint()
      ..color = AppColors.border.withValues(alpha: 0.4)
      ..strokeWidth = 1;
    for (var i = 1; i < 6; i++) {
      final x = size.width * i / 6;
      canvas.drawLine(Offset(x, 0), Offset(x, size.height), grid);
    }
    canvas.drawLine(
      Offset(0, size.height / 2),
      Offset(size.width, size.height / 2),
      grid,
    );
    final path = Path();
    const steps = 180;
    for (var i = 0; i <= steps; i++) {
      final x = i / steps;
      final phase = x * math.pi * 6;
      final mod = index * 5.5 * math.sin(phase * (0.5 + ratio * 3.5));
      final fb = feedback * 0.8 * math.sin(phase * 0.5);
      final y = size.height / 2 - math.sin(phase + mod + fb) * size.height * 0.34;
      if (i == 0) {
        path.moveTo(x * size.width, y);
      } else {
        path.lineTo(x * size.width, y);
      }
    }
    final fill = Path.from(path)
      ..lineTo(size.width, size.height)
      ..lineTo(0, size.height)
      ..close();
    canvas.drawPath(
      fill,
      Paint()
        ..shader = LinearGradient(
          begin: Alignment.topCenter,
          end: Alignment.bottomCenter,
          colors: [
            color.withValues(alpha: 0.18),
            color.withValues(alpha: 0.0),
          ],
        ).createShader(Offset.zero & size),
    );
    canvas.drawPath(
      path,
      Paint()
        ..style = PaintingStyle.stroke
        ..strokeWidth = 1.7
        ..color = color,
    );
  }

  @override
  bool shouldRepaint(covariant _FmPreviewPainter oldDelegate) =>
      oldDelegate.ratio != ratio ||
      oldDelegate.index != index ||
      oldDelegate.feedback != feedback ||
      oldDelegate.color != color;
}

class _GrainPreviewPainter extends CustomPainter {
  _GrainPreviewPainter({
    required this.size,
    required this.density,
    required this.spray,
    required this.color,
  });

  final double size;
  final double density;
  final double spray;
  final Color color;

  @override
  void paint(Canvas canvas, Size canvasSize) {
    final grid = Paint()
      ..color = AppColors.border.withValues(alpha: 0.35)
      ..strokeWidth = 1;
    canvas.drawLine(
      Offset(0, canvasSize.height / 2),
      Offset(canvasSize.width, canvasSize.height / 2),
      grid,
    );
    final count = (12 + density.clamp(0.0, 1.0) * 34).round();
    final rnd = math.Random(19);
    for (var i = 0; i < count; i++) {
      final t = i / count;
      final jitter = (rnd.nextDouble() - 0.5) * spray * 0.12;
      final x = (t + jitter).clamp(0.0, 1.0) * canvasSize.width;
      final env = math.sin(t * math.pi);
      final grainH = (canvasSize.height * (0.12 + size * 0.6)) * env;
      final y = canvasSize.height / 2 +
          (rnd.nextDouble() - 0.5) * canvasSize.height * spray * 0.5;
      final alpha = (0.22 + env * 0.6).clamp(0.0, 1.0);
      canvas.drawRRect(
        RRect.fromRectAndRadius(
          Rect.fromCenter(
            center: Offset(x, y),
            width: 1.6 + size * 2.2,
            height: grainH.clamp(2.0, canvasSize.height),
          ),
          const Radius.circular(2),
        ),
        Paint()..color = color.withValues(alpha: alpha * 0.8),
      );
    }
  }

  @override
  bool shouldRepaint(covariant _GrainPreviewPainter oldDelegate) =>
      oldDelegate.size != size ||
      oldDelegate.density != density ||
      oldDelegate.spray != spray ||
      oldDelegate.color != color;
}

class _MotionPreviewPainter extends CustomPainter {
  _MotionPreviewPainter({
    required this.amount,
    required this.range,
    required this.color,
  });

  final double amount;
  final double range;
  final Color color;

  @override
  void paint(Canvas canvas, Size size) {
    if (size.width <= 0 || size.height <= 0) return;
    final amount = this.amount.isFinite ? this.amount : 0.0;
    final range = this.range.isFinite ? this.range.clamp(0.0, 1.0) : 0.0;
    final grid = Paint()
      ..color = AppColors.border.withValues(alpha: 0.55)
      ..strokeWidth = 1;
    for (var i = 1; i < 8; i++) {
      final x = size.width * i / 8;
      canvas.drawLine(Offset(x, 0), Offset(x, size.height), grid);
    }
    for (var i = 1; i < 7; i++) {
      final y = size.height * i / 7;
      canvas.drawLine(Offset(0, y), Offset(size.width, y), grid);
    }

    // Note lanes: they give the curve a piano-roll context.
    for (var i = 0; i < 9; i++) {
      final y = size.height * (0.18 + i * 0.08);
      final x0 = size.width * (0.06 + (i * 0.13) % 0.6);
      final x1 = x0 + size.width * 0.13;
      canvas.drawRRect(
        RRect.fromRectAndRadius(
          Rect.fromLTRB(x0, y - 2, x1, y + 2),
          const Radius.circular(2),
        ),
        Paint()..color = AppColors.surfaceHigh,
      );
    }

    final curve = Path();
    const steps = 140;
    for (var i = 0; i <= steps; i++) {
      final x = i / steps;
      final y = size.height * 0.5 +
          math.sin(x * math.pi * 2.1 + 0.6) *
              size.height *
              0.2 *
              amount *
              (0.4 + range * 0.9);
      if (i == 0) {
        curve.moveTo(x * size.width, y);
      } else {
        curve.lineTo(x * size.width, y);
      }
    }
    canvas.drawPath(
      curve,
      Paint()
        ..style = PaintingStyle.stroke
        ..strokeWidth = 7
        ..strokeCap = StrokeCap.round
        ..color = color.withValues(alpha: 0.11),
    );
    canvas.drawPath(
      curve,
      Paint()
        ..style = PaintingStyle.stroke
        ..strokeWidth = 2
        ..strokeCap = StrokeCap.round
        ..color = color,
    );
    for (var i = 0; i < 7; i++) {
      final x = size.width * (0.1 + i * 0.14);
      final y = size.height * 0.5 +
          math.sin((x / size.width) * math.pi * 2.1 + 0.6) *
              size.height *
              0.2 *
              amount *
              (0.4 + range * 0.9);
      canvas.drawLine(
        Offset(x, y),
        Offset(x, size.height * (0.8 + (i % 3) * 0.04)),
        Paint()
          ..color = color.withValues(alpha: 0.15)
          ..strokeWidth = 1,
      );
      canvas.drawCircle(
        Offset(x, y),
        3,
        Paint()..color = color.withValues(alpha: 0.85),
      );
    }
  }

  @override
  bool shouldRepaint(covariant _MotionPreviewPainter oldDelegate) =>
      oldDelegate.amount != amount ||
      oldDelegate.range != range ||
      oldDelegate.color != color;
}

class _MiniKeyboardPainter extends CustomPainter {
  _MiniKeyboardPainter({required this.highlight});
  final Set<int> highlight;

  static const _whitePitchClasses = [0, 2, 4, 5, 7, 9, 11];

  @override
  void paint(Canvas canvas, Size size) {
    const whiteKeys = 15;
    final whiteW = size.width / whiteKeys;
    final whiteH = size.height;
    for (var i = 0; i < whiteKeys; i++) {
      final pc = _whitePitchClasses[i % 7];
      final octave = i ~/ 7;
      final midi = 60 + octave * 12 + pc;
      final on = highlight.contains(midi);
      final rect = Rect.fromLTWH(i * whiteW, 0, whiteW, whiteH);
      canvas.drawRect(
        rect,
        Paint()
          ..color = on
              ? _audio.withValues(alpha: 0.55)
              : const Color(0xFF1C212A),
      );
      canvas.drawLine(
        rect.topLeft,
        rect.bottomLeft,
        Paint()
          ..color = const Color(0xFF090B0F)
          ..strokeWidth = 1,
      );
      if (i % 7 == 0) {
        final tp = TextPainter(
          text: TextSpan(
            text: octave == 0 ? 'C3' : 'C4',
            style: TextStyle(
              color: on ? _ink : AppColors.textFaint,
              fontSize: 6,
            ),
          ),
          textDirection: TextDirection.ltr,
        )..layout();
        tp.paint(
          canvas,
          Offset(i * whiteW + 3, whiteH - tp.height - 2),
        );
      }
    }
    const blackOffsets = [0.7, 1.7, 3.7, 4.7, 5.7];
    const blackPitchClasses = [1, 3, 6, 8, 10];
    final blackW = whiteW * 0.62;
    final blackH = whiteH * 0.62;
    for (var octave = 0; octave < 2; octave++) {
      for (var j = 0; j < blackOffsets.length; j++) {
        final x = (octave * 7 + blackOffsets[j]) * whiteW - blackW / 2;
        final midi = 60 + octave * 12 + blackPitchClasses[j];
        final on = highlight.contains(midi);
        final rect = RRect.fromRectAndRadius(
          Rect.fromLTWH(x, 0, blackW, blackH),
          const Radius.circular(1.4),
        );
        canvas.drawRRect(
          rect,
          Paint()
            ..color = on
                ? _audio.withValues(alpha: 0.8)
                : const Color(0xFF080A0D),
        );
      }
    }
  }

  @override
  bool shouldRepaint(covariant _MiniKeyboardPainter oldDelegate) =>
      oldDelegate.highlight != highlight;
}
