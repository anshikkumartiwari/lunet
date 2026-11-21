package com.visteon.lunet.ui.screens

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
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Slider
import androidx.compose.material3.SliderDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.visteon.lunet.ble.DriveMode
import com.visteon.lunet.ble.Gear
import com.visteon.lunet.ble.NavDirection
import com.visteon.lunet.ui.theme.LunetTheme
import com.visteon.lunet.ui.theme.VisteonOrange
import com.visteon.lunet.viewmodel.LunetViewModel

/**
 * Demo Remote Deck for Visteon presentations:
 * Tactile real-time sliders and buttons to directly command the ESP32 cluster's
 * Speedometer, Red Overspeed Flash, Gear, Drive Mode, and simulated alerts.
 */
@Composable
fun DemoRemoteScreen(viewModel: LunetViewModel) {
    val colors = LunetTheme.colors
    val telemetry by viewModel.telemetry.collectAsState()
    val haptic = LocalHapticFeedback.current

    val isOverspeed = telemetry.speedKmh > telemetry.speedLimitKmh && telemetry.speedLimitKmh > 0

    LazyColumn(
        modifier = Modifier
            .fillMaxSize()
            .padding(horizontal = 16.dp),
        verticalArrangement = Arrangement.spacedBy(16.dp)
    ) {
        item {
            Text(
                text = "VISTEON LIVE DEMO REMOTE",
                color = VisteonOrange,
                fontSize = 11.sp,
                fontWeight = FontWeight.Bold,
                fontFamily = FontFamily.Monospace,
                letterSpacing = 1.sp,
                modifier = Modifier.padding(top = 10.dp)
            )
        }

        // Section 1: Speed & Speed Limit (Overspeed Flash Trigger)
        item {
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
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Text(
                            text = "SPEEDOMETER (SPEED:<val>)",
                            color = colors.textMuted,
                            fontSize = 10.sp,
                            fontWeight = FontWeight.Bold,
                            fontFamily = FontFamily.Monospace
                        )
                        Text(
                            text = "${telemetry.speedKmh.toInt()} KM/H",
                            color = if (isOverspeed) colors.red else VisteonOrange,
                            fontSize = 18.sp,
                            fontWeight = FontWeight.Bold,
                            fontFamily = FontFamily.Monospace
                        )
                    }

                    Slider(
                        value = telemetry.speedKmh,
                        onValueChange = {
                            viewModel.setSpeed(it)
                        },
                        valueRange = 0f..220f,
                        steps = 21,
                        colors = SliderDefaults.colors(
                            thumbColor = if (isOverspeed) colors.red else VisteonOrange,
                            activeTrackColor = if (isOverspeed) colors.red else VisteonOrange,
                            inactiveTrackColor = colors.surfaceDeep
                        )
                    )

                    Spacer(modifier = Modifier.height(10.dp))

                    Text(
                        text = "SPEED LIMIT (LIMIT:<val>)",
                        color = colors.textMuted,
                        fontSize = 10.sp,
                        fontWeight = FontWeight.Bold,
                        fontFamily = FontFamily.Monospace
                    )

                    Spacer(modifier = Modifier.height(8.dp))

                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.spacedBy(8.dp)
                    ) {
                        listOf(40, 60, 80, 100, 120).forEach { limit ->
                            val isSelected = telemetry.speedLimitKmh == limit
                            Box(
                                modifier = Modifier
                                    .weight(1f)
                                    .height(38.dp)
                                    .clip(RoundedCornerShape(8.dp))
                                    .background(if (isSelected) colors.red else colors.surfaceDeep)
                                    .border(1.dp, if (isSelected) colors.red else colors.border, RoundedCornerShape(8.dp))
                                    .clickable {
                                        haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                                        viewModel.setSpeedLimit(limit)
                                    },
                                contentAlignment = Alignment.Center
                            ) {
                                Text(
                                    text = "$limit",
                                    color = if (isSelected) colors.bg else colors.text,
                                    fontSize = 13.sp,
                                    fontWeight = FontWeight.Bold,
                                    fontFamily = FontFamily.Monospace
                                )
                            }
                        }
                    }

                    if (isOverspeed) {
                        Spacer(modifier = Modifier.height(8.dp))
                        Text(
                            text = "⚠ OVERSPEED ACTIVE: Cluster flashing red warning border",
                            color = colors.red,
                            fontSize = 11.sp,
                            fontFamily = FontFamily.Monospace,
                            fontWeight = FontWeight.Bold
                        )
                    }
                }
            }
        }

        // Section 2: Gear & Drive Mode Selectors
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
                    // Gear Selector (P, R, N, D)
                    Column {
                        Text(
                            text = "TRANSMISSION GEAR (GEAR:<P|R|N|D>)",
                            color = colors.textMuted,
                            fontSize = 10.sp,
                            fontWeight = FontWeight.Bold,
                            fontFamily = FontFamily.Monospace
                        )
                        Spacer(modifier = Modifier.height(8.dp))
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.spacedBy(8.dp)
                        ) {
                            listOf(Gear.PARK, Gear.REVERSE, Gear.NEUTRAL, Gear.DRIVE).forEach { g ->
                                val isSelected = telemetry.gear == g
                                val bg = when {
                                    isSelected && g == Gear.DRIVE -> colors.green
                                    isSelected && g == Gear.REVERSE -> colors.red
                                    isSelected -> VisteonOrange
                                    else -> colors.surfaceDeep
                                }

                                Box(
                                    modifier = Modifier
                                        .weight(1f)
                                        .height(44.dp)
                                        .clip(RoundedCornerShape(10.dp))
                                        .background(bg)
                                        .border(1.dp, if (isSelected) bg else colors.border, RoundedCornerShape(10.dp))
                                        .clickable {
                                            haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                                            viewModel.setGear(g)
                                        },
                                    contentAlignment = Alignment.Center
                                ) {
                                    Text(
                                        text = g.code.toString(),
                                        color = if (isSelected) colors.bg else colors.text,
                                        fontSize = 16.sp,
                                        fontWeight = FontWeight.Bold,
                                        fontFamily = FontFamily.Monospace
                                    )
                                }
                            }
                        }
                    }

                    // Drive Mode Selector (ECO, COMFORT, SPORT)
                    Column {
                        Text(
                            text = "DRIVE DYNAMICS MODE (MODE:<ECO|COMFORT|SPORT>)",
                            color = colors.textMuted,
                            fontSize = 10.sp,
                            fontWeight = FontWeight.Bold,
                            fontFamily = FontFamily.Monospace
                        )
                        Spacer(modifier = Modifier.height(8.dp))
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.spacedBy(8.dp)
                        ) {
                            listOf(DriveMode.ECO, DriveMode.COMFORT, DriveMode.SPORT).forEach { m ->
                                val isSelected = telemetry.mode == m
                                val bg = when {
                                    isSelected && m == DriveMode.SPORT -> VisteonOrange
                                    isSelected && m == DriveMode.ECO -> colors.green
                                    isSelected -> colors.cyan
                                    else -> colors.surfaceDeep
                                }

                                Box(
                                    modifier = Modifier
                                        .weight(1f)
                                        .height(42.dp)
                                        .clip(RoundedCornerShape(10.dp))
                                        .background(bg)
                                        .border(1.dp, if (isSelected) bg else colors.border, RoundedCornerShape(10.dp))
                                        .clickable {
                                            haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                                            viewModel.setDriveMode(m)
                                        },
                                    contentAlignment = Alignment.Center
                                ) {
                                    Text(
                                        text = m.label,
                                        color = if (isSelected) colors.bg else colors.text,
                                        fontSize = 12.sp,
                                        fontWeight = FontWeight.Bold,
                                        fontFamily = FontFamily.Monospace
                                    )
                                }
                            }
                        }
                    }

                    // EV Battery SoC Slider
                    Column {
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.SpaceBetween
                        ) {
                            Text(
                                text = "EV BATTERY STATE OF CHARGE (SOC:<val>)",
                                color = colors.textMuted,
                                fontSize = 10.sp,
                                fontWeight = FontWeight.Bold,
                                fontFamily = FontFamily.Monospace
                            )
                            Text(
                                text = "${telemetry.evSocPercent.toInt()}%",
                                color = colors.green,
                                fontSize = 12.sp,
                                fontWeight = FontWeight.Bold,
                                fontFamily = FontFamily.Monospace
                            )
                        }
                        Slider(
                            value = telemetry.evSocPercent,
                            onValueChange = { viewModel.setSoc(it) },
                            valueRange = 0f..100f,
                            colors = SliderDefaults.colors(
                                thumbColor = colors.green,
                                activeTrackColor = colors.green,
                                inactiveTrackColor = colors.surfaceDeep
                            )
                        )
                    }
                }
            }
        }

        // Section 3: One-Tap Live Scenario Triggers
        item {
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .clip(RoundedCornerShape(16.dp))
                    .background(colors.panelAlt)
                    .border(1.dp, colors.border, RoundedCornerShape(16.dp))
                    .padding(16.dp)
            ) {
                Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
                    Text(
                        text = "ONE-TAP PRESENTATION SCENARIOS",
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
                            onClick = {
                                haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                                viewModel.simulateIncomingCall("Aditya Sharma")
                            },
                            colors = ButtonDefaults.buttonColors(containerColor = colors.surfaceDeep),
                            modifier = Modifier.weight(1f).height(42.dp),
                            shape = RoundedCornerShape(8.dp)
                        ) {
                            Text("SIMULATE CALL", fontSize = 10.sp, fontWeight = FontWeight.Bold, fontFamily = FontFamily.Monospace)
                        }

                        Button(
                            onClick = {
                                haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                                viewModel.simulateEndCall()
                            },
                            colors = ButtonDefaults.buttonColors(containerColor = colors.surfaceDeep),
                            modifier = Modifier.weight(1f).height(42.dp),
                            shape = RoundedCornerShape(8.dp)
                        ) {
                            Text("END CALL", fontSize = 10.sp, fontWeight = FontWeight.Bold, fontFamily = FontFamily.Monospace)
                        }
                    }

                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.spacedBy(10.dp)
                    ) {
                        Button(
                            onClick = {
                                haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                                viewModel.sendSampleNotification("WhatsApp", "Aditya: Cluster UI demo confirmed for 5 PM!")
                            },
                            colors = ButtonDefaults.buttonColors(containerColor = colors.surfaceDeep),
                            modifier = Modifier.weight(1f).height(42.dp),
                            shape = RoundedCornerShape(8.dp)
                        ) {
                            Text("WHATSAPP NOTIF", fontSize = 10.sp, fontWeight = FontWeight.Bold, fontFamily = FontFamily.Monospace)
                        }

                        Button(
                            onClick = {
                                haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                                viewModel.pushSampleNavTurn(NavDirection.RIGHT, 150, "Visteon Innovation Way", 8, 4.2f)
                            },
                            colors = ButtonDefaults.buttonColors(containerColor = colors.surfaceDeep),
                            modifier = Modifier.weight(1f).height(42.dp),
                            shape = RoundedCornerShape(8.dp)
                        ) {
                            Text("NAV TURN RIGHT", fontSize = 10.sp, fontWeight = FontWeight.Bold, fontFamily = FontFamily.Monospace)
                        }
                    }

                    Button(
                        onClick = {
                            haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                            viewModel.pushSampleNavTurn(NavDirection.UTURN, 320, "Highway 44 Junction", 14, 9.8f)
                        },
                        colors = ButtonDefaults.buttonColors(containerColor = colors.surfaceDeep),
                        modifier = Modifier.fillMaxWidth().height(42.dp),
                        shape = RoundedCornerShape(8.dp)
                    ) {
                        Text("NAV U-TURN DEMO", fontSize = 10.sp, fontWeight = FontWeight.Bold, fontFamily = FontFamily.Monospace)
                    }
                }
            }
        }

        item {
            Spacer(modifier = Modifier.height(90.dp))
        }
    }
}
