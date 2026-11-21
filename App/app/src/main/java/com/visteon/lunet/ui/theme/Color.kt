package com.visteon.lunet.ui.theme

import androidx.compose.ui.graphics.Color

// Visteon Signature Accent
val VisteonOrange = Color(0xFFF37B20)
val VisteonOrangeGlow = Color(0x33F37B20)

// Obsidian Dark Palette (Lunet Cluster Match)
val ObsidianBg = Color(0xFF06090F)
val ElevatedCarbon = Color(0xFF0E1521)
val PanelAltDark = Color(0xFF162133)
val SurfaceDeepDark = Color(0xFF0A1019)
val HairlineBorderDark = Color(0xFF1E2C42)
val TextPrimaryDark = Color(0xFFF4F7FB)
val TextMutedDark = Color(0xFF8094AD)
val ElectricCyanDark = Color(0xFF00D4FF)
val EmeraldGreenDark = Color(0xFF10B981)
val AmberWarningDark = Color(0xFFF59E0B)
val CrimsonAlertDark = Color(0xFFEF4444)
val RoadDark = Color(0xFF131C2B)

// Ceramic Light Palette (Lunet Cluster Match)
val CeramicBg = Color(0xFFEFF3F8)
val ElevatedCeramic = Color(0xFFFFFFFF)
val PanelAltLight = Color(0xFFE2E9F2)
val SurfaceDeepLight = Color(0xFFE2E9F2)
val HairlineBorderLight = Color(0xFFCDD8E6)
val TextPrimaryLight = Color(0xFF0F172A)
val TextMutedLight = Color(0xFF5B6E84)
val ElectricCyanLight = Color(0xFF0284C7)
val EmeraldGreenLight = Color(0xFF059669)
val AmberWarningLight = Color(0xFFD97706)
val CrimsonAlertLight = Color(0xFFDC2626)
val RoadLight = Color(0xFFCBD5E1)

data class LunetColors(
    val isDark: Boolean,
    val bg: Color,
    val surface: Color,
    val panelAlt: Color,
    val surfaceDeep: Color,
    val border: Color,
    val text: Color,
    val textMuted: Color,
    val cyan: Color,
    val green: Color,
    val amber: Color,
    val red: Color,
    val road: Color,
    val accent: Color = VisteonOrange
)

val DarkLunetColors = LunetColors(
    isDark = true,
    bg = ObsidianBg,
    surface = ElevatedCarbon,
    panelAlt = PanelAltDark,
    surfaceDeep = SurfaceDeepDark,
    border = HairlineBorderDark,
    text = TextPrimaryDark,
    textMuted = TextMutedDark,
    cyan = ElectricCyanDark,
    green = EmeraldGreenDark,
    amber = AmberWarningDark,
    red = CrimsonAlertDark,
    road = RoadDark
)

val LightLunetColors = LunetColors(
    isDark = false,
    bg = CeramicBg,
    surface = ElevatedCeramic,
    panelAlt = PanelAltLight,
    surfaceDeep = SurfaceDeepLight,
    border = HairlineBorderLight,
    text = TextPrimaryLight,
    textMuted = TextMutedLight,
    cyan = ElectricCyanLight,
    green = EmeraldGreenLight,
    amber = AmberWarningLight,
    red = CrimsonAlertLight,
    road = RoadLight
)
