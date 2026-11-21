package com.visteon.lunet.ui.components

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.visteon.lunet.ui.theme.LunetTheme
import com.visteon.lunet.ui.theme.VisteonOrange

/**
 * Exact mathematical geometric vector reconstruction of the Visteon V mark
 * matching CarCluster_Connectivity.ino (left quadrilateral stroke + right wedge).
 */
@Composable
fun VisteonLogo(
    modifier: Modifier = Modifier,
    size: Dp = 28.dp,
    color: Color = VisteonOrange
) {
    Canvas(modifier = modifier.size(size)) {
        val w = size.toPx()
        val h = size.toPx()

        // Left quadrilateral stroke
        val leftPath = Path().apply {
            moveTo(0.000f * w, 0.000f * h)
            lineTo(0.385f * w, 0.000f * h)
            lineTo(0.692f * w, 0.616f * h)
            lineTo(0.500f * w, 1.000f * h)
            close()
        }
        drawPath(leftPath, color = color)

        // Right triangle wedge
        val rightPath = Path().apply {
            moveTo(0.690f * w, 0.000f * h)
            lineTo(1.000f * w, 0.000f * h)
            lineTo(0.692f * w, 0.616f * h)
            close()
        }
        drawPath(rightPath, color = color)
    }
}

/**
 * Header lockup with geometric V mark + "VISTEON" + "LUNET" badge
 * mirroring the cluster's top-left branding.
 */
@Composable
fun VisteonHeaderTitle(
    modifier: Modifier = Modifier
) {
    val colors = LunetTheme.colors
    Row(
        modifier = modifier,
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.Start
    ) {
        VisteonLogo(size = 24.dp)
        Spacer(modifier = Modifier.width(10.dp))
        Text(
            text = "VISTEON",
            color = colors.text,
            fontSize = 17.sp,
            fontWeight = FontWeight.Bold,
            fontFamily = FontFamily.SansSerif,
            letterSpacing = 1.2.sp
        )
        Spacer(modifier = Modifier.width(6.dp))
        Box(
            modifier = Modifier
                .background(
                    color = VisteonOrange.copy(alpha = 0.16f),
                    shape = RoundedCornerShape(4.dp)
                )
                .padding(horizontal = 5.dp, vertical = 2.dp)
        ) {
            Text(
                text = "LUNET",
                color = VisteonOrange,
                fontSize = 11.sp,
                fontWeight = FontWeight.Bold,
                fontFamily = FontFamily.Monospace,
                letterSpacing = 0.8.sp
            )
        }
    }
}
