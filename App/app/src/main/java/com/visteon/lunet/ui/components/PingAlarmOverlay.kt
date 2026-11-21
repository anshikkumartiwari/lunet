package com.visteon.lunet.ui.components

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.FastOutSlowInEasing
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.scale
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.visteon.lunet.ui.theme.ObsidianBg
import com.visteon.lunet.ui.theme.VisteonOrange

/**
 * Pulsing full-screen anti-loss alarm overlay triggered by CMD:PHONE:PING.
 */
@Composable
fun PingAlarmOverlay(
    isAlarmActive: Boolean,
    onStopAlarm: () -> Unit
) {
    val transition = rememberInfiniteTransition(label = "alarmTransition")
    val pulseScale by transition.animateFloat(
        initialValue = 0.95f,
        targetValue = 1.08f,
        animationSpec = infiniteRepeatable(
            animation = tween(450, easing = FastOutSlowInEasing),
            repeatMode = RepeatMode.Reverse
        ),
        label = "alarmPulse"
    )

    AnimatedVisibility(
        visible = isAlarmActive,
        enter = fadeIn(),
        exit = fadeOut()
    ) {
        Box(
            modifier = Modifier
                .fillMaxSize()
                .background(ObsidianBg.copy(alpha = 0.95f))
                .padding(24.dp),
            contentAlignment = Alignment.Center
        ) {
            Column(
                horizontalAlignment = Alignment.CenterHorizontally,
                verticalArrangement = Arrangement.Center,
                modifier = Modifier.scale(pulseScale)
            ) {
                // Pulsing Icon Ring
                Box(
                    modifier = Modifier
                        .size(110.dp)
                        .clip(CircleShape)
                        .background(VisteonOrange.copy(alpha = 0.18f))
                        .border(3.dp, VisteonOrange, CircleShape),
                    contentAlignment = Alignment.Center
                ) {
                    VisteonLogo(size = 54.dp, color = VisteonOrange)
                }

                Spacer(modifier = Modifier.height(28.dp))

                Text(
                    text = "CLUSTER LOCATOR ALERT",
                    color = VisteonOrange,
                    fontSize = 20.sp,
                    fontWeight = FontWeight.Bold,
                    fontFamily = FontFamily.Monospace,
                    letterSpacing = 1.5.sp,
                    textAlign = TextAlign.Center
                )

                Spacer(modifier = Modifier.height(8.dp))

                Text(
                    text = "Ping command received from Visteon Lunet Car Cluster.\nAlarm audio and vibration active.",
                    color = Color.White.copy(alpha = 0.85f),
                    fontSize = 13.sp,
                    fontFamily = FontFamily.SansSerif,
                    textAlign = TextAlign.Center
                )

                Spacer(modifier = Modifier.height(36.dp))

                Button(
                    onClick = onStopAlarm,
                    colors = ButtonDefaults.buttonColors(containerColor = VisteonOrange),
                    shape = RoundedCornerShape(24.dp),
                    modifier = Modifier
                        .fillMaxWidth(0.8f)
                        .height(54.dp)
                ) {
                    Text(
                        text = "STOP RINGING",
                        color = ObsidianBg,
                        fontSize = 15.sp,
                        fontWeight = FontWeight.Bold,
                        fontFamily = FontFamily.Monospace,
                        letterSpacing = 1.sp
                    )
                }
            }
        }
    }
}
