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
import androidx.compose.foundation.shape.CircleShape
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
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.unit.dp
import com.visteon.lunet.ui.components.DynamicStatusPill
import com.visteon.lunet.ui.components.FloatingNavBar
import com.visteon.lunet.ui.components.PingAlarmOverlay
import com.visteon.lunet.ui.components.VisteonHeaderTitle
import com.visteon.lunet.ui.screens.BridgeScreen
import com.visteon.lunet.ui.screens.CockpitScreen
import com.visteon.lunet.ui.screens.ConsoleScreen
import com.visteon.lunet.ui.screens.DemoRemoteScreen
import com.visteon.lunet.ui.theme.LunetTheme
import com.visteon.lunet.ui.theme.VisteonOrange
import com.visteon.lunet.viewmodel.LunetViewModel
import com.visteon.lunet.viewmodel.ScreenTab
import kotlin.math.cos
import kotlin.math.sin

/**
 * Main Automotive Scaffold:
 * - Visteon Header + Cluster-identical Sun/Moon vector theme toggle
 * - Dynamic Status Pill
 * - 4 Animated Screens
 * - Floating Frosted Bottom Navigation Pill
 * - Emergency Anti-Loss Ping Alarm Overlay
 */
@Composable
fun LunetMainScreen(viewModel: LunetViewModel) {
    val colors = LunetTheme.colors
    val haptic = LocalHapticFeedback.current

    val currentTab by viewModel.currentTab.collectAsState()
    val isDarkMode by viewModel.isDarkMode.collectAsState()
    val connectionState by viewModel.connectionState.collectAsState()
    val telemetry by viewModel.telemetry.collectAsState()
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
            // Top Header: Visteon Lunet Branding + Sun/Moon Theme Toggle
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 16.dp, vertical = 12.dp),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.SpaceBetween
            ) {
                VisteonHeaderTitle()

                // Cluster-matching Vector Sun / Moon Icon Toggle
                Box(
                    modifier = Modifier
                        .size(36.dp)
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
                    ThemeIconVector(isDark = isDarkMode, color = if (isDarkMode) colors.amber else colors.cyan)
                }
            }

            // Dynamic Status Pill
            DynamicStatusPill(
                connectionState = connectionState,
                telemetry = telemetry,
                modifier = Modifier.padding(horizontal = 16.dp, vertical = 4.dp)
            )

            Spacer(modifier = Modifier.height(6.dp))

            // Screen Body
            Box(modifier = Modifier.weight(1f)) {
                AnimatedContent(
                    targetState = currentTab,
                    transitionSpec = { fadeIn() togetherWith fadeOut() },
                    label = "tabTransition"
                ) { tab ->
                    when (tab) {
                        ScreenTab.COCKPIT -> CockpitScreen(viewModel)
                        ScreenTab.BRIDGE -> BridgeScreen(viewModel)
                        ScreenTab.DEMO_REMOTE -> DemoRemoteScreen(viewModel)
                        ScreenTab.CONSOLE -> ConsoleScreen(viewModel)
                    }
                }
            }
        }

        // Floating Frosted Bottom Navigation Bar
        FloatingNavBar(
            selectedTab = currentTab,
            onTabSelected = { viewModel.selectTab(it) },
            modifier = Modifier.align(Alignment.BottomCenter)
        )

        // Anti-Loss Emergency Overlay
        PingAlarmOverlay(
            isAlarmActive = isAlarmActive,
            onStopAlarm = { viewModel.stopAlarm() }
        )
    }
}

/**
 * Geometric vector reconstruction of the Sun (in Dark mode) and Crescent Moon
 * (in Light mode) matching CarCluster_Connectivity.ino drawThemeIconCb.
 */
@Composable
private fun ThemeIconVector(isDark: Boolean, color: Color) {
    Canvas(modifier = Modifier.size(18.dp)) {
        val cx = size.width / 2
        val cy = size.height / 2

        if (isDark) {
            // Sun: Central Circle + 8 Radiating Rays
            drawCircle(color = color, radius = 4.dp.toPx(), center = Offset(cx, cy))
            for (i in 0 until 8) {
                val angle = Math.toRadians((i * 45).toDouble())
                val r1 = 6.dp.toPx()
                val r2 = 8.5.dp.toPx()
                drawLine(
                    color = color,
                    start = Offset((cx + r1 * cos(angle)).toFloat(), (cy + r1 * sin(angle)).toFloat()),
                    end = Offset((cx + r2 * cos(angle)).toFloat(), (cy + r2 * sin(angle)).toFloat()),
                    strokeWidth = 1.5.dp.toPx()
                )
            }
        } else {
            // Crescent Moon: Outer arc - Inner subtracted arc
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
