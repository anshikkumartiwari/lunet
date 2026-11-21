# Visteon Lunet OS 3.0 & Companion App

An automotive digital car cluster running on **ESP32-S3** (800x480 Waveshare 4.3" RGB LCD with GT911 touch) paired with a flagship **Android Companion App** built with **Kotlin** and **Jetpack Compose**.

```
LunetCarCluster
├─ 📁 App                          # Native Android Companion App (Kotlin, Compose, BLE 5.0)
│  ├─ 📁 app
│  │  ├─ build.gradle.kts
│  │  └─ src/main/...
│  ├─ 📁 gradle
│  │  ├─ libs.versions.toml
│  │  └─ wrapper/...
│  ├─ build.gradle.kts
│  ├─ settings.gradle.kts
│  └─ gradle.properties
└─ 📁 CarCluster_Connectivity      # ESP32-S3 Smart Cluster Firmware (LVGL 8.4)
   ├─ CarCluster_Connectivity.ino
   ├─ esp_panel_board_supported_conf.h
   ├─ esp_panel_drivers_conf.h
   ├─ FILES.txt
   ├─ lv_conf.h
   ├─ README.txt
   └─ v.png
```

---

## Android Companion App (`App/`)
- **UI Architecture**: Jetpack Compose, Material 3, StateFlow, custom geometric Visteon vector branding, Obsidian Dark & Ceramic Light theme toggles.
- **BLE 5.0 Engine**: Native Android `BluetoothGatt` manager with serialized `Channel<String>` write queue, 150-byte safety clamping, MTU 247 negotiation, and auto-reconnect inside a Foreground Service (`foregroundServiceType="connectedDevice"`).
- **Two-Way Media Sync**: Automatic `MediaSessionManager` integration (Spotify, YouTube Music, local players) beaming track metadata, play/pause state, progress, and volume (`MEDIA:...`), with incoming cluster playback/volume commands (`CMD:MEDIA:...`).
- **Google Maps Navigation Bridge**: Turn-by-turn notification parser extracting maneuver direction, distance, street name, ETA, and remaining distance (`NAV:...`).
- **Real Phone Telephony**: Incoming caller ID lookup (`ContactsContract`), incoming call alerts (`CALL:INCOMING:...`), and remote call accept/decline execution (`CMD:CALL:ACCEPT` / `DECLINE`).
- **Anti-Loss Phone Finder**: Incoming `CMD:PHONE:PING` activates maximum-volume `USAGE_ALARM` sound, rhythmic vibration waveform, and an in-app emergency overlay.
- **Auto Telemetry Sync**: Automatically pushes `TIME:`, `DATE:`, `PBATT:`, and live Open-Meteo `WEATHER:`.
- **Demo Remote Deck**: Direct live tactile control over cluster Speed, Speed Limit (red overspeed flash trigger), Gear (`P/R/N/D`), Mode (`ECO/COMFORT/SPORT`), and EV SoC.
