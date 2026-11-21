package com.visteon.lunet.ui.screens

import android.Manifest
import android.annotation.SuppressLint
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.os.PowerManager
import android.provider.Settings
import androidx.compose.animation.AnimatedVisibility
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
import androidx.compose.material.icons.filled.ChevronRight
import androidx.compose.material.icons.filled.Info
import androidx.compose.material.icons.filled.NotificationsActive
import androidx.compose.material.icons.filled.Terminal
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.Switch
import androidx.compose.material3.SwitchDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.core.content.ContextCompat
import com.visteon.lunet.ui.components.BleConsoleSheet
import com.visteon.lunet.ui.theme.LunetTheme
import com.visteon.lunet.ui.theme.VisteonOrange
import com.visteon.lunet.viewmodel.LunetViewModel

/**
 * Sync Screen: Permissions & Real OS Data Bridges Hub.
 * - Single-column iOS/HyperOS style toggle rows
 * - Compact permissions checklist with live green/amber indicators and deep links
 * - Collapsible Android 13-16 Sideloaded APK "Allow Restricted Settings" tip banner
 * - Test Ping & BLE Console modal sheet launcher
 */
@Composable
fun SyncScreen(viewModel: LunetViewModel) {
    val context = LocalContext.current
    val colors = LunetTheme.colors
    val haptic = LocalHapticFeedback.current

    val syncMusic by viewModel.syncMusic.collectAsState()
    val syncNotifs by viewModel.syncNotifs.collectAsState()
    val syncNav by viewModel.syncNav.collectAsState()
    val syncCalls by viewModel.syncCalls.collectAsState()
    val autoReconnect by viewModel.autoReconnect.collectAsState()
    val isNotifListenerActive by viewModel.isNotificationListenerActive.collectAsState()
    val showConsoleSheet by viewModel.showBleConsoleSheet.collectAsState()

    var showRestrictedTip by remember { mutableStateOf(false) }

    // Live permission states
    val hasBleScan = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
        ContextCompat.checkSelfPermission(context, Manifest.permission.BLUETOOTH_SCAN) == PackageManager.PERMISSION_GRANTED
    } else true

    val hasBleConnect = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
        ContextCompat.checkSelfPermission(context, Manifest.permission.BLUETOOTH_CONNECT) == PackageManager.PERMISSION_GRANTED
    } else true

    val hasPhoneState = ContextCompat.checkSelfPermission(context, Manifest.permission.READ_PHONE_STATE) == PackageManager.PERMISSION_GRANTED
    val hasAnswerCalls = ContextCompat.checkSelfPermission(context, Manifest.permission.ANSWER_PHONE_CALLS) == PackageManager.PERMISSION_GRANTED
    val hasContacts = ContextCompat.checkSelfPermission(context, Manifest.permission.READ_CONTACTS) == PackageManager.PERMISSION_GRANTED
    val hasPostNotif = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
        ContextCompat.checkSelfPermission(context, Manifest.permission.POST_NOTIFICATIONS) == PackageManager.PERMISSION_GRANTED
    } else true

    val powerManager = context.getSystemService(Context.POWER_SERVICE) as? PowerManager
    val isBatteryOptimized = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
        powerManager?.isIgnoringBatteryOptimizations(context.packageName) == true
    } else true

    LazyColumn(
        modifier = Modifier
            .fillMaxSize()
            .padding(horizontal = 16.dp),
        verticalArrangement = Arrangement.spacedBy(14.dp)
    ) {
        // Collapsible Android 13-16 Sideload Tip Banner
        item {
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .clip(RoundedCornerShape(14.dp))
                    .background(colors.panelAlt)
                    .border(1.dp, colors.border, RoundedCornerShape(14.dp))
                    .clickable { showRestrictedTip = !showRestrictedTip }
                    .padding(14.dp)
            ) {
                Column {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.SpaceBetween
                    ) {
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            Icon(Icons.Default.Info, contentDescription = null, tint = colors.cyan, modifier = Modifier.size(18.dp))
                            Spacer(modifier = Modifier.width(8.dp))
                            Text(
                                text = "Android 13–16 Sideload Permission Tip",
                                color = colors.text,
                                fontSize = 12.sp,
                                fontWeight = FontWeight.SemiBold
                            )
                        }
                        Text(
                            text = if (showRestrictedTip) "HIDE" else "VIEW",
                            color = colors.cyan,
                            fontSize = 11.sp,
                            fontWeight = FontWeight.Bold,
                            fontFamily = FontFamily.Monospace
                        )
                    }

                    AnimatedVisibility(visible = showRestrictedTip) {
                        Column(modifier = Modifier.padding(top = 10.dp)) {
                            Text(
                                text = "If Notification Access is greyed out with 'Restricted setting':\n" +
                                        "1. Open App Info in Android Settings\n" +
                                        "2. Tap the ⋮ (three dots menu) at top-right\n" +
                                        "3. Select 'Allow restricted settings'\n" +
                                        "4. Return here to enable Notification Listener.",
                                color = colors.textMuted,
                                fontSize = 12.sp,
                                lineHeight = 17.sp
                            )
                        }
                    }
                }
            }
        }

        // Section 1: Live Bridges Toggles
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
                        text = "LIVE CLUSTER BRIDGES",
                        color = VisteonOrange,
                        fontSize = 10.sp,
                        fontWeight = FontWeight.Bold,
                        fontFamily = FontFamily.Monospace,
                        letterSpacing = 0.8.sp
                    )

                    SettingsToggleRow(
                        title = "Music Sync",
                        subtitle = "Beam song, singer & live playback progress",
                        checked = syncMusic,
                        onCheckedChange = {
                            haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                            viewModel.syncMusic.value = it
                        }
                    )

                    SettingsToggleRow(
                        title = "Recent Notifications",
                        subtitle = "Stream phone alerts directly to cluster HUD",
                        checked = syncNotifs,
                        onCheckedChange = {
                            haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                            viewModel.syncNotifs.value = it
                        }
                    )

                    SettingsToggleRow(
                        title = "Google Maps Navigation",
                        subtitle = "Turn-by-turn arrow icons, distances & ETAs",
                        checked = syncNav,
                        onCheckedChange = {
                            haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                            viewModel.syncNav.value = it
                        }
                    )

                    SettingsToggleRow(
                        title = "Incoming Calls",
                        subtitle = "Caller ID display & remote accept/decline",
                        checked = syncCalls,
                        onCheckedChange = {
                            haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                            viewModel.syncCalls.value = it
                        }
                    )

                    SettingsToggleRow(
                        title = "Auto-Reconnect",
                        subtitle = "Exponential backoff reconnect when cluster boots",
                        checked = autoReconnect,
                        onCheckedChange = {
                            haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                            viewModel.autoReconnect.value = it
                        }
                    )
                }
            }
        }

        // Section 2: Permissions Checklist
        item {
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .clip(RoundedCornerShape(16.dp))
                    .background(colors.surface)
                    .border(1.dp, colors.border, RoundedCornerShape(16.dp))
                    .padding(16.dp)
            ) {
                Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
                    Text(
                        text = "ANDROID SYSTEM ACCESS CHECKLIST",
                        color = colors.textMuted,
                        fontSize = 10.sp,
                        fontWeight = FontWeight.Bold,
                        fontFamily = FontFamily.Monospace,
                        letterSpacing = 0.8.sp
                    )

                    PermissionCheckRow(
                        title = "Bluetooth Nearby Devices",
                        description = "Scan & connect to Visteon-Lunet ESP32",
                        isGranted = hasBleScan && hasBleConnect,
                        onClick = { openAppSettings(context) }
                    )

                    PermissionCheckRow(
                        title = "Notification Listener Service",
                        description = "Capture music tokens, maps turns & app alerts",
                        isGranted = isNotifListenerActive,
                        onClick = { openNotificationListenerSettings(context) }
                    )

                    PermissionCheckRow(
                        title = "Phone & Call Controls",
                        description = "Detect calls & execute accept / decline",
                        isGranted = hasPhoneState && hasAnswerCalls,
                        onClick = { openAppSettings(context) }
                    )

                    PermissionCheckRow(
                        title = "Contacts Lookup",
                        description = "Resolve phone numbers to contact names",
                        isGranted = hasContacts,
                        onClick = { openAppSettings(context) }
                    )

                    PermissionCheckRow(
                        title = "Post Notifications",
                        description = "Foreground persistent link service",
                        isGranted = hasPostNotif,
                        onClick = { openAppSettings(context) }
                    )

                    PermissionCheckRow(
                        title = "Unrestricted Battery",
                        description = "Prevent OS killing background connection",
                        isGranted = isBatteryOptimized,
                        onClick = { openBatteryOptimizationSettings(context) }
                    )
                }
            }
        }

        // Section 3: Diagnostic Buttons
        item {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(10.dp)
            ) {
                Button(
                    onClick = {
                        haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                        viewModel.triggerTestPing()
                    },
                    colors = ButtonDefaults.buttonColors(containerColor = VisteonOrange),
                    shape = RoundedCornerShape(12.dp),
                    modifier = Modifier.weight(1f).height(44.dp)
                ) {
                    Icon(Icons.Default.NotificationsActive, contentDescription = null, tint = colors.bg, modifier = Modifier.size(16.dp))
                    Spacer(modifier = Modifier.width(6.dp))
                    Text("TEST PING", color = colors.bg, fontWeight = FontWeight.Bold, fontFamily = FontFamily.Monospace, fontSize = 11.sp)
                }

                Button(
                    onClick = {
                        haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                        viewModel.showBleConsoleSheet.value = true
                    },
                    colors = ButtonDefaults.buttonColors(containerColor = colors.surface),
                    shape = RoundedCornerShape(12.dp),
                    modifier = Modifier.weight(1f).height(44.dp).border(1.dp, colors.border, RoundedCornerShape(12.dp))
                ) {
                    Icon(Icons.Default.Terminal, contentDescription = null, tint = colors.cyan, modifier = Modifier.size(16.dp))
                    Spacer(modifier = Modifier.width(6.dp))
                    Text("BLE CONSOLE", color = colors.text, fontWeight = FontWeight.Bold, fontFamily = FontFamily.Monospace, fontSize = 11.sp)
                }
            }
        }

        item {
            Spacer(modifier = Modifier.height(88.dp))
        }
    }

    if (showConsoleSheet) {
        BleConsoleSheet(
            viewModel = viewModel,
            onDismiss = { viewModel.showBleConsoleSheet.value = false }
        )
    }
}

@Composable
private fun SettingsToggleRow(
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
            Text(title, color = colors.text, fontSize = 13.sp, fontWeight = FontWeight.SemiBold)
            Text(subtitle, color = colors.textMuted, fontSize = 11.sp)
        }

        Switch(
            checked = checked,
            onCheckedChange = onCheckedChange,
            colors = SwitchDefaults.colors(
                checkedThumbColor = VisteonOrange,
                checkedTrackColor = VisteonOrange.copy(alpha = 0.28f),
                uncheckedThumbColor = colors.textMuted,
                uncheckedTrackColor = colors.panelAlt
            )
        )
    }
}

@Composable
private fun PermissionCheckRow(
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
        Row(
            verticalAlignment = Alignment.CenterVertically,
            modifier = Modifier.weight(1f).padding(end = 8.dp)
        ) {
            Box(
                modifier = Modifier
                    .size(8.dp)
                    .clip(CircleShape)
                    .background(if (isGranted) colors.green else colors.amber)
            )
            Spacer(modifier = Modifier.width(10.dp))
            Column {
                Text(title, color = colors.text, fontSize = 12.sp, fontWeight = FontWeight.SemiBold)
                Text(description, color = colors.textMuted, fontSize = 10.sp)
            }
        }

        Icon(Icons.Default.ChevronRight, contentDescription = null, tint = colors.textMuted, modifier = Modifier.size(16.dp))
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

@SuppressLint("BatteryLife")
private fun openBatteryOptimizationSettings(context: Context) {
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
        val intent = Intent(Settings.ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS).apply {
            data = Uri.parse("package:${context.packageName}")
        }
        try {
            context.startActivity(intent)
        } catch (e: Exception) {
            context.startActivity(Intent(Settings.ACTION_IGNORE_BATTERY_OPTIMIZATION_SETTINGS))
        }
    }
}
