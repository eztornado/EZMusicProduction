// Add-track modal (§7.2): name, color and type (audio / MIDI).

import 'package:flutter/material.dart';

import '../engine/project_snapshot.dart';

class AddTrackResult {
  const AddTrackResult(this.name, this.colorIndex, this.type);
  final String name;
  final int colorIndex;
  final int type; // 0 audio, 1 instrument (MIDI)
}

Future<AddTrackResult?> showAddTrackDialog(
    BuildContext context, int nextColorIndex) {
  return showDialog<AddTrackResult>(
    context: context,
    builder: (_) => Dialog(
      backgroundColor: const Color(0xFF252830),
      child: _AddTrackBody(nextColorIndex: nextColorIndex),
    ),
  );
}

class _AddTrackBody extends StatefulWidget {
  const _AddTrackBody({required this.nextColorIndex});

  final int nextColorIndex;

  @override
  State<_AddTrackBody> createState() => _AddTrackBodyState();
}

class _AddTrackBodyState extends State<_AddTrackBody> {
  late final TextEditingController _name;
  late int _colorIndex;
  int _type = 0; // 0 audio, 1 midi

  @override
  void initState() {
    super.initState();
    _colorIndex = widget.nextColorIndex % kTrackPalette.length;
    _name = TextEditingController(text: 'Pista ${widget.nextColorIndex + 1}');
  }

  @override
  void dispose() {
    _name.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.all(20),
      child: Column(
        mainAxisSize: MainAxisSize.min,
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          const Text('Añadir pista',
              style: TextStyle(fontSize: 15, fontWeight: FontWeight.w600)),
          const SizedBox(height: 16),
          const Text('Nombre',
              style: TextStyle(fontSize: 12, color: Color(0xFF9BA1AE))),
          TextField(
            controller: _name,
            autofocus: true,
            style: const TextStyle(fontSize: 13),
          ),
          const SizedBox(height: 16),
          const Text('Color',
              style: TextStyle(fontSize: 12, color: Color(0xFF9BA1AE))),
          const SizedBox(height: 6),
          Wrap(
            spacing: 6,
            runSpacing: 6,
            children: [
              for (var i = 0; i < kTrackPalette.length; i++)
                GestureDetector(
                  onTap: () => setState(() => _colorIndex = i),
                  child: Container(
                    width: 26,
                    height: 26,
                    decoration: BoxDecoration(
                      color: kTrackPalette[i],
                      shape: BoxShape.circle,
                      border: Border.all(
                          color: i == _colorIndex
                              ? Colors.white
                              : Colors.transparent,
                          width: 2),
                    ),
                  ),
                ),
            ],
          ),
          const SizedBox(height: 16),
          const Text('Tipo',
              style: TextStyle(fontSize: 12, color: Color(0xFF9BA1AE))),
          const SizedBox(height: 6),
          Row(
            children: [
              Expanded(
                child: _TypeButton(
                  icon: Icons.mic,
                  label: 'Audio',
                  selected: _type == 0,
                  onTap: () => setState(() => _type = 0),
                ),
              ),
              const SizedBox(width: 8),
              Expanded(
                child: _TypeButton(
                  icon: Icons.piano,
                  label: 'MIDI',
                  selected: _type == 1,
                  onTap: () => setState(() => _type = 1),
                ),
              ),
            ],
          ),
          const SizedBox(height: 20),
          Row(
            mainAxisAlignment: MainAxisAlignment.end,
            children: [
              TextButton(
                onPressed: () => Navigator.pop(context),
                child: const Text('Cancelar'),
              ),
              const SizedBox(width: 8),
              FilledButton(
                onPressed: () => Navigator.pop(
                    context,
                    AddTrackResult(
                        _name.text.trim().isEmpty
                            ? 'Pista'
                            : _name.text.trim(),
                        _colorIndex,
                        _type)),
                child: const Text('Crear pista'),
              ),
            ],
          ),
        ],
      ),
    );
  }
}

class _TypeButton extends StatelessWidget {
  const _TypeButton({
    required this.icon,
    required this.label,
    required this.selected,
    required this.onTap,
  });

  final IconData icon;
  final String label;
  final bool selected;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    return GestureDetector(
      onTap: onTap,
      child: Container(
        height: 44,
        decoration: BoxDecoration(
          color: selected
              ? const Color(0xFF4D9DF7).withValues(alpha: 0.18)
              : const Color(0xFF2C303A),
          borderRadius: BorderRadius.circular(6),
          border: Border.all(
              color: selected ? const Color(0xFF4D9DF7) : const Color(0xFF3E4350),
              width: selected ? 2 : 1),
        ),
        child: Row(
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            Icon(icon,
                size: 18,
                color: selected
                    ? const Color(0xFF4D9DF7)
                    : const Color(0xFF9BA1AE)),
            const SizedBox(width: 8),
            Text(label,
                style: TextStyle(
                    fontSize: 12,
                    fontWeight: FontWeight.w600,
                    color: selected
                        ? const Color(0xFFE9EBF0)
                        : const Color(0xFF9BA1AE))),
          ],
        ),
      ),
    );
  }
}
