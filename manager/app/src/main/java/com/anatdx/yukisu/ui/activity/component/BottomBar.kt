package com.anatdx.yukisu.ui.activity.component

import android.annotation.SuppressLint
import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.navigation.NavHostController
import androidx.navigation.compose.currentBackStackEntryAsState
import com.ramcosta.composedestinations.generated.NavGraphs
import com.ramcosta.composedestinations.utils.isRouteOnBackStackAsState
import com.ramcosta.composedestinations.utils.rememberDestinationsNavigator
import com.anatdx.yukisu.ui.MainActivity
import com.anatdx.yukisu.R
import com.anatdx.yukisu.ui.component.YukiIcon
import com.anatdx.yukisu.ui.component.LiquidGlassSurface
import com.anatdx.yukisu.ui.activity.util.AppData
import com.anatdx.yukisu.ui.screen.BottomBarDestination
import com.anatdx.yukisu.ui.util.LocalNavigationLeaveGuard

@SuppressLint("ContextCastToActivity")
@Composable
fun BottomBar(navController: NavHostController) {
    val navigator = navController.rememberDestinationsNavigator()
    val navigationLeaveGuard = LocalNavigationLeaveGuard.current
    val currentRoute = navController.currentBackStackEntryAsState().value?.destination?.route
    val isFullFeatured by AppData.DataRefreshManager.isFullFeatured.collectAsState()
    val activity = LocalContext.current as MainActivity
    val settings by activity.settingsStateFlow.collectAsState()
    val superuserCount by AppData.DataRefreshManager.superuserCount.collectAsState()
    val moduleCount by AppData.DataRefreshManager.moduleCount.collectAsState()
    val pluginCount by AppData.DataRefreshManager.pluginCount.collectAsState()
    val destinations = BottomBarDestination.entries.filter { isFullFeatured || !it.rootRequired }

    Box(
        modifier = Modifier.fillMaxWidth()
            .windowInsetsPadding(WindowInsets.safeDrawing.only(WindowInsetsSides.Horizontal + WindowInsetsSides.Bottom))
            .padding(horizontal = 16.dp, vertical = 8.dp),
        contentAlignment = Alignment.Center,
    ) {
        LiquidGlassSurface(Modifier.widthIn(max = (destinations.size * 116).dp).fillMaxWidth()) {
            NavigationBar(
                modifier = Modifier.fillMaxWidth(),
                containerColor = Color.Transparent,
                tonalElevation = 0.dp,
                windowInsets = WindowInsets(0, 0, 0, 0),
            ) {
                destinations.forEach { destination ->
                    val selected by navController.isRouteOnBackStackAsState(destination.direction)
                    val count = when (destination) {
                        BottomBarDestination.SuperUser -> superuserCount
                        BottomBarDestination.Module -> moduleCount + pluginCount
                        else -> 0
                    }
                    NavigationBarItem(
                        selected = selected,
                        onClick = {
                            navigationLeaveGuard.navigateOrIntercept(currentRoute) {
                                if (selected) navigator.popBackStack(destination.direction, false)
                                navigator.navigate(destination.direction) {
                                    popUpTo(NavGraphs.root) { saveState = true }
                                    launchSingleTop = true
                                    restoreState = true
                                }
                            }
                        },
                        icon = {
                            BadgedBox(badge = {
                                if (count > 0 && !settings.isHideOtherInfo) {
                                    val countDescription = pluralStringResource(R.plurals.ui_navigation_item_count, count, count)
                                    Badge(
                                        modifier = Modifier.semantics { contentDescription = countDescription },
                                        containerColor = MaterialTheme.colorScheme.primary,
                                        contentColor = MaterialTheme.colorScheme.onPrimary,
                                    ) {
                                        Text(if (count > 99) "99+" else count.toString(),
                                            modifier = Modifier.clearAndSetSemantics {})
                                    }
                                }
                            }) {
                                Box(Modifier.size(32.dp), contentAlignment = Alignment.Center) {
                                    YukiIcon(
                                        if (selected) destination.iconSelected else destination.iconNotSelected,
                                        contentDescription = null,
                                        modifier = Modifier.size(24.dp),
                                    )
                                }
                            }
                        },
                        label = {
                            Text(
                                stringResource(destination.label),
                                style = MaterialTheme.typography.labelMedium,
                                fontWeight = if (selected) FontWeight.SemiBold else FontWeight.Medium,
                            )
                        },
                        alwaysShowLabel = true,
                        colors = NavigationBarItemDefaults.colors(
                            selectedIconColor = MaterialTheme.colorScheme.onPrimaryContainer,
                            selectedTextColor = MaterialTheme.colorScheme.onSurface,
                            indicatorColor = MaterialTheme.colorScheme.primaryContainer,
                            unselectedIconColor = MaterialTheme.colorScheme.onSurface,
                            unselectedTextColor = MaterialTheme.colorScheme.onSurface,
                        ),
                    )
                }
            }
        }
    }
}
