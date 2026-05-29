class BleUuids {
  static const String service    = '4fafc201-1fb5-459e-8fcc-c5c9c331914b';
  static const String pitch      = 'beb5483e-36e1-4688-b7f5-ea07361b26a8';
  static const String roll       = 'beb5483f-36e1-4688-b7f5-ea07361b26a8';
  static const String calibrate  = 'beb54842-36e1-4688-b7f5-ea07361b26a8'; // Write 0x01 to trigger
  static const String status     = 'beb54843-36e1-4688-b7f5-ea07361b26a8'; // Notify: 0x01 = cal done
}

class AppConstants {
  static const String deviceName       = 'InclinoCar';
  static const double levelThreshold   = 0.5;
  static const double warningThreshold = 2.0;
}
