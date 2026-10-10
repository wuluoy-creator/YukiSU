package com.zying.zysu.ui.activity.util

import android.app.Application
import androidx.activity.BackEventCompat
import androidx.activity.ComponentActivity
import androidx.activity.compose.BackHandler
import androidx.compose.animation.AnimatedContentTransitionScope
import androidx.compose.animation.EnterTransition
import androidx.compose.animation.ExitTransition
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.mutableStateOf
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.toPixelMap
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.captureToImage
import androidx.compose.ui.test.junit4.v2.createAndroidComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onRoot
import androidx.lifecycle.Lifecycle
import androidx.navigation.NavBackStackEntry
import androidx.navigation.NavHostController
import androidx.navigation.compose.ComposeNavigator
import androidx.navigation.compose.composable
import androidx.navigation.compose.rememberNavController
import androidx.navigation.get
import com.ramcosta.composedestinations.animations.NavHostAnimatedDestinationStyle
import com.ramcosta.composedestinations.spec.Direction
import com.zying.zysu.ui.util.LocalNavigationLeaveGuard
import com.zying.zysu.ui.util.NavigationLeaveGuard
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config
import org.robolectric.annotation.GraphicsMode
import kotlin.math.abs
import kotlin.test.assertEquals
import kotlin.test.assertTrue

/** Exercises the real page transforms and system-back dispatcher while entry is unfinished. */
@RunWith(RobolectricTestRunner::class)
@Config(application = Application::class, sdk = [35])
@GraphicsMode(GraphicsMode.Mode.NATIVE)
class NavigationBackRegressionTest {
    @get:Rule
    val compose = createAndroidComposeRule<ComponentActivity>()

    private lateinit var navController: NavHostController
    private val leaveGuard = NavigationLeaveGuard()
    private val consumeBack = mutableStateOf(false)

    @Test
    fun immediateSystemBackReturnsToVisibleRoot() = checkBackDuringEntry(predictive = false)

    @Test
    fun immediateSystemBackReturnsToVisibleRootWithPredictiveMotion() = checkBackDuringEntry(predictive = true)

    @Test
    fun immediateSystemBackReturnsToVisibleParent() = checkBackDuringEntry(predictive = false, nested = true)

    @Test
    fun immediateSystemBackReturnsToVisibleParentWithPredictiveMotion() = checkBackDuringEntry(predictive = true, nested = true)

    @Test
    fun earlyGestureCompletionReturnsToVisibleRoot() = checkGestureDuringEntry(cancel = false)

    @Test
    fun earlyGestureCancellationKeepsVisibleChildAndAllowsBack() = checkGestureDuringEntry(cancel = true)

    @Test
    fun duplicateBackBeforeFirstFrameOnlyPopsOnce() {
        setContent(predictive = false)
        compose.runOnUiThread {
            navController.navigate("detail")
            repeat(2) { compose.activity.onBackPressedDispatcher.onBackPressed() }
        }
        settle()
        assertPage("home", "duplicate back before composition")
    }

    @Test
    fun queuedBackCannotPopANewerDestination() {
        setContent(predictive = false)
        compose.runOnUiThread {
            navController.navigate("detail")
            compose.activity.onBackPressedDispatcher.onBackPressed()
            navController.navigate("nested")
        }
        settle()
        assertPage("nested", "queued back after target changes")
    }

    @Test
    fun leaveGuardCanRejectBackAndAllowRetry() {
        setContent(predictive = true)
        var reject: () -> Unit = {}
        var allow: () -> Unit = {}
        var requests = 0
        compose.runOnUiThread {
            leaveGuard.register(this, "detail") { navigate, onIntercepted ->
                requests++
                allow = navigate
                reject = onIntercepted
            }
            navController.navigate("detail")
        }
        settle()
        compose.runOnUiThread {
            repeat(2) { compose.activity.onBackPressedDispatcher.onBackPressed() }
            assertEquals(1, requests)
            reject()
        }
        settle()
        assertPage("detail", "rejected leave request")
        compose.runOnUiThread {
            compose.activity.onBackPressedDispatcher.onBackPressed()
            assertEquals(2, requests)
            allow()
        }
        settle()
        assertPage("home", "approved leave request")
    }

    @Test
    fun destinationBackHandlerKeepsPriority() {
        setContent(predictive = false)
        compose.runOnUiThread {
            consumeBack.value = true
            navController.navigate("detail")
        }
        settle()
        compose.runOnUiThread { compose.activity.onBackPressedDispatcher.onBackPressed() }
        settle()
        assertPage("detail", "destination consumed back")
        compose.runOnUiThread { compose.activity.onBackPressedDispatcher.onBackPressed() }
        settle()
        assertPage("home", "navigation after destination consumed back")
    }

    private fun checkBackDuringEntry(predictive: Boolean, nested: Boolean = false) {
        setContent(predictive)
        val target = if (nested) "detail" else "home"
        if (nested) {
            compose.runOnUiThread { navController.navigate("detail") }
            settle()
        }
        // Repeating also catches a stale completion/captured-page state from the previous pop.
        repeat(2) {
            listOf(0L, 16L, 32L, 100L, 250L).forEach { elapsed ->
                compose.runOnUiThread { navController.navigate(if (nested) "nested" else "detail") }
                if (elapsed > 0) compose.mainClock.advanceTimeBy(elapsed)
                compose.waitForIdle()
                compose.runOnUiThread { compose.activity.onBackPressedDispatcher.onBackPressed() }
                settle()
                assertPage(target, "predictive=$predictive, nested=$nested, entry elapsed=$elapsed")
            }
        }
    }

    private fun checkGestureDuringEntry(cancel: Boolean) {
        setContent(predictive = true)
        listOf(0L, 16L, 32L, 100L, 250L).forEach { elapsed ->
            compose.runOnUiThread { navController.navigate("detail") }
            if (elapsed > 0) compose.mainClock.advanceTimeBy(elapsed)
            compose.waitForIdle()
            compose.runOnUiThread {
                compose.activity.onBackPressedDispatcher.dispatchOnBackStarted(backEvent(0f))
                compose.activity.onBackPressedDispatcher.dispatchOnBackProgressed(backEvent(0.4f))
            }
            compose.mainClock.advanceTimeByFrame()
            compose.runOnUiThread {
                if (cancel) compose.activity.onBackPressedDispatcher.dispatchOnBackCancelled()
                else compose.activity.onBackPressedDispatcher.onBackPressed()
            }
            settle()
            assertPage(if (cancel) "detail" else "home", "gesture cancelled=$cancel, entry elapsed=$elapsed")
            if (cancel) {
                compose.runOnUiThread { compose.activity.onBackPressedDispatcher.onBackPressed() }
                settle()
                assertPage("home", "back after cancelled gesture")
            }
        }
    }

    private fun setContent(predictive: Boolean) {
        compose.setContent {
            MaterialTheme {
                CompositionLocalProvider(LocalNavigationLeaveGuard provides leaveGuard) {
                    navController = rememberNavController()
                    val engine = rememberPredictiveBackNavHostEngine(setOf("home"), predictive)
                    with(engine) {
                        NavHost(
                            modifier = Modifier.fillMaxSize().background(Color.Black),
                            route = "root",
                            start = object : Direction { override val route = "home" },
                            defaultTransitions = NoTransitions,
                            navController = navController,
                        ) {
                            pageColors.forEach { (route, color) ->
                                composable(route) { entry ->
                                    BackHandler(enabled = route == "detail" && consumeBack.value) {
                                        consumeBack.value = false
                                    }
                                    NavigationPage(entry.id, transition) { modifier ->
                                        Box(modifier.fillMaxSize().background(color).testTag(route))
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        compose.waitForIdle()
        compose.mainClock.autoAdvance = false
        assertPage("home", "initial root")
    }

    private fun settle() {
        compose.mainClock.autoAdvance = true
        compose.waitForIdle()
        compose.mainClock.autoAdvance = false
    }

    private fun assertPage(route: String, context: String) {
        compose.runOnUiThread {
            assertEquals(route, navController.currentBackStackEntry?.destination?.route, context)
            assertEquals(Lifecycle.State.RESUMED, navController.currentBackStackEntry?.lifecycle?.currentState, context)
            val navigator = navController.navigatorProvider[ComposeNavigator::class]
            val expectedRoutes = pageColors.keys.takeWhile { it != route } + route
            assertEquals(expectedRoutes, navigator.backStack.value.map { it.destination.route }, context)
        }
        compose.onNodeWithTag(route).assertIsDisplayed()
        // Semantics can remain displayed even if an ancestor graphics layer has alpha zero.
        val pixels = compose.onRoot().captureToImage().toPixelMap()
        val actual = pixels[pixels.width / 2, pixels.height / 2]
        val expected = pageColors.getValue(route)
        assertTrue(
            abs(actual.red - expected.red) < 0.02f &&
                abs(actual.green - expected.green) < 0.02f &&
                abs(actual.blue - expected.blue) < 0.02f,
            "$context: expected visible $route ($expected), center pixel was $actual",
        )
    }

    private fun backEvent(progress: Float) = BackEventCompat(0f, 100f, progress, BackEventCompat.EDGE_LEFT)

    private object NoTransitions : NavHostAnimatedDestinationStyle() {
        override val enterTransition: AnimatedContentTransitionScope<NavBackStackEntry>.() -> EnterTransition = { EnterTransition.None }
        override val exitTransition: AnimatedContentTransitionScope<NavBackStackEntry>.() -> ExitTransition = { ExitTransition.None }
    }

    companion object {
        private val pageColors = mapOf("home" to Color.Red, "detail" to Color.Blue, "nested" to Color.Green)
    }
}
