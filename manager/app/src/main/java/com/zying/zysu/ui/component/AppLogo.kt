package com.zying.zysu.ui.component

import androidx.compose.foundation.Image
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.res.painterResource
import com.zying.zysu.R

@Composable
fun AppLogo(
    modifier: Modifier = Modifier,
    contentDescription: String? = null,
) {
    Image(
        painter = painterResource(R.drawable.ic_zysu),
        contentDescription = contentDescription,
        modifier = modifier.clip(RoundedCornerShape(percent = 22)),
    )
}
