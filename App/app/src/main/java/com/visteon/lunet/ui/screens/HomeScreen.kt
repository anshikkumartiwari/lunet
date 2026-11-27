package com.visteon.lunet.ui.screens

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
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
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Call
import androidx.compose.material.icons.filled.CallEnd
import androidx.compose.material.icons.filled.FastForward
import androidx.compose.material.icons.filled.FastRewind
import androidx.compose.material.icons.filled.Lock
import androidx.compose.material.icons.filled.LockOpen
import androidx.compose.material.icons.filled.Navigation
import androidx.compose.material.icons.filled.Pause
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.TurnLeft
import androidx.compose.material.icons.filled.TurnRight
import androidx.compose.material.icons.filled.VolumeOff
import androidx.compose.material.icons.filled.VolumeUp
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.rotate
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.visteon.lunet.ble.BleConnectionState
import com.visteon.lunet.ble.NavDirection
import com.visteon.lunet.ble.NotifMirrorItem
import com.visteon.lunet.bridge.CarKeyConnectionState
import com.visteon.lunet.ui.components.AudioWaveformVisualizer
import com.visteon.lunet.ui.components.BleRadarRing
import com.visteon.lunet.ui.theme.LunetTheme
import com.visteon.lunet.ui.theme.VisteonOrange
import com.visteon.lunet.viewmodel.LunetViewModel

/**
 * Home Screen: Apple CarPlay / Xiaomi HyperOS Minimalist Live Cluster Mirror.
 * - Clean Hero Connection Card with radar pulse + RSSI
 * - Real Now Playing Card with live waveform + transport controls
 * - Active Google Maps Navigation Card (or calm idle state)
 * - Recent 5 Phone Notifications Card mirrored to Lunet
 */
@Composable
fun HomeScreen(viewModel: LunetViewModel) {
    val colors = LunetTheme.colors
    val connectionState by viewModel.connectionState.collectAsState()
    val rssi by viewModel.rssi.collectAsState()
    val mtu by viewModel.mtu.collectAsState()
    val telemetry by viewModel.telemetry.collectAsState()
    val mediaInfo by viewModel.mediaInfo.collectAsState()
    val callState by viewModel.callState.collectAsState()
    val recentNotifs by viewModel.recentNotifications.collectAsState()
    val carKeyConnectionState by viewModel.carKeyConnectionState.collectAsState()
    val isCarLocked by viewModel.isCarLocked.collectAsState()
    val carKeyBattery by viewModel.carKeyBattery.collectAsState()
    val isCarKeyBuzzerActive by viewModel.isCarKeyBuzzerActive.collectAsState()
    val carKeyActionText by viewModel.carKeyActionText.collectAsState()
    val haptic = LocalHapticFeedback.current

    LazyColumn(
        modifier = Modifier
            .fillMaxSize()
            .padding(horizontal = 16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        // Hero Connection Card
        item {
            HeroConnectionCard(
                connectionState = connectionState,
                rssi = rssi,
                mtu = mtu,
                onConnectClick = {
                    haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                    if (connectionState == BleConnectionState.CONNECTED ||
                        connectionState == BleConnectionState.SCANNING ||
                        connectionState == BleConnectionState.CONNECTING
                    ) {
                        viewModel.disconnect()
                    } else {
                        viewModel.connect()
                    }
                },
                onResetClick = {
                    haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                    viewModel.forceResetBle()
                }
            )
        }

        // Minimal Car Key Card
        item {
            CarKeyCard(
                connectionState = carKeyConnectionState,
                isLocked = isCarLocked,
                batteryLevel = carKeyBattery,
                isBuzzerActive = isCarKeyBuzzerActive,
                actionText = carKeyActionText,
                onToggleLock = {
                    haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                    viewModel.toggleCarLock()
                },
                onToggleBuzzer = {
                    haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                    if (isCarKeyBuzzerActive) viewModel.stopCarKeyBuzzer() else viewModel.triggerCarKeyBuzzer()
                },
                onConnectKey = {
                    haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                    if (carKeyConnectionState == CarKeyConnectionState.CONNECTED ||
                        carKeyConnectionState == CarKeyConnectionState.SCANNING ||
                        carKeyConnectionState == CarKeyConnectionState.CONNECTING
                    ) {
                        viewModel.disconnectCarKey()
                    } else {
                        viewModel.connectCarKey()
                    }
                }
            )
        }

        // Active Call Card (conditional)
        if (callState.isRinging || callState.isActive) {
            item {
                ActiveCallCard(
                    callerName = callState.callerName.ifBlank { "Incoming Call" },
                    isRinging = callState.isRinging,
                    isSpeakerOn = callState.isSpeakerOn,
                    onAccept = {
                        haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                        viewModel.acceptCall()
                    },
                    onAcceptSpeaker = {
                        haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                        viewModel.acceptCallWithSpeaker()
                    },
                    onToggleSpeaker = {
                        haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                        viewModel.toggleSpeaker()
                    },
                    onDecline = {
                        haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                        viewModel.declineCall()
                    }
                )
            }
        }

        // 1. Now Playing Mirror Card
        item {
            NowPlayingMirrorCard(
                title = mediaInfo.title,
                artist = mediaInfo.artist,
                isPlaying = mediaInfo.isPlaying,
                progress = mediaInfo.progressPercent,
                volume = mediaInfo.volumePercent,
                onPlayPause = {
                    haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                    if (mediaInfo.isPlaying) viewModel.pause() else viewModel.play()
                },
                onNext = {
                    haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                    viewModel.next()
                },
                onPrev = {
                    haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                    viewModel.prev()
                }
            )
        }

        // 2. Active Google Maps Turn Card
        item {
            NavigationMirrorCard(
                direction = telemetry.navDirection,
                distMeters = telemetry.navDistanceM,
                street = telemetry.navStreet,
                etaMin = telemetry.navEtaMin,
                remKm = telemetry.navRemKm
            )
        }

        // 3. Recent 5 Mirrored Notifications Card
        item {
            RecentNotificationsCard(notifications = recentNotifs)
        }

        item {
            Spacer(modifier = Modifier.height(88.dp))
        }
    }
}

@Composable
private fun HeroConnectionCard(
    connectionState: BleConnectionState,
    rssi: Int,
    mtu: Int,
    onConnectClick: () -> Unit,
    onResetClick: () -> Unit
) {
    val colors = LunetTheme.colors
    val isConnected = connectionState == BleConnectionState.CONNECTED
    val isScanning = connectionState == BleConnectionState.SCANNING || connectionState == BleConnectionState.CONNECTING

    val statusDotColor = when {
        isConnected -> colors.green
        isScanning -> colors.cyan
        else -> colors.textMuted
    }

    val statusLabel = when {
        isConnected -> "CONNECTED TO CLUSTER"
        isScanning -> "SCANNING FOR VISTEON-LUNET"
        else -> "OFFLINE • ESP32 STANDBY"
    }

    val signalQuality = when {
        rssi >= -65 -> "EXCELLENT"
        rssi >= -80 -> "GOOD"
        rssi >= -90 -> "FAIR"
        else -> "WEAK"
    }

    Box(
        modifier = Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(20.dp))
            .background(colors.surface)
            .border(1.2.dp, if (isConnected) colors.green.copy(alpha = 0.4f) else colors.border, RoundedCornerShape(20.dp))
            .padding(20.dp)
    ) {
        Column(verticalArrangement = Arrangement.spacedBy(16.dp)) {
            // Header Row: Status Indicator & Protocol Pill
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.SpaceBetween
            ) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Box(
                        modifier = Modifier
                            .size(9.dp)
                            .clip(CircleShape)
                            .background(statusDotColor)
                    )
                    Spacer(modifier = Modifier.width(8.dp))
                    Text(
                        text = statusLabel,
                        color = statusDotColor,
                        fontSize = 11.sp,
                        fontWeight = FontWeight.Bold,
                        fontFamily = FontFamily.Monospace,
                        letterSpacing = 0.8.sp
                    )
                }

                Box(
                    modifier = Modifier
                        .clip(RoundedCornerShape(6.dp))
                        .background(colors.panelAlt)
                        .padding(horizontal = 8.dp, vertical = 3.dp)
                ) {
                    Text(
                        text = "BLE 5.0 GATT",
                        color = colors.textMuted,
                        fontSize = 10.sp,
                        fontWeight = FontWeight.SemiBold,
                        fontFamily = FontFamily.Monospace
                    )
                }
            }

            // Body: Large Radar Pulse + Device Details & Status Chips
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically
            ) {
                BleRadarRing(
                    connectionState = connectionState,
                    rssi = rssi,
                    mtu = mtu,
                    onToggleConnection = onConnectClick,
                    size = 78.dp
                )

                Spacer(modifier = Modifier.width(16.dp))

                Column(modifier = Modifier.weight(1f)) {
                    Text(
                        text = "Visteon Lunet OS 3.0",
                        color = colors.text,
                        fontSize = 17.sp,
                        fontWeight = FontWeight.Bold
                    )

                    Spacer(modifier = Modifier.height(2.dp))

                    Text(
                        text = if (isConnected) "Active Cluster Connection" else "ESP32-S3 Dual-Core HUD",
                        color = colors.textMuted,
                        fontSize = 12.sp
                    )

                    Spacer(modifier = Modifier.height(8.dp))

                    Row(
                        horizontalArrangement = Arrangement.spacedBy(6.dp),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        if (isConnected) {
                            StatusMetricChip(label = "$rssi dBm", subLabel = signalQuality, tint = colors.green)
                            StatusMetricChip(label = "MTU $mtu", subLabel = "FAST", tint = colors.cyan)
                        } else {
                            StatusMetricChip(label = "Visteon-Lunet", subLabel = "TARGET", tint = colors.textMuted)
                            StatusMetricChip(label = "2.4 GHz", subLabel = "LE", tint = colors.cyan)
                        }
                    }
                }
            }

            // Actions: Dual-button ergonomic controls
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(10.dp)
            ) {
                Button(
                    onClick = onConnectClick,
                    colors = ButtonDefaults.buttonColors(
                        containerColor = if (isConnected || isScanning) colors.panelAlt else VisteonOrange
                    ),
                    shape = RoundedCornerShape(12.dp),
                    modifier = Modifier
                        .weight(1.4f)
                        .height(44.dp)
                        .border(1.dp, if (isConnected || isScanning) colors.border else Color.Transparent, RoundedCornerShape(12.dp))
                ) {
                    Text(
                        text = if (isConnected) "DISCONNECT" else if (isScanning) "STOP SCAN" else "CONNECT NOW",
                        color = if (isConnected || isScanning) colors.text else colors.bg,
                        fontSize = 12.sp,
                        fontWeight = FontWeight.Bold,
                        fontFamily = FontFamily.Monospace,
                        letterSpacing = 0.5.sp
                    )
                }

                Button(
                    onClick = onResetClick,
                    colors = ButtonDefaults.buttonColors(containerColor = colors.panelAlt),
                    shape = RoundedCornerShape(12.dp),
                    modifier = Modifier
                        .weight(1f)
                        .height(44.dp)
                        .border(1.dp, colors.border, RoundedCornerShape(12.dp))
                ) {
                    Text(
                        text = "RESET LINK",
                        color = colors.cyan,
                        fontSize = 11.sp,
                        fontWeight = FontWeight.Bold,
                        fontFamily = FontFamily.Monospace,
                        letterSpacing = 0.5.sp
                    )
                }
            }
        }
    }
}

@Composable
private fun StatusMetricChip(label: String, subLabel: String, tint: Color) {
    val colors = LunetTheme.colors
    Box(
        modifier = Modifier
            .clip(RoundedCornerShape(6.dp))
            .background(colors.panelAlt)
            .border(1.dp, colors.border, RoundedCornerShape(6.dp))
            .padding(horizontal = 6.dp, vertical = 2.dp)
    ) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(
                text = label,
                color = tint,
                fontSize = 10.sp,
                fontWeight = FontWeight.Bold,
                fontFamily = FontFamily.Monospace
            )
            Spacer(modifier = Modifier.width(4.dp))
            Text(
                text = subLabel,
                color = colors.textMuted,
                fontSize = 9.sp,
                fontFamily = FontFamily.Monospace
            )
        }
    }
}

@Composable
private fun CarKeyCard(
    connectionState: CarKeyConnectionState,
    isLocked: Boolean,
    batteryLevel: Int?,
    isBuzzerActive: Boolean,
    actionText: String,
    onToggleLock: () -> Unit,
    onToggleBuzzer: () -> Unit,
    onConnectKey: () -> Unit
) {
    val colors = LunetTheme.colors
    val isConnected = connectionState == CarKeyConnectionState.CONNECTED
    val isConnecting = connectionState == CarKeyConnectionState.SCANNING || connectionState == CarKeyConnectionState.CONNECTING

    val statusDotColor = when {
        isConnected -> colors.green
        isConnecting -> colors.cyan
        else -> colors.textMuted
    }

    val statusLabel = when {
        isConnected -> "Connected"
        isConnecting -> "Connecting..."
        else -> "Disconnected"
    }

    Box(
        modifier = Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(16.dp))
            .background(colors.surface)
            .border(
                1.dp,
                if (isBuzzerActive) colors.red else colors.border,
                RoundedCornerShape(16.dp)
            )
            .padding(14.dp)
    ) {
        Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
            // Header Row: Title "Car Key", minimal status dot, battery % & connect pill
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    Box(
                        modifier = Modifier
                            .size(7.dp)
                            .clip(CircleShape)
                            .background(statusDotColor)
                    )
                    Text(
                        text = "Car Key",
                        color = colors.text,
                        fontSize = 15.sp,
                        fontWeight = FontWeight.Bold,
                        letterSpacing = 0.3.sp
                    )
                    Text(
                        text = "•  $statusLabel",
                        color = statusDotColor,
                        fontSize = 11.sp,
                        fontWeight = FontWeight.SemiBold,
                        fontFamily = FontFamily.Monospace
                    )
                }

                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    if (batteryLevel != null && isConnected) {
                        Box(
                            modifier = Modifier
                                .clip(RoundedCornerShape(6.dp))
                                .background(colors.panelAlt)
                                .padding(horizontal = 6.dp, vertical = 2.dp)
                        ) {
                            Text(
                                text = "BATT $batteryLevel%",
                                color = colors.textMuted,
                                fontSize = 10.sp,
                                fontWeight = FontWeight.Bold,
                                fontFamily = FontFamily.Monospace
                            )
                        }
                    }

                    Box(
                        modifier = Modifier
                            .clip(RoundedCornerShape(8.dp))
                            .background(if (isConnected) colors.panelAlt else VisteonOrange.copy(alpha = 0.2f))
                            .clickable(onClick = onConnectKey)
                            .border(
                                1.dp,
                                if (isConnected) colors.border else VisteonOrange.copy(alpha = 0.5f),
                                RoundedCornerShape(8.dp)
                            )
                            .padding(horizontal = 10.dp, vertical = 4.dp)
                    ) {
                        Text(
                            text = if (isConnected) "DISCONNECT" else if (isConnecting) "SEARCHING" else "PAIR FOB",
                            color = if (isConnected) colors.textMuted else VisteonOrange,
                            fontSize = 10.sp,
                            fontWeight = FontWeight.Bold,
                            fontFamily = FontFamily.Monospace,
                            letterSpacing = 0.5.sp
                        )
                    }
                }
            }

            // Quick Action Buttons: Lock/Unlock + Buzzer
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(10.dp)
            ) {
                // Lock / Unlock Button
                Button(
                    onClick = onToggleLock,
                    colors = ButtonDefaults.buttonColors(
                        containerColor = colors.panelAlt
                    ),
                    shape = RoundedCornerShape(12.dp),
                    modifier = Modifier
                        .weight(1f)
                        .height(44.dp)
                        .border(
                            1.dp,
                            if (isLocked) colors.cyan.copy(alpha = 0.8f) else colors.amber.copy(alpha = 0.8f),
                            RoundedCornerShape(12.dp)
                        )
                ) {
                    Row(
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(6.dp)
                    ) {
                        Icon(
                            imageVector = if (isLocked) Icons.Default.Lock else Icons.Default.LockOpen,
                            contentDescription = null,
                            tint = if (isLocked) colors.cyan else colors.amber,
                            modifier = Modifier.size(16.dp)
                        )
                        Text(
                            text = if (isLocked) "LOCKED" else "UNLOCKED",
                            color = if (isLocked) colors.cyan else colors.amber,
                            fontSize = 12.sp,
                            fontWeight = FontWeight.Bold,
                            fontFamily = FontFamily.Monospace,
                            letterSpacing = 0.5.sp
                        )
                    }
                }

                // Buzzer Simulation Button
                Button(
                    onClick = onToggleBuzzer,
                    colors = ButtonDefaults.buttonColors(
                        containerColor = if (isBuzzerActive) colors.red else colors.panelAlt
                    ),
                    shape = RoundedCornerShape(12.dp),
                    modifier = Modifier
                        .weight(1f)
                        .height(44.dp)
                        .border(
                            1.dp,
                            if (isBuzzerActive) colors.red else colors.border,
                            RoundedCornerShape(12.dp)
                        )
                ) {
                    Row(
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(6.dp)
                    ) {
                        Icon(
                            imageVector = Icons.Default.VolumeUp,
                            contentDescription = null,
                            tint = if (isBuzzerActive) colors.bg else colors.text,
                            modifier = Modifier.size(16.dp)
                        )
                        Text(
                            text = if (isBuzzerActive) "RINGING..." else "BUZZER",
                            color = if (isBuzzerActive) colors.bg else colors.text,
                            fontSize = 12.sp,
                            fontWeight = FontWeight.Bold,
                            fontFamily = FontFamily.Monospace,
                            letterSpacing = 0.5.sp
                        )
                    }
                }
            }

            // Compact Hint / Live Action Text
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    text = "1-click: Lock/Unlock  •  2-click: Buzzer",
                    color = colors.textMuted,
                    fontSize = 10.sp,
                    fontFamily = FontFamily.Monospace
                )
                if (actionText.isNotBlank()) {
                    Text(
                        text = actionText,
                        color = if (isBuzzerActive) colors.red else colors.cyan,
                        fontSize = 10.sp,
                        fontWeight = FontWeight.SemiBold,
                        fontFamily = FontFamily.Monospace
                    )
                }
            }
        }
    }
}

@Composable
private fun NowPlayingMirrorCard(
    title: String,
    artist: String,
    isPlaying: Boolean,
    progress: Int,
    volume: Int,
    onPlayPause: () -> Unit,
    onNext: () -> Unit,
    onPrev: () -> Unit
) {
    val colors = LunetTheme.colors
    Box(
        modifier = Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(16.dp))
            .background(colors.surface)
            .border(1.dp, colors.border, RoundedCornerShape(16.dp))
            .padding(16.dp)
    ) {
        Column {
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.SpaceBetween
            ) {
                Text(
                    text = "NOW PLAYING",
                    color = VisteonOrange,
                    fontSize = 10.sp,
                    fontWeight = FontWeight.Bold,
                    fontFamily = FontFamily.Monospace,
                    letterSpacing = 0.8.sp
                )
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(
                        text = "VOL $volume%",
                        color = colors.textMuted,
                        fontSize = 10.sp,
                        fontFamily = FontFamily.Monospace
                    )
                    Spacer(modifier = Modifier.width(8.dp))
                    AudioWaveformVisualizer(isPlaying = isPlaying, barCount = 5)
                }
            }

            Spacer(modifier = Modifier.height(10.dp))

            Text(
                text = title,
                color = colors.text,
                fontSize = 17.sp,
                fontWeight = FontWeight.Bold,
                fontFamily = FontFamily.SansSerif,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis
            )

            Text(
                text = artist,
                color = colors.textMuted,
                fontSize = 13.sp,
                fontFamily = FontFamily.SansSerif,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis
            )

            Spacer(modifier = Modifier.height(12.dp))

            LinearProgressIndicator(
                progress = { progress / 100f },
                modifier = Modifier.fillMaxWidth().height(4.dp).clip(RoundedCornerShape(2.dp)),
                color = VisteonOrange,
                trackColor = colors.border
            )

            Spacer(modifier = Modifier.height(10.dp))

            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.Center
            ) {
                IconButton(onClick = onPrev) {
                    Icon(Icons.Default.FastRewind, contentDescription = "Prev", tint = colors.text)
                }
                Spacer(modifier = Modifier.width(16.dp))
                Box(
                    modifier = Modifier
                        .size(42.dp)
                        .clip(CircleShape)
                        .background(VisteonOrange),
                    contentAlignment = Alignment.Center
                ) {
                    IconButton(onClick = onPlayPause) {
                        Icon(
                            if (isPlaying) Icons.Default.Pause else Icons.Default.PlayArrow,
                            contentDescription = "Play/Pause",
                            tint = colors.bg
                        )
                    }
                }
                Spacer(modifier = Modifier.width(16.dp))
                IconButton(onClick = onNext) {
                    Icon(Icons.Default.FastForward, contentDescription = "Next", tint = colors.text)
                }
            }
        }
    }
}

@Composable
private fun NavigationMirrorCard(
    direction: NavDirection,
    distMeters: Int,
    street: String,
    etaMin: Int,
    remKm: Float
) {
    val colors = LunetTheme.colors
    val isIdle = distMeters == 0 || street.contains("Start route", ignoreCase = true)

    Box(
        modifier = Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(16.dp))
            .background(colors.surface)
            .border(1.dp, colors.border, RoundedCornerShape(16.dp))
            .padding(16.dp)
    ) {
        Column {
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.SpaceBetween
            ) {
                Text(
                    text = "GOOGLE MAPS NAVIGATION",
                    color = colors.cyan,
                    fontSize = 10.sp,
                    fontWeight = FontWeight.Bold,
                    fontFamily = FontFamily.Monospace,
                    letterSpacing = 0.8.sp
                )
                if (!isIdle) {
                    Text(
                        text = "$etaMin MIN  •  ${String.format(java.util.Locale.US, "%.1f", remKm)} KM",
                        color = colors.cyan,
                        fontSize = 10.sp,
                        fontFamily = FontFamily.Monospace,
                        fontWeight = FontWeight.Bold
                    )
                }
            }

            Spacer(modifier = Modifier.height(10.dp))

            if (isIdle) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Box(
                        modifier = Modifier
                            .size(36.dp)
                            .clip(RoundedCornerShape(10.dp))
                            .background(colors.panelAlt),
                        contentAlignment = Alignment.Center
                    ) {
                        Icon(Icons.Default.Navigation, contentDescription = null, tint = colors.textMuted, modifier = Modifier.size(20.dp))
                    }
                    Spacer(modifier = Modifier.width(12.dp))
                    Column {
                        Text(
                            text = "Start a route in Google Maps",
                            color = colors.text,
                            fontSize = 14.sp,
                            fontWeight = FontWeight.Medium
                        )
                        Text(
                            text = "Live turns will mirror instantly to your car cluster HUD",
                            color = colors.textMuted,
                            fontSize = 11.sp
                        )
                    }
                }
            } else {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Box(
                        modifier = Modifier
                            .size(44.dp)
                            .clip(RoundedCornerShape(12.dp))
                            .background(colors.panelAlt)
                            .border(1.dp, colors.border, RoundedCornerShape(12.dp)),
                        contentAlignment = Alignment.Center
                    ) {
                        val isUturn = direction == NavDirection.UTURN
                        val icon = when (direction) {
                            NavDirection.LEFT -> Icons.Default.TurnLeft
                            NavDirection.RIGHT -> Icons.Default.TurnRight
                            NavDirection.UTURN -> Icons.Default.TurnLeft
                            NavDirection.STRAIGHT -> Icons.Default.Navigation
                        }
                        Icon(
                            icon,
                            contentDescription = null,
                            tint = colors.cyan,
                            modifier = Modifier.size(24.dp).let { if (isUturn) it.rotate(180f) else it }
                        )
                    }

                    Spacer(modifier = Modifier.width(14.dp))

                    Column {
                        Text(
                            text = "In $distMeters m ${direction.name.lowercase().replaceFirstChar { it.uppercase() }}",
                            color = colors.text,
                            fontSize = 15.sp,
                            fontWeight = FontWeight.Bold,
                            fontFamily = FontFamily.SansSerif
                        )
                        Text(
                            text = street,
                            color = colors.textMuted,
                            fontSize = 12.sp,
                            maxLines = 1,
                            overflow = TextOverflow.Ellipsis
                        )
                    }
                }
            }
        }
    }
}

@Composable
private fun RecentNotificationsCard(notifications: List<NotifMirrorItem>) {
    val colors = LunetTheme.colors
    Box(
        modifier = Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(16.dp))
            .background(colors.surface)
            .border(1.dp, colors.border, RoundedCornerShape(16.dp))
            .padding(16.dp)
    ) {
        Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.SpaceBetween
            ) {
                Text(
                    text = "RECENT MIRRORED NOTIFICATIONS",
                    color = colors.textMuted,
                    fontSize = 10.sp,
                    fontWeight = FontWeight.Bold,
                    fontFamily = FontFamily.Monospace,
                    letterSpacing = 0.8.sp
                )
                Text(
                    text = "${notifications.size}/5",
                    color = colors.textMuted,
                    fontSize = 10.sp,
                    fontFamily = FontFamily.Monospace
                )
            }

            if (notifications.isEmpty()) {
                Text(
                    text = "No notifications captured yet.\nIncoming phone alerts pop up on your ESP32 display.",
                    color = colors.textMuted,
                    fontSize = 12.sp,
                    lineHeight = 16.sp
                )
            } else {
                notifications.take(5).forEach { item ->
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.SpaceBetween
                    ) {
                        Row(
                            verticalAlignment = Alignment.CenterVertically,
                            modifier = Modifier.weight(1f).padding(end = 8.dp)
                        ) {
                            Box(
                                modifier = Modifier
                                    .clip(RoundedCornerShape(6.dp))
                                    .background(colors.panelAlt)
                                    .border(1.dp, colors.border, RoundedCornerShape(6.dp))
                                    .padding(horizontal = 6.dp, vertical = 2.dp)
                            ) {
                                Text(
                                    text = item.appName,
                                    color = colors.amber,
                                    fontSize = 10.sp,
                                    fontWeight = FontWeight.Bold,
                                    fontFamily = FontFamily.Monospace
                                )
                            }
                            Spacer(modifier = Modifier.width(8.dp))
                            Text(
                                text = item.message,
                                color = colors.text,
                                fontSize = 12.sp,
                                maxLines = 1,
                                overflow = TextOverflow.Ellipsis
                            )
                        }

                        Text(
                            text = item.timeFormatted,
                            color = colors.textMuted,
                            fontSize = 10.sp,
                            fontFamily = FontFamily.Monospace
                        )
                    }
                }
            }
        }
    }
}

@Composable
private fun ActiveCallCard(
    callerName: String,
    isRinging: Boolean,
    isSpeakerOn: Boolean,
    onAccept: () -> Unit,
    onAcceptSpeaker: () -> Unit,
    onToggleSpeaker: () -> Unit,
    onDecline: () -> Unit
) {
    val colors = LunetTheme.colors
    Box(
        modifier = Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(16.dp))
            .background(colors.panelAlt)
            .border(1.dp, if (isRinging) colors.green else colors.cyan, RoundedCornerShape(16.dp))
            .padding(16.dp)
    ) {
        Column {
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.SpaceBetween
            ) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Box(
                        modifier = Modifier
                            .size(8.dp)
                            .clip(CircleShape)
                            .background(if (isRinging) colors.green else colors.cyan)
                    )
                    Spacer(modifier = Modifier.width(8.dp))
                    Text(
                        text = if (isRinging) "INCOMING CALL" else "CALL IN PROGRESS",
                        color = if (isRinging) colors.green else colors.cyan,
                        fontSize = 11.sp,
                        fontWeight = FontWeight.Bold,
                        fontFamily = FontFamily.Monospace
                    )
                }
                Text(
                    text = if (isSpeakerOn) "LOUDSPEAKER ON" else "EARPIECE",
                    color = if (isSpeakerOn) VisteonOrange else colors.textMuted,
                    fontSize = 9.sp,
                    fontWeight = FontWeight.Bold,
                    fontFamily = FontFamily.Monospace
                )
            }

            Spacer(modifier = Modifier.height(6.dp))

            Text(
                text = callerName,
                color = colors.text,
                fontSize = 17.sp,
                fontWeight = FontWeight.Bold
            )

            Spacer(modifier = Modifier.height(12.dp))

            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                if (isRinging) {
                    Button(
                        onClick = onAccept,
                        colors = ButtonDefaults.buttonColors(containerColor = colors.green),
                        shape = RoundedCornerShape(10.dp),
                        modifier = Modifier.weight(1f).height(40.dp)
                    ) {
                        Icon(Icons.Default.Call, contentDescription = null, tint = colors.bg, modifier = Modifier.size(15.dp))
                        Spacer(modifier = Modifier.width(4.dp))
                        Text("ANSWER", color = colors.bg, fontWeight = FontWeight.Bold, fontFamily = FontFamily.Monospace, fontSize = 10.sp)
                    }

                    Button(
                        onClick = onAcceptSpeaker,
                        colors = ButtonDefaults.buttonColors(containerColor = colors.cyan),
                        shape = RoundedCornerShape(10.dp),
                        modifier = Modifier.weight(1.2f).height(40.dp)
                    ) {
                        Icon(Icons.Default.VolumeUp, contentDescription = null, tint = colors.bg, modifier = Modifier.size(15.dp))
                        Spacer(modifier = Modifier.width(4.dp))
                        Text("HANDS-FREE", color = colors.bg, fontWeight = FontWeight.Bold, fontFamily = FontFamily.Monospace, fontSize = 10.sp)
                    }
                } else {
                    Button(
                        onClick = onToggleSpeaker,
                        colors = ButtonDefaults.buttonColors(containerColor = if (isSpeakerOn) VisteonOrange else colors.surface),
                        shape = RoundedCornerShape(10.dp),
                        modifier = Modifier.weight(1f).height(40.dp).border(1.dp, colors.border, RoundedCornerShape(10.dp))
                    ) {
                        Icon(
                            if (isSpeakerOn) Icons.Default.VolumeUp else Icons.Default.VolumeOff,
                            contentDescription = null,
                            tint = if (isSpeakerOn) colors.bg else colors.text,
                            modifier = Modifier.size(15.dp)
                        )
                        Spacer(modifier = Modifier.width(4.dp))
                        Text(
                            if (isSpeakerOn) "SPEAKER ON" else "SPEAKER OFF",
                            color = if (isSpeakerOn) colors.bg else colors.text,
                            fontWeight = FontWeight.Bold,
                            fontFamily = FontFamily.Monospace,
                            fontSize = 10.sp
                        )
                    }
                }

                Button(
                    onClick = onDecline,
                    colors = ButtonDefaults.buttonColors(containerColor = colors.red),
                    shape = RoundedCornerShape(10.dp),
                    modifier = Modifier.weight(1f).height(40.dp)
                ) {
                    Icon(Icons.Default.CallEnd, contentDescription = null, tint = Color.White, modifier = Modifier.size(15.dp))
                    Spacer(modifier = Modifier.width(4.dp))
                    Text(if (isRinging) "DECLINE" else "END", color = Color.White, fontWeight = FontWeight.Bold, fontFamily = FontFamily.Monospace, fontSize = 10.sp)
                }
            }
        }
    }
}
