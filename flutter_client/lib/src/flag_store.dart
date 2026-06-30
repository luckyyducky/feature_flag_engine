import 'dart:io';
import 'protocol.dart';

/// In-Memory Copy-on-Write style Flag Store with Offline Fallback support.
class FlagStore {
  final String fallbackFilePath;
  Map<String, FeatureFlag> _flags = {};

  FlagStore({required this.fallbackFilePath}) {
    _loadFromFile();
  }

  /// Returns the current immutable snapshot of all flags.
  Map<String, FeatureFlag> get allFlags => Map.unmodifiable(_flags);

  /// Evaluates whether a flag is enabled for the provided user context.
  bool isEnabled(String key, {UserContext context = const UserContext()}) {
    final flag = _flags[key];
    if (flag == null || !flag.enabled) {
      return false;
    }
    return _evaluateRules(flag, context);
  }

  /// Returns a specific flag or a default inactive flag if missing.
  FeatureFlag getFlag(String key) {
    return _flags[key] ??
        FeatureFlag(
          key: key,
          enabled: false,
          targetGroup: '',
          rolloutPercentage: 100,
          value: null,
        );
  }

  /// Updates or inserts a flag into the local cache and persists to offline storage.
  void setFlag(FeatureFlag flag) {
    // Copy-on-write immutable update
    final updatedMap = Map<String, FeatureFlag>.from(_flags);
    updatedMap[flag.key] = flag;
    _flags = updatedMap;
    _saveToFile();
  }

  /// Evaluates targeting group and percentage rollout rules.
  bool _evaluateRules(FeatureFlag flag, UserContext context) {
    if (!flag.enabled) return false;

    if (flag.targetGroup.isNotEmpty) {
      if (context.group != flag.targetGroup) {
        return false;
      }
    }

    if (flag.rolloutPercentage < 100) {
      if (context.userId.isEmpty) {
        return false;
      }

      // Consistent deterministic bucket calculation (0-99)
      final hashVal = (context.userId.hashCode ^ (flag.key.hashCode * 31)) & 0x7FFFFFFF;
      final bucket = hashVal % 100;
      if (bucket >= flag.rolloutPercentage) {
        return false;
      }
    }

    return true;
  }

  /// Loads fallback cache from local file (CSV-like format: key,enabled,group,rollout,type,val).
  void _loadFromFile() {
    try {
      final file = File(fallbackFilePath);
      if (!file.existsSync()) return;

      final lines = file.readAsLinesSync();
      final newMap = <String, FeatureFlag>{};

      for (final line in lines) {
        if (line.trim().isEmpty) continue;
        final parts = line.split(',');
        if (parts.length < 6) continue;

        final key = parts[0].trim();
        final enabled = parts[1].trim() == '1';
        final group = parts[2].trim();
        final rollout = int.tryParse(parts[3].trim()) ?? 100;
        final typeStr = parts[4].trim();
        final valStr = parts.sublist(5).join(',').trim(); // in case value contains comma

        Object? val;
        if (typeStr == 'b') {
          val = valStr == '1' || valStr.toLowerCase() == 'true';
        } else if (typeStr == 'n') {
          val = double.tryParse(valStr) ?? 0.0;
        } else if (typeStr == 's') {
          val = valStr;
        }

        newMap[key] = FeatureFlag(
          key: key,
          enabled: enabled,
          targetGroup: group,
          rolloutPercentage: rollout,
          value: val,
        );
      }
      _flags = newMap;
    } catch (e) {
      // Ignore read errors; fallback cache is non-fatal
    }
  }

  /// Persists current memory state to the fallback cache file.
  void _saveToFile() {
    try {
      final file = File(fallbackFilePath);
      final sink = file.openWrite();
      for (final flag in _flags.values) {
        String typeStr = 's';
        String valStr = '';
        if (flag.value is bool) {
          typeStr = 'b';
          valStr = (flag.value as bool) ? '1' : '0';
        } else if (flag.value is num) {
          typeStr = 'n';
          valStr = flag.value.toString();
        } else if (flag.value != null) {
          typeStr = 's';
          valStr = flag.value.toString();
        }
        sink.writeln(
          '${flag.key},${flag.enabled ? 1 : 0},${flag.targetGroup},${flag.rolloutPercentage},$typeStr,$valStr',
        );
      }
      sink.close();
    } catch (e) {
      // Ignore file write errors
    }
  }
}
