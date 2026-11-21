package com.visteon.lunet.ui

import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.togetherWith
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.visteon.lunet.ble.BleConnectionState
import com.visteon.lunet.ui.components.FloatingNavBar
import com.visteon.lunet.ui.components.PingAlarmOverlay
import com.visteon.lunet.ui.components.VisteonLogo
import com.visteon.lunet.ui.screens.DemoScreen
import com.visteon.lunet.ui.screens.HomeScreen
import com.visteon.lunet.ui.screens.SyncScreen
import com.visteon.lunet.ui.theme.LunetTheme
import com.visteon.lunet.ui.theme.VisteonOrange
import com.visteon.lunet.viewmodel.LunetViewModel
import com.visteon.lunet.viewmodel.ScreenTab
import kotlin.math.cos
import kotlin.math.sin

/**
 * Ultra-Clean OEM Automotive Scaffold:
 * - Top bar: Vector Visteon logo + "Lunet" wordmark (left), compact live status pill (center), Sun/Moon toggle (right)
 * - 3 Minimalist Tabs: Home (Live Mirror), Sync (Bridges & Permissions), Demo (Presentation Remote)
 * - Sleek Floating Capsule Dock
 * - Emergency Anti-Loss Ping Alarm Overlay
 */
@Composable
fun LunetMainScreen(viewModel: LunetViewModel) {
    val colors = LunetTheme.colors
    val haptic = LocalHapticFeedback.current

    val currentTab by viewModel.currentTab.collectAsState()
    val isDarkMode by viewModel.isDarkMode.collectAsState()
    val connectionState by viewModel.connectionState.collectAsState()
    val isAlarmActive by viewModel.isAlarmActive.collectAsState()

    Box(
        modifier = Modifier
            .fillMaxSize()
            .background(colors.bg)
    ) {
        Column(
            modifier = Modifier
                .fillMaxSize()
                .statusBarsPadding()
        ) {
            // Ultra-Clean Top Bar: Logo+Wordmark (Left), Status Pill (Center), Sun/Moon (Right)
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 16.dp, vertical = 10.dp),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.SpaceBetween
            ) {
                // Left: Visteon Logo + Lunet wordmark
                Row(
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    VisteonLogo(size = 22.dp)
                    Spacer(modifier = Modifier.width(8.dp))
                    Text(
                        text = "Lunet",
                        color = colors.text,
                        fontSize = 17.sp,
                        fontWeight = FontWeight.Bold,
                        fontFamily = FontFamily.SansSerif,
                        letterSpacing = 0.5.sp
                    )
                }

                // Center: Compact live status pill
                val (dotColor, statusLabel) = when (connectionState) {
                    BleConnectionState.CONNECTED -> Pair(colors.green, "Connected")
                    BleConnectionState.SCANNING, BleConnectionState.CONNECTING -> Pair(colors.cyan, "Scanning")
                    BleConnectionState.DISCONNECTED -> Pair(colors.textMuted, "Offline")
                }
                Box(
                    modifier = Modifier
                        .clip(RoundedCornerShape(12.dp))
                        .background(colors.surface)
                        .border(1.dp, colors.border, RoundedCornerShape(12.dp))
                        .padding(horizontal = 10.dp, vertical = 4.dp)
                ) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Box(
                            modifier = Modifier
                                .size(6.dp)
                                .clip(CircleShape)
                                .background(dotColor)
                        )
                        Spacer(modifier = Modifier.width(6.dp))
                        Text(
                            text = statusLabel,
                            color = dotColor,
                            fontSize = 11.sp,
                            fontWeight = FontWeight.Bold,
                            fontFamily = FontFamily.Monospace,
                            letterSpacing = 0.5.sp
                        )
                    }
                }

                // Right: Icon-only Sun / Moon Theme Toggle
                Box(
                    modifier = Modifier
                        .size(34.dp)
                        .clip(CircleShape)
                        .background(colors.surface)
                        .border(1.dp, colors.border, CircleShape)
                        .clickable(
                            interactionSource = remember { MutableInteractionSource() },
                            indication = null
                        ) {
                            haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                            viewModel.toggleTheme()
                        },
                    contentAlignment = Alignment.Center
                ) {
                    ThemeIconVector(
                        isDark = isDarkMode,
                        color = if (isDarkMode) colors.amber else colors.cyan
                    )
                }
            }

            Spacer(modifier = Modifier.height(4.dp))

            // Body: Animated switching across the 3 tabs
            Box(modifier = Modifier.weight(1f)) {
                AnimatedContent(
                    targetState = currentTab,
                    transitionSpec = { fadeIn() togetherWith fadeOut() },
                    label = "tabTransition"
                ) { tab ->
                    when (tab) {
                        ScreenTab.HOME -> HomeScreen(viewModel)
                        ScreenTab.SYNC -> SyncScreen(viewModel)
                        ScreenTab.DEMO -> DemoScreen(viewModel)
                    }
                }
            }
        }

        // Floating Frosted Bottom Navigation Dock (3 tabs: Home, Sync, Demo)
        FloatingNavBar(
            selectedTab = currentTab,
            onTabSelected = { viewModel.selectTab(it) },
            modifier = Modifier.align(Alignment.BottomCenter)
        )

        // Emergency Full-Screen Anti-Loss Ping Alarm Overlay
        PingAlarmOverlay(
            isAlarmActive = isAlarmActive,
            onStopAlarm = { viewModel.stopAlarm() }
        )
    }
}

/**
 * Geometric vector reconstruction of Sun (in Dark mode) and Crescent Moon (in Light mode)
 * matching CarCluster_Connectivity.ino drawThemeIconCb.
 */
@Composable
private fun ThemeIconVector(isDark: Boolean, color: Color) {
    Canvas(modifier = Modifier.size(17.dp)) {
        val cx = size.width / 2
        val cy = size.height / 2

        if (isDark) {
            drawCircle(color = color, radius = 3.8.dp.toPx(), center = Offset(cx, cy))
            for (i in 0 until 8) {
                val angle = Math.toRadians((i * 45).toDouble())
                val r1 = 5.6.dp.toPx()
                val r2 = 8.0.dp.toPx()
                drawLine(
                    color = color,
                    start = Offset((cx + r1 * cos(angle)).toFloat(), (cy + r1 * sin(angle)).toFloat()),
                    end = Offset((cx + r2 * cos(angle)).toFloat(), (cy + r2 * sin(angle)).toFloat()),
                    strokeWidth = 1.4.dp.toPx()
                )
            }
        } else {
            val path = Path().apply {
                moveTo(cx - 3.dp.toPx(), cy - 6.dp.toPx())
                cubicTo(
                    cx + 6.dp.toPx(), cy - 5.dp.toPx(),
                    cx + 6.dp.toPx(), cy + 5.dp.toPx(),
                    cx - 3.dp.toPx(), cy + 6.dp.toPx()
                )
                cubicTo(
                    cx + 2.dp.toPx(), cy + 3.dp.toPx(),
                    cx + 2.dp.toPx(), cy - 3.dp.toPx(),
                    cx - 3.dp.toPx(), cy - 6.dp.toPx()
                )
                close()
            }
            drawPath(path, color = color)
        }
    }
}
