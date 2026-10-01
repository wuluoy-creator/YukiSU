package com.anatdx.yukisu.ui.activity.component

import android.annotation.SuppressLint
import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.navigation.NavHostController
import androidx.navigation.compose.currentBackStackEntryAsState
import com.ramcosta.composedestinations.generated.NavGraphs
import com.ramcosta.composedestinations.utils.isRouteOnBackStackAsState
import com.ramcosta.composedestinations.utils.rememberDestinationsNavigator
import com.anatdx.yukisu.ui.MainActivity
import com.anatdx.yukisu.ui.component.YukiIcon
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

    Surface(modifier = Modifier.fillMaxWidth(), color = MaterialTheme.colorScheme.surfaceContainerLow) {
        Column(Modifier.fillMaxWidth(), horizontalAlignment = Alignment.CenterHorizontally) {
            HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant)
            NavigationBar(
                modifier = Modifier.widthIn(max = 840.dp),
                containerColor = MaterialTheme.colorScheme.surfaceContainerLow,
                tonalElevation = 0.dp,
                windowInsets = WindowInsets.safeDrawing.only(WindowInsetsSides.Horizontal + WindowInsetsSides.Bottom),
            ) {
                destinations.forEach { destination ->
                    val selected by navController.isRouteOnBackStackAsState(destination.direction)
                    val count = when (destination) {
                        BottomBarDestination.SuperUser -> superuserCount
                        BottomBarDestination.Module -> moduleCount
                        BottomBarDestination.Plugin -> pluginCount
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
                                    Badge(
                                        containerColor = MaterialTheme.colorScheme.secondaryContainer,
                                        contentColor = MaterialTheme.colorScheme.onSecondaryContainer,
                                    ) { Text(count.toString()) }
                                }
                            }) {
                                YukiIcon(
                                    if (selected) destination.iconSelected else destination.iconNotSelected,
                                    contentDescription = null,
                                )
                            }
                        },
                        label = { Text(stringResource(destination.label), style = MaterialTheme.typography.labelMedium) },
                        alwaysShowLabel = true,
                        colors = NavigationBarItemDefaults.colors(
                            selectedIconColor = MaterialTheme.colorScheme.primary,
                            selectedTextColor = MaterialTheme.colorScheme.primary,
                            indicatorColor = MaterialTheme.colorScheme.primaryContainer,
                            unselectedIconColor = MaterialTheme.colorScheme.onSurfaceVariant,
                            unselectedTextColor = MaterialTheme.colorScheme.onSurfaceVariant,
                        ),
                    )
                }
            }
        }
    }
}
