package com.visteon.lunet.ui.components

import androidx.compose.animation.animateColorAsState
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.visteon.lunet.ui.theme.LunetTheme
import com.visteon.lunet.ui.theme.VisteonOrange
import com.visteon.lunet.viewmodel.ScreenTab

/**
 * Floating frosted-style bottom navigation pill for automotive UI.
 */
@Composable
fun FloatingNavBar(
    selectedTab: ScreenTab,
    onTabSelected: (ScreenTab) -> Unit,
    modifier: Modifier = Modifier
) {
    val colors = LunetTheme.colors
    val haptic = LocalHapticFeedback.current

    Box(
        modifier = modifier
            .fillMaxWidth()
            .padding(horizontal = 18.dp, vertical = 12.dp)
            .shadow(elevation = 16.dp, shape = RoundedCornerShape(28.dp), spotColor = Color.Black)
            .clip(RoundedCornerShape(28.dp))
            .background(colors.surface.copy(alpha = 0.92f))
            .border(1.dp, colors.border, RoundedCornerShape(28.dp))
            .padding(4.dp)
    ) {
        Row(
            modifier = Modifier.fillMaxWidth().height(48.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.SpaceEvenly
        ) {
            ScreenTab.values().forEach { tab ->
                val isSelected = tab == selectedTab
                val itemBg by animateColorAsState(
                    targetValue = if (isSelected) VisteonOrange.copy(alpha = 0.16f) else Color.Transparent,
                    label = "tabBg"
                )
                val textColor by animateColorAsState(
                    targetValue = if (isSelected) VisteonOrange else colors.textMuted,
                    label = "tabText"
                )

                Box(
                    modifier = Modifier
                        .weight(1f)
                        .height(40.dp)
                        .clip(RoundedCornerShape(22.dp))
                        .background(itemBg)
                        .clickable(
                            interactionSource = remember { MutableInteractionSource() },
                            indication = null
                        ) {
                            haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                            onTabSelected(tab)
                        },
                    contentAlignment = Alignment.Center
                ) {
                    Text(
                        text = tab.title.uppercase(),
                        color = textColor,
                        fontSize = 11.sp,
                        fontWeight = if (isSelected) FontWeight.Bold else FontWeight.Medium,
                        fontFamily = FontFamily.Monospace,
                        letterSpacing = 0.6.sp
                    )
                }
            }
        }
    }
}
