import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import 'services/ble_service.dart';
import 'screens/home_screen.dart';

void main() {
  runApp(
    ChangeNotifierProvider(
      create: (_) => BleService(),
      child: const InclinoCarApp(),
    ),
  );
}

class InclinoCarApp extends StatelessWidget {
  const InclinoCarApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'InclinoCar',
      debugShowCheckedModeBanner: false,
      theme: ThemeData.dark().copyWith(
        scaffoldBackgroundColor: const Color(0xFF0D1A0D),
        colorScheme: const ColorScheme.dark(
          primary: Color(0xFF4CAF50),
          surface: Color(0xFF111E11),
        ),
      ),
      home: const HomeScreen(),
    );
  }
}
