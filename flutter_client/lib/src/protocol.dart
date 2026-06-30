import 'dart:typed_data';

/// Protocol Constants matching C++ TCP Server (protocol.h)
class ProtocolConstants {
  static const int kProtocolMagic = 0x46; // 'F' in ASCII

  // Command Types
  static const int kCmdCheck = 0x01;
  static const int kCmdSubscribe = 0x02;
  static const int kCmdSetFlag = 0x04;
  static const int kCmdAuth = 0x05;

  // Response Statuses
  static const int kStatusOk = 0x00;
  static const int kStatusError = 0x01;
  static const int kStatusInvalidMsg = 0x02;
  static const int kStatusUnauthorized = 0x03;
  static const int kStatusForbidden = 0x04;

  // Value Type Tags
  static const int kTagBool = 0x01;
  static const int kTagDouble = 0x02;
  static const int kTagString = 0x03;
}

/// Represents the active user context used for evaluation rules (group targeting & percentage rollout).
class UserContext {
  final String userId;
  final String group;

  const UserContext({
    this.userId = '',
    this.group = '',
  });
}

/// Represents a single Feature Flag or Remote Config item.
class FeatureFlag {
  final String key;
  final bool enabled;
  final String targetGroup;
  final int rolloutPercentage;
  final Object? value; // bool, double, or String

  const FeatureFlag({
    required this.key,
    required this.enabled,
    this.targetGroup = '',
    this.rolloutPercentage = 100,
    this.value,
  });

  FeatureFlag copyWith({
    String? key,
    bool? enabled,
    String? targetGroup,
    int? rolloutPercentage,
    Object? value,
  }) {
    return FeatureFlag(
      key: key ?? this.key,
      enabled: enabled ?? this.enabled,
      targetGroup: targetGroup ?? this.targetGroup,
      rolloutPercentage: rolloutPercentage ?? this.rolloutPercentage,
      value: value ?? this.value,
    );
  }
}

/// Binary serialization and deserialization helpers for Big Endian uint16 and IEEE 754 float64.
class ProtocolSerializer {
  static void packUint16(BytesBuilder builder, int value) {
    builder.addByte((value >> 8) & 0xFF);
    builder.addByte(value & 0xFF);
  }

  static int unpackUint16(List<int> buffer, int offset) {
    return ((buffer[offset] & 0xFF) << 8) | (buffer[offset + 1] & 0xFF);
  }

  static double unpackDouble(List<int> buffer, int offset) {
    final byteData = ByteData.sublistView(Uint8List.fromList(buffer.sublist(offset, offset + 8)));
    return byteData.getFloat64(0, Endian.little);
  }

  static List<int> packDouble(double value) {
    final byteData = ByteData(8);
    byteData.setFloat64(0, value, Endian.little);
    return byteData.buffer.asUint8List();
  }
}
