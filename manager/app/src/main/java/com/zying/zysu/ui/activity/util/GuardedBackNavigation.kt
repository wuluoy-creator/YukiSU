package com.zying.zysu.ui.activity.util

import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.rememberUpdatedState
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.compose.LifecycleStartEffect
import androidx.navigation.NavController
import androidx.navigation.NavHostController
import androidx.navigation.compose.NavBackStackEntryInfo
import androidx.navigationevent.NavigationEvent
import androidx.navigationevent.NavigationEventDispatcher
import androidx.navigationevent.NavigationEventDispatcherOwner
import androidx.navigationevent.NavigationEventHandler
import androidx.navigationevent.NavigationEventHistory
import androidx.navigationevent.NavigationEventInfo
import androidx.navigationevent.NavigationEventInput
import androidx.navigationevent.compose.LocalNavigationEventDispatcherOwner
import androidx.navigationevent.compose.rememberNavigationEventDispatcherOwner
import com.zying.zysu.ui.util.LocalNavigationLeaveGuard
import com.zying.zysu.ui.util.NavigationLeaveGuard
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.launch

internal data class BackTarget(val id: String, val route: String?)

internal class ForwardedBackInput : NavigationEventInput() {
    private var attached = false
    private var started = false
    var onEnabledChanged: (Boolean) -> Unit = {}
    var onHistoryUpdated: (NavigationEventHistory) -> Unit = {}

    override fun onAdded(dispatcher: NavigationEventDispatcher) { attached = true }
    override fun onRemoved() {
        attached = false
        started = false
        onEnabledChanged(false)
    }
    override fun onHasEnabledHandlersChanged(hasEnabledHandlers: Boolean) = onEnabledChanged(hasEnabledHandlers)
    override fun onHistoryChanged(history: NavigationEventHistory) = onHistoryUpdated(history)

    fun start(event: NavigationEvent) {
        if (!attached) return
        dispatchOnBackStarted(event)
        started = true
    }

    fun progress(event: NavigationEvent) {
        if (attached && started) dispatchOnBackProgressed(event)
    }

    fun complete() {
        if (!attached) return
        dispatchOnBackCompleted()
        started = false
    }

    fun cancel() {
        if (attached && started) dispatchOnBackCancelled()
        started = false
    }
}

internal class GuardedBackHandler(
    private val input: ForwardedBackInput,
    private val leaveGuard: NavigationLeaveGuard,
    private val predictiveBackEnabled: () -> Boolean = { false },
    private val canNavigateBack: () -> Boolean = { false },
    private val awaitNavigationReady: (BackTarget, () -> Unit) -> Unit = { _, action -> action() },
    private val currentTarget: () -> BackTarget?,
) : NavigationEventHandler<NavigationEventInfo>(NavigationEventInfo.None, isBackEnabled = false) {
    private var revision = 0
    private var pendingTarget: BackTarget? = null
    private var gestureTarget: BackTarget? = null
    private var ignoredGesture = false
    private var hostStarted = false
    private var childEnabled = false

    init {
        input.onEnabledChanged = {
            childEnabled = it
            updateEnabled()
        }
        input.onHistoryUpdated = { history ->
            val index = history.currentIndex
            val entries = history.mergedHistory
            setInfo(
                currentInfo = entries.getOrNull(index) ?: NavigationEventInfo.None,
                backInfo = if (index >= 0) entries.take(index) else emptyList(),
                forwardInfo = if (index >= 0) entries.drop(index + 1) else emptyList(),
            )
        }
    }

    override fun onBackStarted(event: NavigationEvent) {
        ignoredGesture = pendingTarget != null
        if (ignoredGesture) return
        cancelPending()
        gestureTarget = currentTarget()
        val navigationInfo = currentInfo as? NavBackStackEntryInfo
        if (childEnabled && (navigationInfo == null ||
                (predictiveBackEnabled() && navigationInfo.visibleEntry?.id == gestureTarget?.id))) {
            input.start(event)
        }
    }

    override fun onBackProgressed(event: NavigationEvent) {
        onTargetChanged()
        if (!ignoredGesture && pendingTarget == null) input.progress(event)
    }

    override fun onBackCancelled() {
        if (ignoredGesture) {
            ignoredGesture = false
        } else if (pendingTarget == null) {
            cancelPending()
        }
    }

    override fun onBackCompleted() {
        onTargetChanged()
        if (ignoredGesture) {
            ignoredGesture = false
            return
        }
        if (pendingTarget != null) return
        if (childEnabled && currentInfo !is NavBackStackEntryInfo) {
            gestureTarget = null
            input.complete()
            return
        }
        if (!canNavigateBack()) return cancelPending()
        val target = currentTarget() ?: return cancelPending()
        val requestRevision = ++revision
        pendingTarget = target
        fun finish(allowed: Boolean) {
            if (revision != requestRevision || pendingTarget != target) return
            if (!allowed) return cancelPending()
            val complete = {
                if (revision == requestRevision && pendingTarget == target) {
                    pendingTarget = null
                    gestureTarget = null
                    revision++
                    if (currentTarget()?.id == target.id) input.complete() else input.cancel()
                }
            }
            // The stack changes synchronously, but NavHost's handler history updates on the
            // next composition. Keep a back request for the new entry until it is ready.
            if ((currentInfo as? NavBackStackEntryInfo)?.visibleEntry?.id == target.id) {
                complete()
            } else {
                awaitNavigationReady(target, complete)
            }
        }
        leaveGuard.navigateOrIntercept(target.route, onIntercepted = { finish(false) }) {
            finish(true)
        }
    }

    fun cancelPending() {
        revision++
        pendingTarget = null
        gestureTarget = null
        input.cancel()
    }

    fun onTargetChanged() {
        val target = pendingTarget ?: gestureTarget
        if (target != null && target.id != currentTarget()?.id) {
            val ignoreTerminal = pendingTarget == null && gestureTarget != null
            cancelPending()
            if (ignoreTerminal) ignoredGesture = true
        }
        updateEnabled()
    }

    fun setHostStarted(started: Boolean) {
        hostStarted = started
        if (!started) cancelPending()
        updateEnabled()
    }

    private fun updateEnabled() {
        isBackEnabled = hostStarted && (childEnabled || canNavigateBack())
    }
}

@Composable
internal fun rememberGuardedNavigationDispatcher(
    navController: NavHostController,
    predictiveBackEnabled: Boolean,
): NavigationEventDispatcherOwner {
    val parent = checkNotNull(LocalNavigationEventDispatcherOwner.current).navigationEventDispatcher
    val owner = rememberNavigationEventDispatcherOwner(parent = null)
    val dispatcher = owner.navigationEventDispatcher
    val leaveGuard = LocalNavigationLeaveGuard.current
    val input = remember(dispatcher) { ForwardedBackInput() }
    val enabled = rememberUpdatedState(predictiveBackEnabled)
    val scope = rememberCoroutineScope()
    val handler = remember(input, leaveGuard, navController) {
        GuardedBackHandler(
            input,
            leaveGuard,
            predictiveBackEnabled = { enabled.value },
            canNavigateBack = { navController.previousBackStackEntry != null },
            awaitNavigationReady = { target, action ->
                scope.launch {
                    val entry = navController.currentBackStackEntry
                    if (entry?.id == target.id) {
                        combine(navController.currentBackStackEntryFlow, entry.lifecycle.currentStateFlow) { current, state ->
                            current.id != target.id || state == Lifecycle.State.RESUMED || state == Lifecycle.State.DESTROYED
                        }.first { it }
                        action()
                    }
                }
            },
        ) {
            navController.currentBackStackEntry?.let { BackTarget(it.id, it.destination.route) }
        }
    }

    LifecycleStartEffect(handler) {
        handler.setHostStarted(true)
        onStopOrDispose {
            handler.setHostStarted(false)
        }
    }
    DisposableEffect(parent, dispatcher, handler, input) {
        dispatcher.addInput(input)
        parent.addHandler(handler)
        onDispose {
            handler.cancelPending()
            handler.remove()
            dispatcher.removeInput(input)
        }
    }
    DisposableEffect(navController, handler) {
        val listener = NavController.OnDestinationChangedListener { _, _, _ -> handler.onTargetChanged() }
        navController.addOnDestinationChangedListener(listener)
        onDispose { navController.removeOnDestinationChangedListener(listener) }
    }
    return owner
}
