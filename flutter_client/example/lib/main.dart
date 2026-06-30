import 'package:flutter/material.dart';
import 'package:feature_flag_client/feature_flag_client.dart';

void main() async {
  WidgetsFlutterBinding.ensureInitialized();

  // Initialize the real-time client SDK pointing to local C++ TCP Engine
  final client = FeatureFlagClient(
    serverIp: '127.0.0.1',
    port: 8888,
    clientToken: 'client_secret_token_123',
  );

  // Connect and subscribe to real-time socket updates
  await client.start();

  runApp(FeatureFlagDemoApp(client: client));
}

class FeatureFlagDemoApp extends StatefulWidget {
  final FeatureFlagClient client;

  const FeatureFlagDemoApp({super.key, required this.client});

  @override
  State<FeatureFlagDemoApp> createState() => _FeatureFlagDemoAppState();
}

class _FeatureFlagDemoAppState extends State<FeatureFlagDemoApp> {
  String _currentUserId = 'user_99';
  String _currentGroup = 'beta';

  @override
  Widget build(BuildContext context) {
    // ListenableBuilder rebuilds automatically whenever a flag is toggled on the server!
    return ListenableBuilder(
      listenable: widget.client,
      builder: (context, _) {
        final userCtx = UserContext(userId: _currentUserId, group: _currentGroup);

        // Remote Config: Dynamically control theme mode
        final isDarkMode = widget.client.isEnabled('dark_mode_beta', context: userCtx);
        final themeMode = isDarkMode ? ThemeMode.dark : ThemeMode.light;

        // Remote Config: Dynamic welcome message string
        final welcomeMsg = widget.client.getValue<String>(
          'welcome_message',
          'Hello, World! (Default)',
          context: userCtx,
        );

        // Feature Flag: Toggle new checkout flow for beta users
        final showNewCheckout = widget.client.isEnabled('new_checkout_flow', context: userCtx);

        // Feature Flag: Toggle AI recommendations
        final showAiRecs = widget.client.isEnabled('ai_recommendations', context: userCtx);

        return MaterialApp(
          title: 'Feature Flag Real-Time Demo',
          themeMode: themeMode,
          theme: ThemeData(
            colorScheme: ColorScheme.fromSeed(seedColor: Colors.blue, brightness: Brightness.light),
            useMaterial3: true,
          ),
          darkTheme: ThemeData(
            colorScheme: ColorScheme.fromSeed(seedColor: Colors.blue, brightness: Brightness.dark),
            useMaterial3: true,
          ),
          home: Scaffold(
            appBar: AppBar(
              title: const Text('🚀 Remote Config & Flag Controller'),
              actions: [
                Padding(
                  padding: const EdgeInsets.symmetric(horizontal: 16.0),
                  child: Center(
                    child: Chip(
                      avatar: Icon(
                        widget.client.isConnected ? Icons.wifi : Icons.wifi_off,
                        color: widget.client.isConnected ? Colors.green : Colors.orange,
                        size: 18,
                      ),
                      label: Text(widget.client.isConnected ? 'Live Sync' : 'Offline Cache'),
                    ),
                  ),
                ),
              ],
            ),
            body: Padding(
              padding: const EdgeInsets.all(24.0),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.stretch,
                children: [
                  // 1. Remote Config Welcome Message Banner
                  Card(
                    elevation: 4,
                    color: Theme.of(context).colorScheme.primaryContainer,
                    child: Padding(
                      padding: const EdgeInsets.all(20.0),
                      child: Column(
                        children: [
                          Text(
                            'Remote Config Variable',
                            style: Theme.of(context).textTheme.labelLarge,
                          ),
                          const SizedBox(height: 8),
                          Text(
                            welcomeMsg,
                            style: Theme.of(context).textTheme.headlineSmall?.copyWith(
                                  fontWeight: FontWeight.bold,
                                  color: Theme.of(context).colorScheme.onPrimaryContainer,
                                ),
                            textAlign: TextAlign.center,
                          ),
                        ],
                      ),
                    ),
                  ),
                  const SizedBox(height: 24),

                  // 2. User Context Simulation Panel (Group & Rollouts)
                  Card(
                    child: Padding(
                      padding: const EdgeInsets.all(16.0),
                      child: Column(
                        crossAxisAlignment: CrossAxisAlignment.start,
                        children: [
                          Text(
                            '👤 Active User Targeting Context',
                            style: Theme.of(context).textTheme.titleMedium?.copyWith(fontWeight: FontWeight.bold),
                          ),
                          const SizedBox(height: 12),
                          Row(
                            children: [
                              Expanded(
                                child: TextFormField(
                                  initialValue: _currentUserId,
                                  decoration: const InputDecoration(
                                    labelText: 'User ID (for % rollouts)',
                                    border: OutlineInputBorder(),
                                    isDense: true,
                                  ),
                                  onChanged: (val) => setState(() => _currentUserId = val),
                                ),
                              ),
                              const SizedBox(width: 16),
                              Expanded(
                                child: DropdownButtonFormField<String>(
                                  value: _currentGroup,
                                  decoration: const InputDecoration(
                                    labelText: 'Target Group',
                                    border: OutlineInputBorder(),
                                    isDense: true,
                                  ),
                                  items: const [
                                    DropdownMenuItem(value: 'beta', child: Text('Beta Users (beta)')),
                                    DropdownMenuItem(value: 'everyone', child: Text('General (everyone)')),
                                    DropdownMenuItem(value: 'internal', child: Text('Internal Testers')),
                                  ],
                                  onChanged: (val) => setState(() => _currentGroup = val ?? ''),
                                ),
                              ),
                            ],
                          ),
                        ],
                      ),
                    ),
                  ),
                  const SizedBox(height: 24),

                  // 3. Real-Time Feature Flags Grid
                  Text(
                    '⚡ Real-Time Active Flags',
                    style: Theme.of(context).textTheme.titleLarge?.copyWith(fontWeight: FontWeight.bold),
                  ),
                  const SizedBox(height: 12),
                  Expanded(
                    child: ListView(
                      children: [
                        _buildFlagTile(
                          title: 'new_checkout_flow',
                          subtitle: 'Replaces legacy checkout with 1-click ordering (Beta only)',
                          isEnabled: showNewCheckout,
                          icon: Icons.shopping_cart_checkout,
                        ),
                        const SizedBox(height: 12),
                        _buildFlagTile(
                          title: 'dark_mode_beta',
                          subtitle: 'Controls application dark theme dynamically',
                          isEnabled: isDarkMode,
                          icon: Icons.dark_mode,
                        ),
                        const SizedBox(height: 12),
                        _buildFlagTile(
                          title: 'ai_recommendations',
                          subtitle: 'Enables machine learning product recommendations banner',
                          isEnabled: showAiRecs,
                          icon: Icons.auto_awesome,
                        ),
                      ],
                    ),
                  ),
                ],
              ),
            ),
          ),
        );
      },
    );
  }

  Widget _buildFlagTile({
    required String title,
    required String subtitle,
    required bool isEnabled,
    required IconData icon,
  }) {
    return Card(
      color: isEnabled
          ? Colors.green.withOpacity(0.15)
          : Colors.grey.withOpacity(0.1),
      shape: RoundedRectangleBorder(
        side: BorderSide(
          color: isEnabled ? Colors.green : Colors.grey.shade400,
          width: 2,
        ),
        borderRadius: BorderRadius.circular(12),
      ),
      child: ListTile(
        leading: CircleAvatar(
          backgroundColor: isEnabled ? Colors.green : Colors.grey,
          child: Icon(icon, color: Colors.white),
        ),
        title: Text(
          title,
          style: const TextStyle(fontWeight: FontWeight.bold, fontFamily: 'monospace'),
        ),
        subtitle: Text(subtitle),
        trailing: Chip(
          backgroundColor: isEnabled ? Colors.green : Colors.grey.shade400,
          label: Text(
            isEnabled ? 'ON' : 'OFF',
            style: const TextStyle(color: Colors.white, fontWeight: FontWeight.bold),
          ),
        ),
      ),
    );
  }
}
