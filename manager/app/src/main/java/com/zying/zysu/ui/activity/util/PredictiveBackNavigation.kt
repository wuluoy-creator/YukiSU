package com.zying.zysu.ui.activity.util

import androidx.compose.animation.EnterExitState
import androidx.compose.animation.EnterTransition
import androidx.compose.animation.ExitTransition
import androidx.compose.animation.core.Animatable
import androidx.compose.animation.core.Spring
import androidx.compose.animation.core.Transition
import androidx.compose.animation.core.LinearEasing
import androidx.compose.animation.core.animate
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.animateIntOffset
import androidx.compose.animation.core.tween
import androidx.compose.animation.core.spring
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.SideEffect
import androidx.compose.runtime.State
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.drawWithContent
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.graphics.layer.GraphicsLayer
import androidx.compose.ui.graphics.layer.drawLayer
import androidx.compose.ui.graphics.rememberGraphicsLayer
import androidx.compose.ui.layout.onGloballyPositioned
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalLayoutDirection
import androidx.compose.ui.platform.LocalWindowInfo
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.IntSize
import androidx.compose.ui.unit.dp
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.compose.LocalLifecycleOwner
import androidx.navigation.NavGraphBuilder
import androidx.navigation.NavHostController
import androidx.navigation.compose.ComposeNavigator
import androidx.navigation.compose.NavBackStackEntryInfo
import androidx.navigation.get
import androidx.navigationevent.NavigationEvent
import androidx.navigationevent.NavigationEventTransitionState
import androidx.navigationevent.compose.LocalNavigationEventDispatcherOwner
import com.zying.zysu.ui.theme.BackgroundLayer
import com.zying.zysu.ui.component.WallpaperSurface
import com.ramcosta.composedestinations.animations.NavHostAnimatedDestinationStyle
import com.ramcosta.composedestinations.manualcomposablecalls.ManualComposableCallsBuilder
import com.ramcosta.composedestinations.manualcomposablecalls.composable
import com.ramcosta.composedestinations.rememberNavHostEngine
import com.ramcosta.composedestinations.spec.DestinationStyle
import com.ramcosta.composedestinations.spec.Direction
import com.ramcosta.composedestinations.spec.NavGraphSpec
import com.ramcosta.composedestinations.spec.NavHostEngine
import com.ramcosta.composedestinations.spec.TypedDestinationSpec
import kotlinx.coroutines.flow.collectLatest
import kotlin.math.roundToInt

private class BackGesture(val outgoingId: String, val incomingId: String, val direction: Int)

private data class CapturedBackPage(
    val entryId: String,
    val transform: PageTransform,
    val shape: ScreenShape,
    val elevation: Float,
)

private class NavigationMotionState(val captureLayer: GraphicsLayer) {
    var navigation by mutableStateOf(PageNavigation())
    var gesture by mutableStateOf<BackGesture?>(null)
    var progress by mutableFloatStateOf(0f)
    var gestureActive = false
    var capturedPage: CapturedBackPage? = null
    var completion by mutableStateOf<CapturedBackPage?>(null)
    var completionProgress by mutableFloatStateOf(0f)
    var completedEntries by mutableStateOf(emptySet<String>())
}

private val LocalNavigationMotion = staticCompositionLocalOf<NavigationMotionState> {
    error("Navigation motion is not available")
}

internal val LocalPredictiveBackEnabled = staticCompositionLocalOf { false }

@Composable
fun rememberPredictiveBackNavHostEngine(bottomBarRoutes: Set<String>, predictiveBackEnabled: Boolean): NavHostEngine {
    val delegate = rememberNavHostEngine()
    val enabled = rememberUpdatedState(predictiveBackEnabled)
    return remember(delegate, bottomBarRoutes) { PredictiveBackNavHostEngine(delegate, bottomBarRoutes, enabled) }
}

private class PredictiveBackNavHostEngine(
    delegate: NavHostEngine,
    private val bottomBarRoutes: Set<String>,
    private val predictiveBackEnabled: State<Boolean>,
) : NavHostEngine by delegate {
    @Composable
    override fun NavHost(
        modifier: Modifier,
        route: String,
        start: Direction,
        defaultTransitions: NavHostAnimatedDestinationStyle,
        navController: NavHostController,
        builder: NavGraphBuilder.() -> Unit,
    ) {
        val captureLayer = rememberGraphicsLayer()
        val motion = remember(captureLayer) { NavigationMotionState(captureLayer) }
        val density = LocalDensity.current
        val layoutDirection = LocalLayoutDirection.current
        val navigator = navController.navigatorProvider[ComposeNavigator::class]
        val dispatcherOwner = rememberGuardedNavigationDispatcher(navController, predictiveBackEnabled.value)
        val dispatcher = dispatcherOwner.navigationEventDispatcher

        LaunchedEffect(navigator) {
            var previous = navigator.backStack.value
            navigator.backStack.collect { entries ->
                val from = previous.lastOrNull()
                val to = entries.lastOrNull()
                if (from?.id != to?.id) {
                    motion.navigation = PageNavigation(
                        from = from?.id,
                        to = to?.id,
                        pop = previous.any { it.id == to?.id },
                        fromBottomBar = from?.destination?.route in bottomBarRoutes,
                        toBottomBar = to?.destination?.route in bottomBarRoutes,
                    )
                }
                previous = entries
            }
        }

        LaunchedEffect(dispatcher, navigator, density, layoutDirection) {
            dispatcher.transitionState.collectLatest { eventState ->
                val event = (eventState as? NavigationEventTransitionState.InProgress)
                    ?.takeIf { it.direction == NavigationEventTransitionState.TRANSITIONING_BACK }
                    ?.latestEvent
                if (event != null) {
                    if (!motion.gestureActive) {
                        val entries = navigator.backStack.value
                        if (entries.size < 2) return@collectLatest
                        val history = dispatcher.history.value
                        val activeEntry = (history.mergedHistory.getOrNull(history.currentIndex) as? NavBackStackEntryInfo)
                            ?.visibleEntry
                        if (activeEntry?.id != entries.last().id) return@collectLatest
                        motion.completion = null
                        motion.capturedPage = null
                        motion.gesture = BackGesture(
                            outgoingId = entries.last().id,
                            incomingId = entries[entries.lastIndex - 1].id,
                            direction = when (event.swipeEdge) {
                                NavigationEvent.EDGE_LEFT -> 1
                                NavigationEvent.EDGE_RIGHT -> -1
                                else -> 0
                            },
                        )
                    }
                    motion.gestureActive = true
                    motion.progress = event.progress.coerceIn(0f, 1f)
                } else {
                    motion.gestureActive = false
                    val gesture = motion.gesture ?: return@collectLatest
                    val cancelled = navigator.backStack.value.lastOrNull()?.id == gesture.outgoingId
                    if (cancelled) {
                        animate(
                            initialValue = motion.progress,
                            targetValue = 0f,
                            animationSpec = tween(
                                (motion.progress * NavigationDurationMillis).roundToInt(),
                                easing = LinearEasing,
                            ),
                        ) { value, _ -> motion.progress = value }
                    } else {
                        val startProgress = motion.progress
                        motion.completedEntries += gesture.outgoingId
                        motion.completionProgress = 0f
                        // This layer survives destination disposal even when the gesture reached 1f.
                        motion.completion = motion.capturedPage?.takeIf { it.entryId == gesture.outgoingId }
                        animate(0f, 1f, animationSpec = tween(BackCommitDurationMillis, easing = LinearEasing)) { value, _ ->
                            motion.completionProgress = value
                            motion.progress = startProgress + (1f - startProgress) * value
                        }
                    }
                    if (motion.gesture === gesture) {
                        motion.gesture = null
                        motion.completion = null
                        motion.capturedPage = null
                        captureLayer.record(density, layoutDirection, IntSize.Zero) {}
                        motion.completedEntries = motion.completedEntries.intersect(navController.visibleEntries.value.map { it.id }.toSet())
                    }
                }
            }
        }

        CompositionLocalProvider(
            LocalNavigationMotion provides motion,
            LocalPredictiveBackEnabled provides predictiveBackEnabled.value,
            LocalNavigationEventDispatcherOwner provides dispatcherOwner,
        ) {
            Box(modifier) {
                // Keep NavHost's back stack and lifecycle clock, but give the whole page one transform.
                androidx.navigation.compose.NavHost(
                    navController = navController,
                    startDestination = start.route,
                    modifier = Modifier.fillMaxSize(),
                    route = route,
                    contentAlignment = Alignment.Center,
                    enterTransition = { EnterTransition.None },
                    exitTransition = { ExitTransition.None },
                    popEnterTransition = { EnterTransition.None },
                    popExitTransition = { ExitTransition.None },
                    predictivePopEnterTransition = { EnterTransition.None },
                    predictivePopExitTransition = { ExitTransition.None },
                    builder = builder,
                )
                motion.completion?.let { page ->
                    BackCompletionLayer(motion, page)
                }
            }
        }
    }
}

@Composable
private fun BackCompletionLayer(motion: NavigationMotionState, page: CapturedBackPage) {
    Box(
        Modifier
            .fillMaxSize()
            .graphicsLayer {
                val transform = page.transform.completingBack(motion.completionProgress)
                scaleX = transform.scale
                scaleY = transform.scale
                translationX = size.width * transform.offset
                alpha = transform.alpha
                shape = page.shape
                clip = true
                shadowElevation = page.elevation
            }
            .drawWithContent { drawLayer(motion.captureLayer) },
    )
}

fun ManualComposableCallsBuilder.predictiveBackSurfaces(
    navGraph: NavGraphSpec,
    page: @Composable (route: String?, contentModifier: Modifier, content: @Composable () -> Unit) -> Unit,
) {
    navGraph.destinations.forEach { destination ->
        if (destination.style is DestinationStyle.Default || destination.style is DestinationStyle.Animated) {
            predictiveBackSurface(destination, page)
        }
    }
    navGraph.nestedNavGraphs.forEach { predictiveBackSurfaces(it, page) }
}

private fun <T> ManualComposableCallsBuilder.predictiveBackSurface(
    destination: TypedDestinationSpec<T>,
    page: @Composable (route: String?, contentModifier: Modifier, content: @Composable () -> Unit) -> Unit,
) {
    composable(destination) {
        NavigationPage(navBackStackEntry.id, transition) { contentModifier ->
            page(navBackStackEntry.destination.route, contentModifier) { with(destination) { Content() } }
        }
    }
}

@Composable
internal fun NavigationPage(
    entryId: String,
    transition: Transition<EnterExitState>,
    content: @Composable (Modifier) -> Unit,
) {
    val motion = LocalNavigationMotion.current
    val predictiveBackEnabled = LocalPredictiveBackEnabled.current
    val navigation = motion.navigation
    val lifecycleState by LocalLifecycleOwner.current.lifecycle.currentStateFlow.collectAsState()
    val gesture = motion.gesture?.takeIf { it.outgoingId == entryId || it.incomingId == entryId }
    var handledGesture by remember(navigation) { mutableStateOf(false) }
    SideEffect { if (gesture != null) handledGesture = true }

    DisposableEffect(entryId) {
        onDispose { motion.completedEntries -= entryId }
    }

    val pageWidth = LocalWindowInfo.current.containerSize.width
    fun targetTransform(state: EnterExitState) = navigation.transform(
        entryId,
        entering = if (state == EnterExitState.PreEnter) 1f else 0f,
        leaving = if (state == EnterExitState.PostExit) 1f else 0f,
        predictiveBackEnabled = predictiveBackEnabled,
    )
    val offset by transition.animateIntOffset(
        transitionSpec = { PageSlideAnimation },
        label = "page slide",
    ) { IntOffset((targetTransform(it).offset * pageWidth).roundToInt(), 0) }
    val opacity by transition.animateFloat(
        transitionSpec = { navigation.fadeAnimation(entryId, predictiveBackEnabled) },
        label = "page opacity",
    ) { targetTransform(it).alpha }
    val scale by transition.animateFloat(
        transitionSpec = { spring(stiffness = Spring.StiffnessMediumLow) },
        label = "page scale",
    ) { targetTransform(it).scale }

    // NavHost can snap back to the original entry when a push is interrupted. Its child
    // transition may retain an intermediate value; RESUMED means the host has finished.
    fun ordinaryTransform() = if (
        handledGesture || lifecycleState == Lifecycle.State.RESUMED
    ) PageTransform() else PageTransform(
        scale = scale,
        offset = if (pageWidth > 0) offset.x.toFloat() / pageWidth else 0f,
        alpha = opacity,
    )

    // Finish the remaining incoming offset while back takes control; never seek the old slide backwards.
    val incomingOffset = remember(gesture) {
        Animatable(if (gesture?.outgoingId == entryId) ordinaryTransform().offset else 0f)
    }
    LaunchedEffect(gesture) {
        if (gesture != null) incomingOffset.animateTo(0f, tween(100))
    }

    fun pageTransform(): PageTransform = if (gesture != null) {
        predictivePageTransform(motion.progress, gesture.direction, gesture.outgoingId == entryId, incomingOffset.value)
    } else {
        ordinaryTransform()
    }

    val view = LocalView.current
    var screenShape by remember { mutableStateOf(ScreenShape()) }
    val animatedModifier = Modifier
        .graphicsLayer {
            val transform = pageTransform()
            scaleX = transform.scale
            scaleY = transform.scale
            translationX = size.width * transform.offset
            alpha = if (entryId in motion.completedEntries) 0f else transform.alpha.coerceIn(0f, 1f)
            shape = screenShape
            clip = gesture != null
            shadowElevation = if (gesture?.outgoingId == entryId) 16.dp.toPx() * motion.progress else 0f
        }
        .drawWithContent {
            if (gesture?.outgoingId == entryId && motion.gestureActive && motion.gesture === gesture) {
                motion.captureLayer.record { this@drawWithContent.drawContent() }
                motion.capturedPage = CapturedBackPage(entryId, pageTransform(), screenShape, 16.dp.toPx() * motion.progress)
                drawLayer(motion.captureLayer)
            } else {
                drawContent()
            }
            val dim = pageTransform().dim
            if (dim > 0f) drawRect(Color.Black, alpha = dim)
        }
    Box(
        Modifier
            .fillMaxSize()
            .onGloballyPositioned {
                screenShape = ScreenShape.fromInsets(view.rootWindowInsets)
            }
            .then(if (predictiveBackEnabled) animatedModifier else Modifier),
    ) {
        if (predictiveBackEnabled) {
            // Keep the sampled wallpaper and floating bar in the same moving page coordinates.
            WallpaperSurface(background = { BackgroundLayer() }) { content(Modifier) }
        } else {
            content(animatedModifier)
        }
    }
}
