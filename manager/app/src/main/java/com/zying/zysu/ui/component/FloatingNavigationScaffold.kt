package com.zying.zysu.ui.component

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.consumeWindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.ime
import androidx.compose.material3.Scaffold
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.compositionLocalOf
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.drawWithContent
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.layer.drawLayer
import androidx.compose.ui.graphics.rememberGraphicsLayer
import androidx.compose.ui.layout.onGloballyPositioned
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.unit.dp

/** Scroll containers add this to their end padding; floating controls use it as an offset. */
internal val LocalBottomBarPadding = compositionLocalOf { 0.dp }

/** Measures the dock without shortening the page, including during predictive back. */
@Composable
internal fun FloatingNavigationScaffold(
    showBottomBar: Boolean,
    containerColor: Color,
    bottomBar: @Composable () -> Unit,
    snackbarHost: @Composable () -> Unit,
    content: @Composable () -> Unit,
) {
    // Search pages already apply IME padding; do not reserve the keyboard a second time.
    val dockVisible = showBottomBar && WindowInsets.ime.getBottom(LocalDensity.current) == 0
    val layer = rememberGraphicsLayer()
    val backdrop = remember(layer) { SurfaceBackdrop(layer) }
    CompositionLocalProvider(
        LocalPageBackdrop provides if (dockVisible) backdrop else LocalPageBackdrop.current,
    ) {
        Scaffold(
            containerColor = containerColor,
            bottomBar = { if (dockVisible) bottomBar() },
            snackbarHost = snackbarHost,
            contentWindowInsets = WindowInsets(0, 0, 0, 0),
        ) { padding ->
            CompositionLocalProvider(
                LocalBottomBarPadding provides if (dockVisible) {
                    padding.calculateBottomPadding()
                } else {
                    LocalBottomBarPadding.current
                },
            ) {
                Box(
                    Modifier.fillMaxSize()
                        // Consume the measured safe area, but let the page draw beneath the dock.
                        .consumeWindowInsets(padding)
                        .then(
                            if (dockVisible) Modifier
                                .onGloballyPositioned { backdrop.coordinates = it }
                                .drawWithContent {
                                    // Only page content is recorded; the dock is a sibling slot.
                                    layer.record { this@drawWithContent.drawContent() }
                                    drawLayer(layer)
                                }
                            else Modifier,
                        ),
                ) { content() }
            }
        }
    }
}
