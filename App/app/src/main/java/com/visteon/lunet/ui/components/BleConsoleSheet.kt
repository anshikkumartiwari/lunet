package com.visteon.lunet.ui.components

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.Send
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.DeleteSweep
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.OutlinedTextFieldDefaults
import androidx.compose.material3.Text
import androidx.compose.material3.rememberModalBottomSheetState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.LocalFocusManager
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.visteon.lunet.ble.BleLogEntry
import com.visteon.lunet.ble.PacketDirection
import com.visteon.lunet.ui.theme.LunetTheme
import com.visteon.lunet.ui.theme.VisteonOrange
import com.visteon.lunet.viewmodel.LunetViewModel

/**
 * Modal Bottom Sheet BLE Console keeping raw TX/RX debug logs out of the main UI
 * while providing full diagnostic power on demand.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun BleConsoleSheet(
    viewModel: LunetViewModel,
    onDismiss: () -> Unit
) {
    val colors = LunetTheme.colors
    val logs by viewModel.packetLogs.collectAsState()
    val sheetState = rememberModalBottomSheetState(skipPartiallyExpanded = true)
    var rawInput by remember { mutableStateOf("") }
    val focusManager = LocalFocusManager.current

    val quickCommands = listOf(
        "CMD:SYNC:REQ",
        "CMD:PHONE:PING",
        "SPEED:100",
        "LIMIT:80",
        "GEAR:D",
        "MODE:SPORT",
        "CMD:MEDIA:PLAY",
        "CMD:VOL:80"
    )

    ModalBottomSheet(
        onDismissRequest = onDismiss,
        sheetState = sheetState,
        containerColor = colors.surface,
        dragHandle = null
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .fillMaxHeight(0.85f)
                .padding(16.dp)
        ) {
            // Header
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.SpaceBetween
            ) {
                Column {
                    Text(
                        text = "BLE PACKET INSPECTOR",
                        color = VisteonOrange,
                        fontSize = 12.sp,
                        fontWeight = FontWeight.Bold,
                        fontFamily = FontFamily.Monospace,
                        letterSpacing = 1.sp
                    )
                    Text(
                        text = "${logs.size} packets captured • UTF-8 Safe",
                        color = colors.textMuted,
                        fontSize = 10.sp,
                        fontFamily = FontFamily.Monospace
                    )
                }

                Row {
                    IconButton(onClick = { viewModel.clearLogs() }) {
                        Icon(Icons.Default.DeleteSweep, contentDescription = "Clear", tint = colors.textMuted)
                    }
                    IconButton(onClick = onDismiss) {
                        Icon(Icons.Default.Close, contentDescription = "Close", tint = colors.text)
                    }
                }
            }

            Spacer(modifier = Modifier.height(10.dp))

            // Quick Chips
            LazyRow(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                items(quickCommands) { cmd ->
                    Box(
                        modifier = Modifier
                            .clip(RoundedCornerShape(8.dp))
                            .background(colors.panelAlt)
                            .border(1.dp, colors.border, RoundedCornerShape(8.dp))
                            .clickable { viewModel.sendRawCommand(cmd) }
                            .padding(horizontal = 8.dp, vertical = 5.dp)
                    ) {
                        Text(
                            text = cmd,
                            color = colors.cyan,
                            fontSize = 11.sp,
                            fontWeight = FontWeight.Bold,
                            fontFamily = FontFamily.Monospace
                        )
                    }
                }
            }

            Spacer(modifier = Modifier.height(12.dp))

            // Log output list
            Box(
                modifier = Modifier
                    .weight(1f)
                    .fillMaxWidth()
                    .clip(RoundedCornerShape(12.dp))
                    .background(colors.surfaceDeep)
                    .border(1.dp, colors.border, RoundedCornerShape(12.dp))
                    .padding(10.dp)
            ) {
                if (logs.isEmpty()) {
                    Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                        Text("No BLE packets exchanged yet.", color = colors.textMuted, fontSize = 12.sp, fontFamily = FontFamily.Monospace)
                    }
                } else {
                    LazyColumn(
                        modifier = Modifier.fillMaxSize(),
                        verticalArrangement = Arrangement.spacedBy(6.dp)
                    ) {
                        items(logs, key = { it.id }) { entry ->
                            ConsoleLogRow(entry = entry)
                        }
                    }
                }
            }

            Spacer(modifier = Modifier.height(12.dp))

            // Input Bar
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                OutlinedTextField(
                    value = rawInput,
                    onValueChange = { rawInput = it },
                    modifier = Modifier.weight(1f),
                    placeholder = {
                        Text("e.g. SPEED:85 or GEAR:D", color = colors.textMuted, fontSize = 12.sp, fontFamily = FontFamily.Monospace)
                    },
                    singleLine = true,
                    keyboardOptions = KeyboardOptions(imeAction = ImeAction.Send),
                    keyboardActions = KeyboardActions(onSend = {
                        if (rawInput.isNotBlank()) {
                            viewModel.sendRawCommand(rawInput)
                            rawInput = ""
                            focusManager.clearFocus()
                        }
                    }),
                    colors = OutlinedTextFieldDefaults.colors(
                        focusedBorderColor = VisteonOrange,
                        unfocusedBorderColor = colors.border,
                        focusedTextColor = colors.text,
                        unfocusedTextColor = colors.text
                    ),
                    shape = RoundedCornerShape(10.dp)
                )

                Button(
                    onClick = {
                        if (rawInput.isNotBlank()) {
                            viewModel.sendRawCommand(rawInput)
                            rawInput = ""
                            focusManager.clearFocus()
                        }
                    },
                    colors = ButtonDefaults.buttonColors(containerColor = VisteonOrange),
                    shape = RoundedCornerShape(10.dp),
                    modifier = Modifier.height(50.dp)
                ) {
                    Icon(Icons.AutoMirrored.Filled.Send, contentDescription = "Send", tint = colors.bg)
                }
            }
        }
    }
}

@Composable
private fun ConsoleLogRow(entry: BleLogEntry) {
    val colors = LunetTheme.colors
    val isRx = entry.direction == PacketDirection.RX
    val color = if (isRx) colors.cyan else VisteonOrange

    Row(modifier = Modifier.fillMaxWidth(), verticalAlignment = Alignment.Top) {
        Text(
            text = entry.formattedTime,
            color = colors.textMuted,
            fontSize = 10.sp,
            fontFamily = FontFamily.Monospace,
            modifier = Modifier.padding(end = 6.dp)
        )
        Text(
            text = if (isRx) "◄ RX" else "► TX",
            color = color,
            fontSize = 10.sp,
            fontWeight = FontWeight.Bold,
            fontFamily = FontFamily.Monospace,
            modifier = Modifier.padding(end = 8.dp)
        )
        Text(
            text = entry.payload,
            color = colors.text,
            fontSize = 11.sp,
            fontFamily = FontFamily.Monospace
        )
    }
}
