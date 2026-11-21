package com.visteon.lunet.ui.components

import androidx.compose.animation.core.LinearEasing
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.size
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.visteon.lunet.ble.BleConnectionState
import com.visteon.lunet.ui.theme.LunetTheme
import com.visteon.lunet.ui.theme.VisteonOrange
import kotlin.math.cos
import kotlin.math.sin

/**
 * Hyper-tactile automotive BLE Radar Ring.
 * Visualizes connection state, animated pulse rings, rotating radar sweep, and RSSI.
 */
@Composable
fun BleRadarRing(
    connectionState: BleConnectionState,
    rssi: Int,
    mtu: Int,
    onToggleConnection: () -> Unit,
    modifier: Modifier = Modifier,
    size: Dp = 190.dp
) {
    val colors = LunetTheme.colors
    val haptic = LocalHapticFeedback.current

    val transition = rememberInfiniteTransition(label = "radarInfinite")

    // Pulse animation for scanning / connecting
    val pulseAlpha by transition.animateFloat(
        initialValue = 0.8f,
        targetValue = 0.0f,
        animationSpec = infiniteRepeatable(
            animation = tween(2200, easing = LinearEasing),
            repeatMode = RepeatMode.Restart
        ),
        label = "radarPulseAlpha"
    )
    val pulseRadiusMult by transition.animateFloat(
        initialValue = 0.45f,
        targetValue = 0.95f,
        animationSpec = infiniteRepeatable(
            animation = tween(2200, easing = LinearEasing),
            repeatMode = RepeatMode.Restart
        ),
        label = "radarPulseRadius"
    )

    // Radar rotating sweep
    val rotationDegrees by transition.animateFloat(
        initialValue = 0f,
        targetValue = 360f,
        animationSpec = infiniteRepeatable(
            animation = tween(4000, easing = LinearEasing),
            repeatMode = RepeatMode.Restart
        ),
        label = "radarRotation"
    )

    val activeColor = when (connectionState) {
        BleConnectionState.CONNECTED -> colors.green
        BleConnectionState.CONNECTING, BleConnectionState.SCANNING -> colors.cyan
        BleConnectionState.DISCONNECTED -> VisteonOrange
    }

    Box(
        modifier = modifier
            .size(size)
            .clickable(
                interactionSource = remember { MutableInteractionSource() },
                indication = null
            ) {
                haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                onToggleConnection()
            },
        contentAlignment = Alignment.Center
    ) {
        Canvas(modifier = Modifier.size(size)) {
            val center = Offset(size.toPx() / 2, size.toPx() / 2)
            val maxRadius = size.toPx() / 2 - 8.dp.toPx()

            // Concentric background rings
            val ringRadii = listOf(0.35f, 0.60f, 0.85f, 1.0f)
            ringRadii.forEach { factor ->
                drawCircle(
                    color = colors.border.copy(alpha = 0.5f),
                    radius = maxRadius * factor,
                    center = center,
                    style = Stroke(width = 1.dp.toPx())
                )
            }

            // Radar sweep / pulse when scanning or connected
            if (connectionState == BleConnectionState.SCANNING || connectionState == BleConnectionState.CONNECTING) {
                // Expanding pulse circle
                drawCircle(
                    color = activeColor.copy(alpha = pulseAlpha),
                    radius = maxRadius * pulseRadiusMult,
                    center = center,
                    style = Stroke(width = 2.dp.toPx())
                )

                // Rotating radar beam line
                val rad = Math.toRadians(rotationDegrees.toDouble())
                val endX = center.x + (maxRadius * cos(rad)).toFloat()
                val endY = center.y + (maxRadius * sin(rad)).toFloat()

                drawLine(
                    brush = Brush.linearGradient(
                        colors = listOf(activeColor.copy(alpha = 0.8f), Color.Transparent),
                        start = center,
                        end = Offset(endX, endY)
                    ),
                    start = center,
                    end = Offset(endX, endY),
                    strokeWidth = 2.5f,
                    cap = StrokeCap.Round
                )
            } else if (connectionState == BleConnectionState.CONNECTED) {
                // Static high-precision glowing outer arc
                drawCircle(
                    color = colors.green.copy(alpha = 0.35f),
                    radius = maxRadius * 0.95f,
                    center = center,
                    style = Stroke(width = 3.dp.toPx())
                )
            }

            // Outer primary perimeter
            drawCircle(
                color = activeColor.copy(alpha = 0.9f),
                radius = maxRadius,
                center = center,
                style = Stroke(width = 1.5.dp.toPx())
            )
        }

        // Inner Center Node
        Column(
            horizontalAlignment = Alignment.CenterHorizontally
        ) {
            VisteonLogo(size = 36.dp, color = activeColor)
            Spacer(modifier = Modifier.height(6.dp))
            Text(
                text = when (connectionState) {
                    BleConnectionState.CONNECTED -> "CONNECTED"
                    BleConnectionState.CONNECTING -> "LINKING"
                    BleConnectionState.SCANNING -> "SCANNING"
                    BleConnectionState.DISCONNECTED -> "CONNECT"
                },
                color = activeColor,
                fontSize = 11.sp,
                fontWeight = FontWeight.Bold,
                fontFamily = FontFamily.Monospace,
                letterSpacing = 1.sp
            )
            if (connectionState == BleConnectionState.CONNECTED) {
                Text(
                    text = "$rssi dBm  •  MTU $mtu",
                    color = colors.textMuted,
                    fontSize = 9.sp,
                    fontFamily = FontFamily.Monospace
                )
            } else {
                Text(
                    text = "TAP TO SCAN",
                    color = colors.textMuted,
                    fontSize = 9.sp,
                    fontFamily = FontFamily.Monospace,
                    letterSpacing = 0.5.sp
                )
            }
        }
    }
}
