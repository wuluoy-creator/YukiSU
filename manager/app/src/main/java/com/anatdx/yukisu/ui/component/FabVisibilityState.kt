package com.anatdx.yukisu.ui.component

import android.annotation.SuppressLint
import androidx.compose.animation.*
import androidx.compose.animation.core.MutableTransitionState
import androidx.compose.foundation.lazy.LazyListState
import androidx.compose.runtime.*

@SuppressLint("AutoboxingStateCreation")
@Composable
fun rememberFabVisibilityState(listState: LazyListState): State<Boolean> {
    var previousScrollOffset by remember { mutableStateOf(0) }
    var previousIndex by remember { mutableStateOf(0) }
    val fabVisible = remember { mutableStateOf(true) }

    LaunchedEffect(listState) {
        snapshotFlow { listState.firstVisibleItemIndex to listState.firstVisibleItemScrollOffset }
            .collect { (index, offset) ->
                if (previousIndex == 0 && previousScrollOffset == 0) {
                    fabVisible.value = true
                } else {
                    val isScrollingDown = when {
                        index > previousIndex -> false
                        index < previousIndex -> true
                        else -> offset < previousScrollOffset
                    }

                    fabVisible.value = isScrollingDown
                }

                previousIndex = index
                previousScrollOffset = offset
            }
    }

    return fabVisible
}

@Composable
fun rememberFabTransition(visible: Boolean): MutableTransitionState<Boolean> =
    remember { MutableTransitionState(visible) }.apply { targetState = visible }

@Composable
fun AnimatedFab(visibilityState: MutableTransitionState<Boolean>, content: @Composable () -> Unit) {
    // Scroll-triggered visibility should be brief and never bounce or grow from zero.
    // Compose animation specs honor Android's animator duration scale.
    AnimatedVisibility(
        visibleState = visibilityState,
        enter = fadeIn(androidx.compose.animation.core.tween(160)),
        exit = fadeOut(androidx.compose.animation.core.tween(100)),
    ) { content() }
}
