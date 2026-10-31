// Settings for the external arrangement model.
//
// The key is stored outside ui_settings.json with owner-only permissions. No
// value from this dialog is logged or written into a project.

import 'package:flutter/material.dart';

import '../ai/ai_settings.dart';
import '../l10n/l10n.dart';

const Color _kDialogBg = Color(0xFF252830);
const Color _kTextDim = Color(0xFF9BA1AE);
const Color _kAccent = Color(0xFF4D9DF7);

Future<bool?> showAiSettingsDialog(BuildContext context) {
  return showDialog<bool>(
    context: context,
    builder: (context) => const _AiSettingsBody(),
  );
}

class _AiSettingsBody extends StatefulWidget {
  const _AiSettingsBody();

  @override
  State<_AiSettingsBody> createState() => _AiSettingsBodyState();
}

class _AiSettingsBodyState extends State<_AiSettingsBody> {
  late final TextEditingController _baseUrl = TextEditingController(
    text: AiSettings.baseUrl,
  );
  late final TextEditingController _model = TextEditingController(
    text: AiSettings.model,
  );
  late final TextEditingController _key = TextEditingController();
  late double _temperature = AiSettings.temperature;
  late int _maxTokens = AiSettings.maxTokens;
  bool _showKey = false;
  String? _error;

  @override
  void dispose() {
    _baseUrl.dispose();
    _model.dispose();
    _key.dispose();
    super.dispose();
  }

  void _save() {
    final l = context.l10n;
    final base = _baseUrl.text.trim();
    final model = _model.text.trim();
    final uri = Uri.tryParse(base);
    if (base.isEmpty ||
        uri == null ||
        !(uri.scheme == 'http' || uri.scheme == 'https') ||
        uri.host.isEmpty) {
      setState(() => _error = l.arrangerInvalidUrl);
      return;
    }
    if (model.isEmpty) {
      setState(() => _error = l.arrangerModel);
      return;
    }

    AiSettings.save(
      baseUrl: base,
      model: model,
      // Empty means "keep the saved key"; the Forget button clears it.
      newKey: _key.text.trim().isEmpty ? null : _key.text,
      temperature: _temperature,
      maxTokens: _maxTokens,
    );
    if (!mounted) return;
    Navigator.of(context).pop(true);
  }

  void _forgetKey() {
    AiSettings.save(newKey: '');
    _key.clear();
    setState(() => _showKey = false);
  }

  @override
  Widget build(BuildContext context) {
    final l = context.l10n;
    final local = AiSettings.isLocalEndpoint;

    return AlertDialog(
      backgroundColor: _kDialogBg,
      titlePadding: const EdgeInsets.fromLTRB(20, 18, 20, 0),
      contentPadding: const EdgeInsets.fromLTRB(20, 12, 20, 0),
      actionsPadding: const EdgeInsets.fromLTRB(12, 8, 20, 14),
      title: Row(
        children: [
          const Icon(Icons.tune_rounded, size: 18, color: _kAccent),
          const SizedBox(width: 8),
          Text(l.arrangerSettingsTitle, style: const TextStyle(fontSize: 15)),
        ],
      ),
      content: SizedBox(
        width: 500,
        child: SingleChildScrollView(
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            mainAxisSize: MainAxisSize.min,
            children: [
              Text(
                l.arrangerSettingsHint,
                style: const TextStyle(fontSize: 11, color: _kTextDim),
              ),
              const SizedBox(height: 12),
              TextField(
                controller: _baseUrl,
                decoration: InputDecoration(
                  labelText: l.arrangerBaseUrl,
                  hintText: 'https://openrouter.ai/api/v1',
                  isDense: true,
                  border: const OutlineInputBorder(),
                ),
                style: const TextStyle(fontSize: 12),
              ),
              const SizedBox(height: 10),
              TextField(
                controller: _model,
                decoration: InputDecoration(
                  labelText: l.arrangerModel,
                  hintText: 'openai/gpt-4.1-mini',
                  isDense: true,
                  border: const OutlineInputBorder(),
                ),
                style: const TextStyle(fontSize: 12),
              ),
              const SizedBox(height: 10),
              TextField(
                controller: _key,
                obscureText: !_showKey,
                enableSuggestions: false,
                autocorrect: false,
                decoration: InputDecoration(
                  labelText: l.arrangerApiKey,
                  hintText:
                      AiSettings.hasKey
                          ? l.arrangerApiKeyKept
                          : l.arrangerApiKeyHint,
                  helperText:
                      AiSettings.hasKey
                          ? l.arrangerApiKeyPresent
                          : l.arrangerApiKeyMissing,
                  helperStyle: const TextStyle(fontSize: 10, color: _kTextDim),
                  suffixIcon: IconButton(
                    tooltip: _showKey ? l.arrangerHideKey : l.arrangerShowKey,
                    icon: Icon(
                      _showKey
                          ? Icons.visibility_off_outlined
                          : Icons.visibility_outlined,
                      size: 18,
                    ),
                    onPressed: () => setState(() => _showKey = !_showKey),
                  ),
                  isDense: true,
                  border: const OutlineInputBorder(),
                ),
                style: const TextStyle(fontSize: 12),
              ),
              if (local)
                Padding(
                  padding: const EdgeInsets.only(top: 4),
                  child: Text(
                    l.arrangerLocalNoKey,
                    style: const TextStyle(fontSize: 10, color: _kTextDim),
                  ),
                ),
              const SizedBox(height: 6),
              Align(
                alignment: Alignment.centerLeft,
                child: TextButton.icon(
                  onPressed: _forgetKey,
                  icon: const Icon(Icons.delete_outline, size: 15),
                  label: Text(
                    l.arrangerForgetKey,
                    style: const TextStyle(fontSize: 11),
                  ),
                ),
              ),
              const SizedBox(height: 4),
              Text(
                '${l.arrangerTemperature} ${_temperature.toStringAsFixed(2)}',
                style: const TextStyle(fontSize: 11, color: _kTextDim),
              ),
              Slider(
                value: _temperature,
                min: 0.0,
                max: 1.5,
                divisions: 30,
                activeColor: _kAccent,
                onChanged: (v) => setState(() => _temperature = v),
              ),
              const SizedBox(height: 2),
              Text(
                '${l.arrangerMaxTokens} $_maxTokens',
                style: const TextStyle(fontSize: 11, color: _kTextDim),
              ),
              Slider(
                value: _maxTokens.toDouble(),
                min: 1024,
                max: 16384,
                divisions: 15,
                activeColor: _kAccent,
                onChanged:
                    (v) =>
                        setState(() => _maxTokens = (v / 1024).round() * 1024),
              ),
              if (_error != null)
                Container(
                  margin: const EdgeInsets.only(top: 6),
                  padding: const EdgeInsets.all(8),
                  decoration: BoxDecoration(
                    color: const Color(0x33E5484D),
                    borderRadius: BorderRadius.circular(6),
                    border: Border.all(color: const Color(0x66E5484D)),
                  ),
                  child: Text(
                    _error!,
                    style: const TextStyle(
                      fontSize: 11,
                      color: Color(0xFFFF8A8D),
                    ),
                  ),
                ),
              const SizedBox(height: 8),
              Text(
                l.arrangerKeySecurity,
                style: const TextStyle(fontSize: 10, color: _kTextDim),
              ),
            ],
          ),
        ),
      ),
      actions: [
        TextButton(
          onPressed: () => Navigator.of(context).pop(false),
          child: Text(l.arrangerCancel),
        ),
        FilledButton(
          onPressed: _save,
          style: FilledButton.styleFrom(backgroundColor: _kAccent),
          child: Text(l.arrangerSave),
        ),
      ],
    );
  }
}
