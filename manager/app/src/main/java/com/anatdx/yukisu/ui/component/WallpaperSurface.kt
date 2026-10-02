package com.anatdx.yukisu.ui.component

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.drawWithContent
import androidx.compose.ui.graphics.layer.GraphicsLayer
import androidx.compose.ui.graphics.layer.drawLayer
import androidx.compose.ui.graphics.rememberGraphicsLayer
import androidx.compose.ui.layout.LayoutCoordinates
import androidx.compose.ui.layout.onGloballyPositioned

internal class WallpaperBackdrop(val layer: GraphicsLayer) {
    var coordinates by mutableStateOf<LayoutCoordinates?>(null)
}

internal val LocalWallpaperBackdrop = staticCompositionLocalOf<WallpaperBackdrop?> { null }

/** Records only the wallpaper; navigation controls can never be captured into their own glass. */
@Composable
internal fun WallpaperSurface(
    modifier: Modifier = Modifier,
    background: @Composable () -> Unit,
    content: @Composable () -> Unit,
) {
    val layer = rememberGraphicsLayer()
    val backdrop = remember(layer) { WallpaperBackdrop(layer) }
    CompositionLocalProvider(LocalWallpaperBackdrop provides backdrop) {
        Box(modifier.fillMaxSize()) {
            Box(
                Modifier.matchParentSize()
                    .onGloballyPositioned { backdrop.coordinates = it }
                    .drawWithContent {
                        layer.record { this@drawWithContent.drawContent() }
                        drawLayer(layer)
                    },
            ) { background() }
            Box(Modifier.matchParentSize()) { content() }
        }
    }
}
