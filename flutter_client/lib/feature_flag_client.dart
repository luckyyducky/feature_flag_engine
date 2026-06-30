import 'dart:async';
import 'dart:io';
import 'dart:typed_data';
import 'package:flutter/foundation.dart';
import 'src/flag_store.dart';
import 'src/protocol.dart';

export 'src/protocol.dart' show UserContext, FeatureFlag;

/// The primary client SDK for integrating the Feature Flag & Remote Config Engine into a Flutter application.
/// 
/// Extends [ChangeNotifier] so Flutter widgets (like [AnimatedBuilder] or [ListenableBuilder])
/// can rebuild in real-time the instant a flag is toggled on the server.
class FeatureFlagClient extends ChangeNotifier {
  final String serverIp;
  final int port;
  final String clientToken;
  final FlagStore _store;

  Socket? _socket;
  bool _isRunning = false;
  bool _isConnected = false;
  int _backoffSeconds = 1;
  final List<int> _buffer = [];

  FeatureFlagClient({
    required this.serverIp,
    required this.port,
    this.clientToken = 'client_secret_token_123',
    String fallbackFilePath = 'client_cache_fallback.txt',
  }) : _store = FlagStore(fallbackFilePath: fallbackFilePath);

  /// Returns true if currently connected to the live C++ TCP Server.
  bool get isConnected => _isConnected;

  /// Returns all currently cached flags.
  Map<String, FeatureFlag> get allFlags => _store.allFlags;

  /// Starts the client, connects to the server, and initiates real-time push subscriptions.
  Future<bool> start() async {
    if (_isRunning) return _isConnected;
    _isRunning = true;

    _connectAndSubscribe();
    return true;
  }

  /// Stops the client and closes the active network connection.
  void stop() {
    _isRunning = false;
    _disconnect();
  }

  /// Evaluates if a feature flag is currently active for the given [context].
  bool isEnabled(String key, {UserContext context = const UserContext()}) {
    return _store.isEnabled(key, context: context);
  }

  /// High-performance remote configuration lookup.
  /// Returns the typed config value if enabled and matching type [T], otherwise [defaultValue].
  T getValue<T>(String key, T defaultValue, {UserContext context = const UserContext()}) {
    if (!isEnabled(key, context: context)) {
      return defaultValue;
    }

    final flag = _store.getFlag(key);
    if (flag.value is T) {
      return flag.value as T;
    }
    return defaultValue;
  }

  /// Internal connection loop with automatic reconnection & exponential backoff.
  Future<void> _connectAndSubscribe() async {
    while (_isRunning) {
      try {
        final socket = await Socket.connect(serverIp, port, timeout: const Duration(seconds: 3));
        _socket = socket;

        // 1. Send CMD_AUTH Handshake
        final tokenBytes = clientToken.codeUnits;
        final authHeader = BytesBuilder()
          ..addByte(ProtocolConstants.kProtocolMagic)
          ..addByte(ProtocolConstants.kCmdAuth);
        ProtocolSerializer.packUint16(authHeader, tokenBytes.length);
        authHeader.add(tokenBytes);
        socket.add(authHeader.takeBytes());

        // 2. Send CMD_SUBSCRIBE Handshake
        final subHeader = BytesBuilder()
          ..addByte(ProtocolConstants.kProtocolMagic)
          ..addByte(ProtocolConstants.kCmdSubscribe);
        ProtocolSerializer.packUint16(subHeader, 0);
        socket.add(subHeader.takeBytes());

        _isConnected = true;
        _backoffSeconds = 1; // Reset backoff on successful connection
        notifyListeners();

        // 3. Listen to incoming push update stream
        await for (final data in socket) {
          _buffer.addAll(data);
          _processBuffer();
        }
      } catch (e) {
        // Connection failed or lost; fall back to local offline cache
      } finally {
        _disconnect();
      }

      if (_isRunning) {
        await Future.delayed(Duration(seconds: _backoffSeconds));
        _backoffSeconds = (_backoffSeconds * 2).clamp(1, 60);
      }
    }
  }

  /// Parses TLV push update packets from the incoming stream buffer.
  void _processBuffer() {
    while (_buffer.length >= 5) {
      final magic = _buffer[0];
      final eventType = _buffer[1];
      final valueByte = _buffer[2];
      final rolloutPercentage = _buffer[3];
      final groupLen = _buffer[4];

      if (magic != ProtocolConstants.kProtocolMagic || eventType != 0x03) {
        // Corrupted packet or unhandled event type; discard byte and resync
        _buffer.removeAt(0);
        continue;
      }

      // Check if we have enough bytes for group + nameLen
      if (_buffer.length < 5 + groupLen + 2) {
        break; // Wait for more data from socket
      }

      final group = String.fromCharCodes(_buffer.sublist(5, 5 + groupLen));
      final nameLenOffset = 5 + groupLen;
      final nameLen = ProtocolSerializer.unpackUint16(_buffer, nameLenOffset);

      if (_buffer.length < nameLenOffset + 2 + nameLen + 1) {
        break; // Wait for more data from socket
      }

      final nameOffset = nameLenOffset + 2;
      final flagName = String.fromCharCodes(_buffer.sublist(nameOffset, nameOffset + nameLen));
      final tagOffset = nameOffset + nameLen;
      final tag = _buffer[tagOffset];

      int totalPacketSize = tagOffset + 1;
      Object? flagVal;

      if (tag == ProtocolConstants.kTagBool) {
        if (_buffer.length < totalPacketSize + 1) break;
        flagVal = (_buffer[totalPacketSize] == 0x01);
        totalPacketSize += 1;
      } else if (tag == ProtocolConstants.kTagDouble) {
        if (_buffer.length < totalPacketSize + 8) break;
        flagVal = ProtocolSerializer.unpackDouble(_buffer, totalPacketSize);
        totalPacketSize += 8;
      } else if (tag == ProtocolConstants.kTagString) {
        if (_buffer.length < totalPacketSize + 2) break;
        final strLen = ProtocolSerializer.unpackUint16(_buffer, totalPacketSize);
        if (_buffer.length < totalPacketSize + 2 + strLen) break;
        flagVal = String.fromCharCodes(_buffer.sublist(totalPacketSize + 2, totalPacketSize + 2 + strLen));
        totalPacketSize += 2 + strLen;
      } else {
        // Unknown tag; discard magic byte and retry
        _buffer.removeAt(0);
        continue;
      }

      // We have a complete, verified TLV packet!
      final enabled = (valueByte == 0x01);
      final updatedFlag = FeatureFlag(
        key: flagName,
        enabled: enabled,
        targetGroup: group,
        rolloutPercentage: rolloutPercentage,
        value: flagVal,
      );

      _store.setFlag(updatedFlag);
      _buffer.removeRange(0, totalPacketSize);

      // Notify all listening Flutter widgets to trigger a real-time UI rebuild!
      notifyListeners();
    }
  }

  void _disconnect() {
    if (_isConnected) {
      _isConnected = false;
      notifyListeners();
    }
    _socket?.destroy();
    _socket = null;
  }
}
