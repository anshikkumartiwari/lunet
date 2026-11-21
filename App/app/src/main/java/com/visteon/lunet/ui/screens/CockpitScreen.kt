package com.visteon.lunet.ui.screens

import androidx.compose.foundation.background
import androidx.compose.foundation.border
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
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Call
import androidx.compose.material.icons.filled.CallEnd
import androidx.compose.material.icons.filled.FastForward
import androidx.compose.material.icons.filled.FastRewind
import androidx.compose.material.icons.filled.Navigation
import androidx.compose.material.icons.filled.Pause
import androidx.compose.material.icons.filled.PlayArrow
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
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.visteon.lunet.ble.BleConnectionState
import com.visteon.lunet.ui.components.AudioWaveformVisualizer
import com.visteon.lunet.ui.components.BleRadarRing
import com.visteon.lunet.ui.theme.LunetTheme
import com.visteon.lunet.ui.theme.LunetTypography
import com.visteon.lunet.ui.theme.VisteonOrange
import com.visteon.lunet.viewmodel.LunetViewModel

/**
 * Cockpit Dashboard:
 * Central BLE Radar ring + Real-Time Mirroring Cards for Now Playing, Navigation,
 * Telemetry, Call Status, and Phone Health.
 */
@Composable
fun CockpitScreen(viewModel: LunetViewModel) {
    val colors = LunetTheme.colors
    val connectionState by viewModel.connectionState.collectAsState()
    val rssi by viewModel.rssi.collectAsState()
    val mtu by viewModel.mtu.collectAsState()
    val telemetry by viewModel.telemetry.collectAsState()
    val mediaInfo by viewModel.mediaInfo.collectAsState()
    val callState by viewModel.callState.collectAsState()
    val weather by viewModel.weather.collectAsState()

    LazyColumn(
        modifier = Modifier
            .fillMaxSize()
            .padding(horizontal = 16.dp),
        verticalArrangement = Arrangement.spacedBy(14.dp)
    ) {
        // Hero Radar Ring
        item {
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(vertical = 12.dp),
                contentAlignment = Alignment.Center
            ) {
                BleRadarRing(
                    connectionState = connectionState,
                    rssi = rssi,
                    mtu = mtu,
                    onToggleConnection = {
                        if (connectionState == BleConnectionState.CONNECTED ||
                            connectionState == BleConnectionState.CONNECTING ||
                            connectionState == BleConnectionState.SCANNING
                        ) {
                            viewModel.disconnect()
                        } else {
                            viewModel.connect()
                        }
                    }
                )
            }
        }

        // Active Call Card (if ringing or connected)
        if (callState.isRinging || callState.isActive) {
            item {
                ActiveCallCard(
                    callerName = callState.callerName.ifBlank { "Incoming Call" },
                    isRinging = callState.isRinging,
                    onAccept = { viewModel.sendRawCommand("CMD:CALL:ACCEPT") },
                    onDecline = { viewModel.sendRawCommand("CMD:CALL:DECLINE") }
                )
            }
        }

        // Now Playing Media Mirror Card
        item {
            MediaMirrorCard(
                title = mediaInfo.title,
                artist = mediaInfo.artist,
                isPlaying = mediaInfo.isPlaying,
                progress = mediaInfo.progressPercent,
                volume = mediaInfo.volumePercent,
                eqProfile = mediaInfo.activeEqProfile,
                onPlayPause = { if (mediaInfo.isPlaying) viewModel.pause() else viewModel.play() },
                onNext = { viewModel.next() },
                onPrev = { viewModel.prev() }
            )
        }

        // Turn-by-Turn Navigation Mirror Card
        item {
            NavigationMirrorCard(
                direction = telemetry.navDirection.name,
                distMeters = telemetry.navDistanceM,
                street = telemetry.navStreet,
                etaMin = telemetry.navEtaMin,
                remKm = telemetry.navRemKm
            )
        }

        // Cluster Telemetry Status Card
        item {
            TelemetryMirrorCard(
                speed = telemetry.speedKmh,
                limit = telemetry.speedLimitKmh,
                gear = telemetry.gear.code.toString(),
                mode = telemetry.mode.name,
                soc = telemetry.evSocPercent,
                phoneBatt = telemetry.phoneBatteryPercent,
                weather = weather
            )
        }

        // Bottom spacing for floating navigation pill
        item {
            Spacer(modifier = Modifier.height(90.dp))
        }
    }
}

@Composable
private fun ActiveCallCard(
    callerName: String,
    isRinging: Boolean,
    onAccept: () -> Unit,
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
                            .size(10.dp)
                            .clip(CircleShape)
                            .background(if (isRinging) colors.green else colors.cyan)
                    )
                    Spacer(modifier = Modifier.width(8.dp))
                    Text(
                        text = if (isRinging) "INCOMING CALL" else "CALL ACTIVE",
                        color = if (isRinging) colors.green else colors.cyan,
                        fontSize = 11.sp,
                        fontWeight = FontWeight.Bold,
                        fontFamily = FontFamily.Monospace,
                        letterSpacing = 1.sp
                    )
                }
                Text(
                    text = "ESP32 HUD",
                    color = colors.textMuted,
                    fontSize = 10.sp,
                    fontFamily = FontFamily.Monospace
                )
            }

            Spacer(modifier = Modifier.height(8.dp))

            Text(
                text = callerName,
                color = colors.text,
                fontSize = 18.sp,
                fontWeight = FontWeight.Bold,
                fontFamily = FontFamily.SansSerif
            )

            Text(
                text = "Voice audio remains on phone / vehicle audio system",
                color = colors.textMuted,
                fontSize = 11.sp,
                fontFamily = FontFamily.SansSerif
            )

            Spacer(modifier = Modifier.height(14.dp))

            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(12.dp)
            ) {
                if (isRinging) {
                    Button(
                        onClick = onAccept,
                        colors = ButtonDefaults.buttonColors(containerColor = colors.green),
                        modifier = Modifier.weight(1f).height(42.dp),
                        shape = RoundedCornerShape(10.dp)
                    ) {
                        Icon(Icons.Default.Call, contentDescription = null, tint = colors.bg)
                        Spacer(modifier = Modifier.width(6.dp))
                        Text("ANSWER", color = colors.bg, fontWeight = FontWeight.Bold, fontFamily = FontFamily.Monospace)
                    }
                }
                Button(
                    onClick = onDecline,
                    colors = ButtonDefaults.buttonColors(containerColor = colors.red),
                    modifier = Modifier.weight(1f).height(42.dp),
                    shape = RoundedCornerShape(10.dp)
                ) {
                    Icon(Icons.Default.CallEnd, contentDescription = null, tint = Color.White)
                    Spacer(modifier = Modifier.width(6.dp))
                    Text("DECLINE", color = Color.White, fontWeight = FontWeight.Bold, fontFamily = FontFamily.Monospace)
                }
            }
        }
    }
}

@Composable
private fun MediaMirrorCard(
    title: String,
    artist: String,
    isPlaying: Boolean,
    progress: Int,
    volume: Int,
    eqProfile: String,
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
                    text = "MEDIA BEAM",
                    color = VisteonOrange,
                    fontSize = 10.sp,
                    fontWeight = FontWeight.Bold,
                    fontFamily = FontFamily.Monospace,
                    letterSpacing = 1.sp
                )
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(
                        text = "EQ: $eqProfile",
                        color = colors.textMuted,
                        fontSize = 10.sp,
                        fontFamily = FontFamily.Monospace
                    )
                    Spacer(modifier = Modifier.width(10.dp))
                    AudioWaveformVisualizer(isPlaying = isPlaying, barCount = 10)
                }
            }

            Spacer(modifier = Modifier.height(10.dp))

            Text(
                text = title,
                color = colors.text,
                fontSize = 16.sp,
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

            // Progress Bar
            LinearProgressIndicator(
                progress = { progress / 100f },
                modifier = Modifier.fillMaxWidth().height(4.dp).clip(RoundedCornerShape(2.dp)),
                color = VisteonOrange,
                trackColor = colors.border
            )

            Spacer(modifier = Modifier.height(10.dp))

            // Media Transport Controls
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.SpaceBetween
            ) {
                Text(
                    text = "VOL: $volume%",
                    color = colors.textMuted,
                    fontSize = 11.sp,
                    fontFamily = FontFamily.Monospace
                )

                Row(verticalAlignment = Alignment.CenterVertically) {
                    IconButton(onClick = onPrev) {
                        Icon(Icons.Default.FastRewind, contentDescription = "Prev", tint = colors.text)
                    }
                    Box(
                        modifier = Modifier
                            .size(38.dp)
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
                    IconButton(onClick = onNext) {
                        Icon(Icons.Default.FastForward, contentDescription = "Next", tint = colors.text)
                    }
                }
            }
        }
    }
}

@Composable
private fun NavigationMirrorCard(
    direction: String,
    distMeters: Int,
    street: String,
    etaMin: Int,
    remKm: Float
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
                    text = "GOOGLE MAPS BRIDGE",
                    color = colors.cyan,
                    fontSize = 10.sp,
                    fontWeight = FontWeight.Bold,
                    fontFamily = FontFamily.Monospace,
                    letterSpacing = 1.sp
                )
                Text(
                    text = "$etaMin MIN  •  $remKm KM",
                    color = colors.textMuted,
                    fontSize = 11.sp,
                    fontFamily = FontFamily.Monospace,
                    fontWeight = FontWeight.SemiBold
                )
            }

            Spacer(modifier = Modifier.height(12.dp))

            Row(verticalAlignment = Alignment.CenterVertically) {
                Box(
                    modifier = Modifier
                        .size(44.dp)
                        .clip(RoundedCornerShape(12.dp))
                        .background(colors.panelAlt)
                        .border(1.dp, colors.border, RoundedCornerShape(12.dp)),
                    contentAlignment = Alignment.Center
                ) {
                    Icon(
                        Icons.Default.Navigation,
                        contentDescription = null,
                        tint = colors.cyan,
                        modifier = Modifier.size(24.dp)
                    )
                }

                Spacer(modifier = Modifier.width(14.dp))

                Column {
                    Text(
                        text = "In $distMeters m $direction",
                        color = colors.text,
                        fontSize = 15.sp,
                        fontWeight = FontWeight.Bold,
                        fontFamily = FontFamily.SansSerif
                    )
                    Text(
                        text = street,
                        color = colors.textMuted,
                        fontSize = 13.sp,
                        fontFamily = FontFamily.SansSerif,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis
                    )
                }
            }
        }
    }
}

@Composable
private fun TelemetryMirrorCard(
    speed: Float,
    limit: Int,
    gear: String,
    mode: String,
    soc: Float,
    phoneBatt: Int,
    weather: String
) {
    val colors = LunetTheme.colors
    val isOverspeed = speed > limit && limit > 0

    Box(
        modifier = Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(16.dp))
            .background(colors.surface)
            .border(
                1.dp,
                if (isOverspeed) colors.red else colors.border,
                RoundedCornerShape(16.dp)
            )
            .padding(16.dp)
    ) {
        Column {
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.SpaceBetween
            ) {
                Text(
                    text = "CLUSTER TELEMETRY",
                    color = if (isOverspeed) colors.red else colors.textMuted,
                    fontSize = 10.sp,
                    fontWeight = FontWeight.Bold,
                    fontFamily = FontFamily.Monospace,
                    letterSpacing = 1.sp
                )
                Text(
                    text = weather,
                    color = colors.text,
                    fontSize = 11.sp,
                    fontFamily = FontFamily.Monospace
                )
            }

            Spacer(modifier = Modifier.height(14.dp))

            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.SpaceBetween
            ) {
                // Speed Readout
                Column {
                    Row(verticalAlignment = Alignment.Bottom) {
                        Text(
                            text = String.format(java.util.Locale.US, "%.0f", speed),
                            color = if (isOverspeed) colors.red else colors.text,
                            style = LunetTypography.displayMedium.copy(
                                fontSize = 38.sp,
                                fontWeight = FontWeight.Bold,
                                fontFamily = FontFamily.SansSerif
                            )
                        )
                        Spacer(modifier = Modifier.width(4.dp))
                        Text(
                            text = "KM/H",
                            color = colors.textMuted,
                            fontSize = 11.sp,
                            fontFamily = FontFamily.Monospace,
                            modifier = Modifier.padding(bottom = 6.dp)
                        )
                    }
                    if (isOverspeed) {
                        Text(
                            text = "OVERSPEED FLASH ACTIVE",
                            color = colors.red,
                            fontSize = 9.sp,
                            fontWeight = FontWeight.Bold,
                            fontFamily = FontFamily.Monospace
                        )
                    }
                }

                // Speed Limit Badge
                Box(
                    modifier = Modifier
                        .size(42.dp)
                        .clip(CircleShape)
                        .background(colors.bg)
                        .border(3.dp, colors.red, CircleShape),
                    contentAlignment = Alignment.Center
                ) {
                    Text(
                        text = "$limit",
                        color = colors.text,
                        fontSize = 14.sp,
                        fontWeight = FontWeight.Bold,
                        fontFamily = FontFamily.Monospace
                    )
                }

                // Gear & Mode
                Column(horizontalAlignment = Alignment.End) {
                    Text(
                        text = "GEAR: $gear",
                        color = if (gear == "D") colors.green else colors.text,
                        fontSize = 13.sp,
                        fontWeight = FontWeight.Bold,
                        fontFamily = FontFamily.Monospace
                    )
                    Text(
                        text = "MODE: $mode",
                        color = when (mode) {
                            "SPORT" -> VisteonOrange
                            "ECO" -> colors.green
                            else -> colors.cyan
                        },
                        fontSize = 11.sp,
                        fontWeight = FontWeight.Bold,
                        fontFamily = FontFamily.Monospace
                    )
                }
            }

            Spacer(modifier = Modifier.height(14.dp))

            // EV Battery SoC and Phone Battery
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween
            ) {
                Text(
                    text = "EV SOC: ${soc.toInt()}%",
                    color = colors.green,
                    fontSize = 11.sp,
                    fontFamily = FontFamily.Monospace,
                    fontWeight = FontWeight.SemiBold
                )
                Text(
                    text = "PHONE BATT: $phoneBatt%",
                    color = colors.cyan,
                    fontSize = 11.sp,
                    fontFamily = FontFamily.Monospace,
                    fontWeight = FontWeight.SemiBold
                )
            }
        }
    }
}
