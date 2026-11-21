package com.visteon.lunet.ui.components

import androidx.compose.animation.core.LinearEasing
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import com.visteon.lunet.ui.theme.LunetTheme
import com.visteon.lunet.ui.theme.VisteonOrange
import kotlin.math.abs
import kotlin.math.sin

/**
 * Animated automotive equalizer spectrum visualizer synced with live media playback state.
 */
@Composable
fun AudioWaveformVisualizer(
    isPlaying: Boolean,
    modifier: Modifier = Modifier,
    barCount: Int = 16,
    barWidth: Dp = 3.dp,
    maxHeight: Dp = 26.dp
) {
    val colors = LunetTheme.colors
    val transition = rememberInfiniteTransition(label = "audioWaveform")

    val phase by transition.animateFloat(
        initialValue = 0f,
        targetValue = (2 * Math.PI).toFloat(),
        animationSpec = infiniteRepeatable(
            animation = tween(1200, easing = LinearEasing),
            repeatMode = RepeatMode.Restart
        ),
        label = "phaseAnim"
    )

    Row(
        modifier = modifier.height(maxHeight),
        horizontalArrangement = Arrangement.spacedBy(2.5.dp),
        verticalAlignment = Alignment.Bottom
    ) {
        for (i in 0 until barCount) {
            val heightFraction = if (isPlaying) {
                // Wave formula combining harmonic sine components
                val raw = (sin(phase + i * 0.45f) * 0.5f + 0.5f) *
                          (sin(phase * 1.8f + i * 0.25f) * 0.35f + 0.65f)
                abs(raw).coerceIn(0.18f, 1.0f)
            } else {
                0.14f
            }

            Box(
                modifier = Modifier
                    .width(barWidth)
                    .fillMaxHeight(heightFraction)
                    .clip(RoundedCornerShape(2.dp))
                    .background(
                        if (isPlaying) {
                            if (i % 3 == 0) colors.cyan else VisteonOrange
                        } else {
                            colors.border
                        }
                    )
            )
        }
    }
}
