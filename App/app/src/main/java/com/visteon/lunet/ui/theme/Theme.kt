package com.visteon.lunet.ui.theme

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.ReadOnlyComposable
import androidx.compose.runtime.staticCompositionLocalOf

val LocalLunetColors = staticCompositionLocalOf { DarkLunetColors }

object LunetTheme {
    val colors: LunetColors
        @Composable
        @ReadOnlyComposable
        get() = LocalLunetColors.current
}

@Composable
fun LunetAppTheme(
    isDark: Boolean = true,
    content: @Composable () -> Unit
) {
    val palette = if (isDark) DarkLunetColors else LightLunetColors
    val materialColors = if (isDark) {
        darkColorScheme(
            primary = VisteonOrange,
            secondary = palette.cyan,
            tertiary = palette.green,
            background = palette.bg,
            surface = palette.surface,
            onPrimary = palette.bg,
            onSecondary = palette.bg,
            onBackground = palette.text,
            onSurface = palette.text,
            outline = palette.border
        )
    } else {
        lightColorScheme(
            primary = VisteonOrange,
            secondary = palette.cyan,
            tertiary = palette.green,
            background = palette.bg,
            surface = palette.surface,
            onPrimary = palette.surface,
            onSecondary = palette.surface,
            onBackground = palette.text,
            onSurface = palette.text,
            outline = palette.border
        )
    }

    CompositionLocalProvider(LocalLunetColors provides palette) {
        MaterialTheme(
            colorScheme = materialColors,
            typography = LunetTypography,
            content = content
        )
    }
}
