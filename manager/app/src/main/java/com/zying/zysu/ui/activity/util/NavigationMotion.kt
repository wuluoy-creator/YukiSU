package com.zying.zysu.ui.activity.util

import androidx.compose.animation.core.FiniteAnimationSpec
import androidx.compose.animation.core.Spring
import androidx.compose.animation.core.VisibilityThreshold
import androidx.compose.animation.core.spring
import androidx.compose.animation.core.tween
import androidx.compose.ui.unit.IntOffset

internal const val NavigationDurationMillis = 300
internal const val BackCommitDurationMillis = 120

internal val PageSlideAnimation = spring<IntOffset>(
    stiffness = Spring.StiffnessMediumLow,
    visibilityThreshold = IntOffset.VisibilityThreshold,
)

internal data class PageTransform(
    val scale: Float = 1f,
    val offset: Float = 0f,
    val alpha: Float = 1f,
    val dim: Float = 0f,
)

internal fun PageTransform.completingBack(fraction: Float): PageTransform =
    copy(alpha = alpha * (1f - fraction.coerceIn(0f, 1f)))

internal fun predictivePageTransform(
    progress: Float,
    direction: Int,
    outgoing: Boolean,
    incomingOffset: Float = 0f,
): PageTransform {
    val fraction = progress.coerceIn(0f, 1f)
    return if (outgoing) {
        PageTransform(
            scale = 1f - 0.2f * fraction,
            offset = direction * 0.075f * fraction + incomingOffset * (1f - fraction),
        )
    } else {
        PageTransform(
            offset = -direction * 0.025f * (1f - fraction),
            dim = 0.24f * (1f - fraction),
        )
    }
}

internal data class PageNavigation(
    val from: String? = null,
    val to: String? = null,
    val pop: Boolean = false,
    val fromBottomBar: Boolean = false,
    val toBottomBar: Boolean = false,
) {
    fun fadeAnimation(entryId: String, predictiveBackEnabled: Boolean): FiniteAnimationSpec<Float> {
        val useSpring = if (pop && !predictiveBackEnabled) {
            if (entryId == from) !fromBottomBar else toBottomBar
        } else {
            fromBottomBar && !toBottomBar
        }
        return if (useSpring) spring(stiffness = Spring.StiffnessMediumLow) else tween(340)
    }

    fun transform(entryId: String, entering: Float, leaving: Float, predictiveBackEnabled: Boolean = true): PageTransform = when {
        pop && !predictiveBackEnabled && entryId == from -> PageTransform(
            scale = if (fromBottomBar) 1f else 1f - 0.1f * leaving,
            alpha = 1f - leaving,
        )
        pop && !predictiveBackEnabled -> PageTransform(
            offset = if (toBottomBar) -0.25f * entering else 0f,
            alpha = 1f - entering,
        )
        pop && entryId == from -> PageTransform(offset = leaving)
        pop -> PageTransform(offset = -0.25f * entering)
        toBottomBar -> PageTransform(alpha = 1f - entering - leaving)
        else -> PageTransform(
            offset = entering - if (fromBottomBar) 0.25f * leaving else 0f,
            alpha = 1f - leaving,
        )
    }
}
