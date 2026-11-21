package com.visteon.lunet.ui.components

import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.togetherWith
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.visteon.lunet.ble.BleConnectionState
import com.visteon.lunet.ble.ClusterTelemetry
import com.visteon.lunet.ui.theme.LunetTheme
import com.visteon.lunet.ui.theme.VisteonOrange

/**
 * Adaptive Dynamic Status Pill mirroring the Lunet OS cluster's top dynamic island.
 * Seamlessly transitions between Cluster Telemetry, Connection Status, and Alerts.
 */
@Composable
fun DynamicStatusPill(
    connectionState: BleConnectionState,
    telemetry: ClusterTelemetry,
    modifier: Modifier = Modifier
) {
    val colors = LunetTheme.colors

    val statusColor = when (connectionState) {
        BleConnectionState.CONNECTED -> colors.green
        BleConnectionState.CONNECTING, BleConnectionState.SCANNING -> colors.cyan
        BleConnectionState.DISCONNECTED -> colors.textMuted
    }

    val statusLabel = when (connectionState) {
        BleConnectionState.CONNECTED -> "CONNECTED"
        BleConnectionState.CONNECTING -> "LINKING..."
        BleConnectionState.SCANNING -> "SCANNING"
        BleConnectionState.DISCONNECTED -> "STANDBY"
    }

    Box(
        modifier = modifier
            .fillMaxWidth()
            .height(38.dp)
            .clip(RoundedCornerShape(19.dp))
            .background(colors.panelAlt)
            .border(1.dp, colors.border, RoundedCornerShape(19.dp))
            .padding(horizontal = 14.dp),
        contentAlignment = Alignment.Center
    ) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.SpaceBetween
        ) {
            // Left: Connection Status Badge
            Row(verticalAlignment = Alignment.CenterVertically) {
                Box(
                    modifier = Modifier
                        .size(8.dp)
                        .clip(CircleShape)
                        .background(statusColor)
                )
                Spacer(modifier = Modifier.width(6.dp))
                Text(
                    text = statusLabel,
                    color = statusColor,
                    fontSize = 11.sp,
                    fontWeight = FontWeight.Bold,
                    fontFamily = FontFamily.Monospace,
                    letterSpacing = 0.8.sp
                )
            }

            // Center: Notification / Telemetry Ticker
            AnimatedContent(
                targetState = telemetry.latestAlertText,
                transitionSpec = { fadeIn() togetherWith fadeOut() },
                label = "pillAlertAnim",
                modifier = Modifier.weight(1f).padding(horizontal = 10.dp)
            ) { alert ->
                if (alert.isNotBlank()) {
                    Text(
                        text = alert,
                        color = VisteonOrange,
                        fontSize = 11.sp,
                        fontWeight = FontWeight.SemiBold,
                        fontFamily = FontFamily.SansSerif,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis
                    )
                } else {
                    Row(
                        horizontalArrangement = Arrangement.Center,
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Text(
                            text = "LUNET 3.0",
                            color = colors.textMuted,
                            fontSize = 10.sp,
                            fontWeight = FontWeight.Medium,
                            fontFamily = FontFamily.Monospace,
                            letterSpacing = 1.sp
                        )
                    }
                }
            }

            // Right: Cluster Gear & Drive Mode Pills
            Row(verticalAlignment = Alignment.CenterVertically) {
                // Gear Pill
                Box(
                    modifier = Modifier
                        .clip(RoundedCornerShape(6.dp))
                        .background(colors.surfaceDeep)
                        .border(1.dp, colors.border, RoundedCornerShape(6.dp))
                        .padding(horizontal = 6.dp, vertical = 2.dp)
                ) {
                    Text(
                        text = telemetry.gear.code.toString(),
                        color = if (telemetry.gear.code == 'D') colors.green else colors.text,
                        fontSize = 11.sp,
                        fontWeight = FontWeight.Bold,
                        fontFamily = FontFamily.Monospace
                    )
                }
                Spacer(modifier = Modifier.width(6.dp))
                // Mode Pill
                Box(
                    modifier = Modifier
                        .clip(RoundedCornerShape(6.dp))
                        .background(
                            when (telemetry.mode.name) {
                                "SPORT" -> VisteonOrange.copy(alpha = 0.2f)
                                "ECO" -> colors.green.copy(alpha = 0.2f)
                                else -> colors.cyan.copy(alpha = 0.2f)
                            }
                        )
                        .padding(horizontal = 6.dp, vertical = 2.dp)
                ) {
                    Text(
                        text = telemetry.mode.name,
                        color = when (telemetry.mode.name) {
                            "SPORT" -> VisteonOrange
                            "ECO" -> colors.green
                            else -> colors.cyan
                        },
                        fontSize = 9.sp,
                        fontWeight = FontWeight.Bold,
                        fontFamily = FontFamily.Monospace
                    )
                }
            }
        }
    }
}
