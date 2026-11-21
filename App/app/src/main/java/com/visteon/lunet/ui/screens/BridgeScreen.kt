package com.visteon.lunet.ui.screens

import android.Manifest
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.provider.Settings
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
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Check
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.NotificationsActive
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.Switch
import androidx.compose.material3.SwitchDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.core.content.ContextCompat
import com.visteon.lunet.ui.theme.LunetTheme
import com.visteon.lunet.ui.theme.VisteonOrange
import com.visteon.lunet.viewmodel.LunetViewModel

/**
 * Bridge Screen:
 * Interactive Permissions Checklist with deep links + Real-Device Sync Toggles
 * + Anti-Loss Phone Ping Test + Offline Weather Overrides.
 */
@Composable
fun BridgeScreen(viewModel: LunetViewModel) {
    val context = LocalContext.current
    val colors = LunetTheme.colors

    val syncMusic by viewModel.syncMusic.collectAsState()
    val syncNotifs by viewModel.syncNotifs.collectAsState()
    val syncNav by viewModel.syncNav.collectAsState()
    val syncCalls by viewModel.syncCalls.collectAsState()
    val syncTimeBattWeather by viewModel.syncTimeBattWeather.collectAsState()
    val isNotifListenerActive by viewModel.isNotificationListenerActive.collectAsState()

    // Permission checks
    val hasBleScan = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
        ContextCompat.checkSelfPermission(context, Manifest.permission.BLUETOOTH_SCAN) == PackageManager.PERMISSION_GRANTED
    } else true

    val hasBleConnect = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
        ContextCompat.checkSelfPermission(context, Manifest.permission.BLUETOOTH_CONNECT) == PackageManager.PERMISSION_GRANTED
    } else true

    val hasPhoneState = ContextCompat.checkSelfPermission(
        context,
        Manifest.permission.READ_PHONE_STATE
    ) == PackageManager.PERMISSION_GRANTED

    val hasAnswerCalls = ContextCompat.checkSelfPermission(
        context,
        Manifest.permission.ANSWER_PHONE_CALLS
    ) == PackageManager.PERMISSION_GRANTED

    val hasContacts = ContextCompat.checkSelfPermission(
        context,
        Manifest.permission.READ_CONTACTS
    ) == PackageManager.PERMISSION_GRANTED

    LazyColumn(
        modifier = Modifier
            .fillMaxSize()
            .padding(horizontal = 16.dp),
        verticalArrangement = Arrangement.spacedBy(16.dp)
    ) {
        item {
            Text(
                text = "PERMISSIONS & BRIDGE HUB",
                color = VisteonOrange,
                fontSize = 11.sp,
                fontWeight = FontWeight.Bold,
                fontFamily = FontFamily.Monospace,
                letterSpacing = 1.sp,
                modifier = Modifier.padding(top = 10.dp)
            )
        }

        // Section 1: Android OS Permissions Checklist
        item {
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .clip(RoundedCornerShape(16.dp))
                    .background(colors.surface)
                    .border(1.dp, colors.border, RoundedCornerShape(16.dp))
                    .padding(16.dp)
            ) {
                Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
                    Text(
                        text = "ANDROID SYSTEM ACCESS",
                        color = colors.textMuted,
                        fontSize = 10.sp,
                        fontWeight = FontWeight.Bold,
                        fontFamily = FontFamily.Monospace
                    )

                    PermissionRow(
                        title = "Bluetooth Nearby Devices",
                        description = "Scan & connect to Visteon-Lunet ESP32",
                        isGranted = hasBleScan && hasBleConnect,
                        onClick = { openAppSettings(context) }
                    )

                    PermissionRow(
                        title = "Notification Listener Service",
                        description = "Capture WhatsApp & Google Maps turn-by-turn",
                        isGranted = isNotifListenerActive,
                        onClick = { openNotificationListenerSettings(context) }
                    )

                    PermissionRow(
                        title = "Phone State & Answer Calls",
                        description = "Display HUD caller & trigger accept/decline",
                        isGranted = hasPhoneState && hasAnswerCalls,
                        onClick = { openAppSettings(context) }
                    )

                    PermissionRow(
                        title = "Contacts Name Lookup",
                        description = "Resolve incoming numbers to contact names",
                        isGranted = hasContacts,
                        onClick = { openAppSettings(context) }
                    )
                }
            }
        }

        // Section 2: Real Sync Feature Toggles
        item {
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .clip(RoundedCornerShape(16.dp))
                    .background(colors.surface)
                    .border(1.dp, colors.border, RoundedCornerShape(16.dp))
                    .padding(16.dp)
            ) {
                Column(verticalArrangement = Arrangement.spacedBy(14.dp)) {
                    Text(
                        text = "LIVE CLUSTER BRIDGES",
                        color = colors.textMuted,
                        fontSize = 10.sp,
                        fontWeight = FontWeight.Bold,
                        fontFamily = FontFamily.Monospace
                    )

                    SyncToggleRow(
                        title = "Music Stream & Two-Way Control",
                        subtitle = "Spotify, YT Music -> MEDIA:<title>|<artist>",
                        checked = syncMusic,
                        onCheckedChange = { viewModel.syncMusic.value = it }
                    )

                    SyncToggleRow(
                        title = "Notification Forwarder",
                        subtitle = "User apps -> NOTIF:<App>|<Text>",
                        checked = syncNotifs,
                        onCheckedChange = { viewModel.syncNotifs.value = it }
                    )

                    SyncToggleRow(
                        title = "Google Maps Navigation",
                        subtitle = "Extract turns -> NAV:<DIR>|<m>|<street>",
                        checked = syncNav,
                        onCheckedChange = { viewModel.syncNav.value = it }
                    )

                    SyncToggleRow(
                        title = "Telephony Call HUD",
                        subtitle = "Incoming alerts & accept/decline over BLE",
                        checked = syncCalls,
                        onCheckedChange = { viewModel.syncCalls.value = it }
                    )

                    SyncToggleRow(
                        title = "Time, Battery & Weather Sync",
                        subtitle = "TIME: | DATE: | PBATT: | WEATHER: periodic push",
                        checked = syncTimeBattWeather,
                        onCheckedChange = { viewModel.syncTimeBattWeather.value = it }
                    )
                }
            }
        }

        // Section 3: Anti-Loss Ping Test & Weather Sync Actions
        item {
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .clip(RoundedCornerShape(16.dp))
                    .background(colors.panelAlt)
                    .border(1.dp, colors.border, RoundedCornerShape(16.dp))
                    .padding(16.dp)
            ) {
                Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
                    Text(
                        text = "UTILITIES & MANUAL SYNC",
                        color = colors.textMuted,
                        fontSize = 10.sp,
                        fontWeight = FontWeight.Bold,
                        fontFamily = FontFamily.Monospace
                    )

                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.spacedBy(10.dp)
                    ) {
                        Button(
                            onClick = { viewModel.triggerTestPing() },
                            colors = ButtonDefaults.buttonColors(containerColor = VisteonOrange),
                            modifier = Modifier.weight(1f).height(44.dp),
                            shape = RoundedCornerShape(10.dp)
                        ) {
                            Icon(Icons.Default.NotificationsActive, contentDescription = null, tint = colors.bg)
                            Spacer(modifier = Modifier.width(6.dp))
                            Text("TEST PING", color = colors.bg, fontWeight = FontWeight.Bold, fontFamily = FontFamily.Monospace, fontSize = 11.sp)
                        }

                        Button(
                            onClick = { viewModel.triggerFullSync() },
                            colors = ButtonDefaults.buttonColors(containerColor = colors.surfaceDeep),
                            modifier = Modifier.weight(1f).height(44.dp),
                            shape = RoundedCornerShape(10.dp)
                        ) {
                            Icon(Icons.Default.Refresh, contentDescription = null, tint = colors.text)
                            Spacer(modifier = Modifier.width(6.dp))
                            Text("SYNC ALL", color = colors.text, fontWeight = FontWeight.Bold, fontFamily = FontFamily.Monospace, fontSize = 11.sp)
                        }
                    }

                    // Offline Weather Override Chips
                    Text(
                        text = "Weather Preset Overrides:",
                        color = colors.textMuted,
                        fontSize = 11.sp
                    )

                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.spacedBy(8.dp)
                    ) {
                        listOf("26°C Clear", "19°C Rain", "31°C Haze").forEach { preset ->
                            Box(
                                modifier = Modifier
                                    .weight(1f)
                                    .clip(RoundedCornerShape(8.dp))
                                    .background(colors.surface)
                                    .border(1.dp, colors.border, RoundedCornerShape(8.dp))
                                    .clickable { viewModel.setManualWeather(preset) }
                                    .padding(vertical = 8.dp),
                                contentAlignment = Alignment.Center
                            ) {
                                Text(
                                    text = preset,
                                    color = colors.cyan,
                                    fontSize = 11.sp,
                                    fontWeight = FontWeight.SemiBold,
                                    fontFamily = FontFamily.Monospace
                                )
                            }
                        }
                    }
                }
            }
        }

        item {
            Spacer(modifier = Modifier.height(90.dp))
        }
    }
}

@Composable
private fun PermissionRow(
    title: String,
    description: String,
    isGranted: Boolean,
    onClick: () -> Unit
) {
    val colors = LunetTheme.colors
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(8.dp))
            .clickable(onClick = onClick)
            .padding(vertical = 4.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.SpaceBetween
    ) {
        Column(modifier = Modifier.weight(1f).padding(end = 12.dp)) {
            Text(
                text = title,
                color = colors.text,
                fontSize = 13.sp,
                fontWeight = FontWeight.SemiBold
            )
            Text(
                text = description,
                color = colors.textMuted,
                fontSize = 11.sp
            )
        }

        Box(
            modifier = Modifier
                .size(24.dp)
                .clip(CircleShape)
                .background(if (isGranted) colors.green else colors.amber),
            contentAlignment = Alignment.Center
        ) {
            Icon(
                if (isGranted) Icons.Default.Check else Icons.Default.Close,
                contentDescription = null,
                tint = colors.bg,
                modifier = Modifier.size(16.dp)
            )
        }
    }
}

@Composable
private fun SyncToggleRow(
    title: String,
    subtitle: String,
    checked: Boolean,
    onCheckedChange: (Boolean) -> Unit
) {
    val colors = LunetTheme.colors
    Row(
        modifier = Modifier.fillMaxWidth(),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.SpaceBetween
    ) {
        Column(modifier = Modifier.weight(1f).padding(end = 12.dp)) {
            Text(
                text = title,
                color = colors.text,
                fontSize = 13.sp,
                fontWeight = FontWeight.SemiBold
            )
            Text(
                text = subtitle,
                color = colors.textMuted,
                fontSize = 11.sp
            )
        }

        Switch(
            checked = checked,
            onCheckedChange = onCheckedChange,
            colors = SwitchDefaults.colors(
                checkedThumbColor = VisteonOrange,
                checkedTrackColor = VisteonOrange.copy(alpha = 0.3f),
                uncheckedThumbColor = colors.textMuted,
                uncheckedTrackColor = colors.surfaceDeep
            )
        )
    }
}

private fun openAppSettings(context: Context) {
    val intent = Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS).apply {
        data = Uri.fromParts("package", context.packageName, null)
    }
    context.startActivity(intent)
}

private fun openNotificationListenerSettings(context: Context) {
    val intent = Intent(Settings.ACTION_NOTIFICATION_LISTENER_SETTINGS)
    context.startActivity(intent)
}
