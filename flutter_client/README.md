# High-Performance Flutter / Dart Feature Flag SDK

A standalone, real-time client SDK for Flutter and Dart applications that connects to the self-hosted C++ Feature Flag and Remote Configuration Engine.

## ✨ Key Features
1. **Real-Time Socket Subscriptions:** Connects via persistent TCP streams (`KCmdSubscribe`). When a flag is toggled on the server (via terminal dashboard or CLI), the Flutter app updates instantly without page refresh or app restart.
2. **Lock-Free COW Memory Cache:** Flags are cached locally using immutable Copy-on-Write (COW) maps, guaranteeing zero lock contention on reader widgets.
3. **Reactive UI with `ChangeNotifier`:** The SDK extends Flutter's `ChangeNotifier`. Simply wrap your widgets in an `AnimatedBuilder` or `ListenableBuilder` to trigger instant reactive UI rebuilds when server configs change.
4. **Targeting & Rollouts:** Full support for User Groups (e.g., `beta`, `internal`) and deterministic percentage rollouts (0-100%).
5. **Offline Fallback & Auto-Reconnect:** If the C++ server is offline, the client boots seamlessly from local offline cache (`client_cache_fallback.txt`) and automatically retries connection in the background with exponential backoff.
6. **100% General & Open-Source:** Uses standard `dart:io` sockets and zero Google-specific or proprietary dependencies. Ready for GitHub export.

---

## 🚀 Quick Start & Integration

### 1. Initialize the SDK Client
```dart
import 'package:feature_flag_client/feature_flag_client.dart';

final client = FeatureFlagClient(
  serverIp: '127.0.0.1',
  port: 8888,
  clientToken: 'client_secret_token_123',
);

// Connect and initiate background streaming updates
await client.start();
```

### 2. Wrap Widgets for Real-Time UI Rebuilds
Use `ListenableBuilder` or `AnimatedBuilder` passing the `client` instance. Your widgets will rebuild automatically the millisecond a flag state changes!

```dart
ListenableBuilder(
  listenable: client,
  builder: (context, child) {
    // Specify target user context for rule evaluation
    final userCtx = UserContext(userId: 'user_99', group: 'beta');

    // Check boolean Feature Flag
    final isNewCheckout = client.isEnabled('new_checkout_flow', context: userCtx);

    // Get Remote Config variable
    final welcomeMsg = client.getValue<String>('welcome_message', 'Hello!', context: userCtx);

    return Column(
      children: [
        Text(welcomeMsg),
        if (isNewCheckout)
          ElevatedButton(onPressed: () {}, child: Text('Beta 1-Click Checkout')),
      ],
    );
  },
);
```

---

## 🏃 Running the Flutter Example App

1. Ensure the **C++ TCP Server** is running on port `8888`:
   ```bash
   cd ../
   ./server
   ```
2. In a new terminal, launch the **Python Terminal UI (TUI) Dashboard**:
   ```bash
   python3 admin_tui.py
   ```
3. Run the **Flutter Demo App**:
   ```bash
   cd flutter_client/example
   flutter run -d linux # or macos / windows
   ```

Now press `[Space]` in your Python TUI terminal dashboard to toggle flags and watch the Flutter UI rebuild instantly in real-time!
