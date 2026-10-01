package com.anatdx.yukisu.ui

import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.BackHandler
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.consumeWindowInsets
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.layout.only
import androidx.compose.foundation.layout.WindowInsetsSides
import androidx.compose.material3.Scaffold
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.SnackbarHost
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.Alignment
import androidx.compose.ui.unit.dp
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.core.net.toUri
import androidx.lifecycle.lifecycleScope
import androidx.navigation.compose.currentBackStackEntryAsState
import androidx.navigation.compose.rememberNavController
import com.ramcosta.composedestinations.DestinationsNavHost
import com.ramcosta.composedestinations.generated.NavGraphs
import com.ramcosta.composedestinations.generated.destinations.ExecuteModuleActionScreenDestination
import com.ramcosta.composedestinations.generated.destinations.HomeScreenDestination
import com.ramcosta.composedestinations.spec.NavHostGraphSpec
import com.ramcosta.composedestinations.utils.rememberDestinationsNavigator
import com.anatdx.yukisu.Natives
import com.anatdx.yukisu.R
import com.anatdx.yukisu.integrity.KsudIntegrity
import com.anatdx.yukisu.ui.activity.component.BottomBar
import com.anatdx.yukisu.ui.activity.util.AnimatedBottomBar
import com.anatdx.yukisu.ui.activity.util.DataRefreshUtils
import com.anatdx.yukisu.ui.activity.util.DisplayUtils
import com.anatdx.yukisu.ui.activity.util.LocalPredictiveBackEnabled
import com.anatdx.yukisu.ui.activity.util.predictiveBackSurfaces
import com.anatdx.yukisu.ui.activity.util.rememberPredictiveBackEnabled
import com.anatdx.yukisu.ui.activity.util.rememberPredictiveBackNavHostEngine
import com.anatdx.yukisu.ui.activity.util.ThemeChangeContentObserver
import com.anatdx.yukisu.ui.activity.util.ThemeUtils
import com.anatdx.yukisu.ui.activity.util.UltraActivityUtils
import com.anatdx.yukisu.ui.component.InstallConfirmationDialog
import com.anatdx.yukisu.ui.component.ZipFileDetector
import com.anatdx.yukisu.ui.component.ZipFileInfo
import com.anatdx.yukisu.ui.screen.BottomBarDestination
import com.anatdx.yukisu.ui.theme.KernelSUTheme
import com.anatdx.yukisu.ui.util.rememberSnackbarController
import com.anatdx.yukisu.ui.util.KsuCli
import com.anatdx.yukisu.ui.util.LocalNavigationLeaveGuard
import com.anatdx.yukisu.ui.util.LocalSnackbarHost
import com.anatdx.yukisu.ui.util.NavigationLeaveGuard
import com.anatdx.yukisu.ui.util.install
import com.anatdx.yukisu.ui.util.resetTaskDescriptionToAppName
import com.anatdx.yukisu.ui.viewmodel.HomeViewModel
import com.anatdx.yukisu.ui.viewmodel.SuperUserViewModel
import com.anatdx.yukisu.ui.webui.WebUIActivity
import com.anatdx.yukisu.ui.webui.initPlatform
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import ui.screen.moreSettings.util.LocaleHelper

class MainActivity : ComponentActivity() {
    private lateinit var superUserViewModel: SuperUserViewModel
    private lateinit var homeViewModel: HomeViewModel
    internal val settingsStateFlow = MutableStateFlow(SettingsState())
    private val intentState = MutableStateFlow(0)

    data class SettingsState(
        val isHideOtherInfo: Boolean = false
    )

    private var showConfirmationDialog = mutableStateOf(false)
    private var pendingZipFiles = mutableStateOf<List<ZipFileInfo>>(emptyList())

    private lateinit var themeChangeObserver: ThemeChangeContentObserver
    private var isInitialized = false

    override fun attachBaseContext(newBase: Context?) {
        super.attachBaseContext(newBase?.let { LocaleHelper.applyLanguage(it) })
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        try {
            DisplayUtils.applyCustomDpi(this)

            // Enable edge to edge
            enableEdgeToEdge()

            window.isNavigationBarContrastEnforced = false

            super.onCreate(savedInstanceState)
            resetTaskDescriptionToAppName()

            // Note: ksud installation moved to KsuCli.refreshShells()
            // which is called after SuperKey authentication succeeds.
            // In SuperKey mode, isManager is false until authentication,
            // so we can't install ksud here.

            if (!isInitialized) {
                initializeViewModels()
                initializeData()
                isInitialized = true
            }

            setContent {
                KernelSUTheme {
                    val navController = rememberNavController()
                    val snackBarHostState = rememberSnackbarController()
                    val navigationLeaveGuard = remember { NavigationLeaveGuard() }
                    val predictiveBackEnabled by rememberPredictiveBackEnabled()
                    val currentDestination = navController.currentBackStackEntryAsState().value?.destination

                    val bottomBarRoutes = remember {
                        BottomBarDestination.entries.map { it.direction.route }.toSet()
                    }

                    val navigator = navController.rememberDestinationsNavigator()
                    val intentStateValue by intentState.collectAsState()

                    ShortcutIntentHandler(
                        intentState = intentState,
                        navigator = navigator
                    )

                    BackHandler(currentDestination != null && currentDestination.route != HomeScreenDestination.route) {
                        navigationLeaveGuard.navigateOrIntercept(currentDestination?.route) {
                            navigator.navigate(HomeScreenDestination) {
                                navigator.clearBackStack(NavGraphs.root as NavHostGraphSpec)
                                launchSingleTop = true
                            }
                        }
                    }

                    InstallConfirmationDialog(
                        show = showConfirmationDialog.value,
                        zipFiles = pendingZipFiles.value,
                        onConfirm = { confirmedFiles, ak3Options ->
                            showConfirmationDialog.value = false
                            pendingZipFiles.value = emptyList()
                            UltraActivityUtils.navigateToFlashScreen(
                                this,
                                confirmedFiles,
                                ak3Options,
                                navigator,
                            )
                        },
                        onDismiss = {
                            showConfirmationDialog.value = false
                            ZipFileDetector.cleanupStagedFiles(pendingZipFiles.value)
                            pendingZipFiles.value = emptyList()
                            finish()
                        }
                    )

                    LaunchedEffect(intentStateValue) {
                        val zipUri = extractZipUris(intent)
                        if (!zipUri.isNullOrEmpty()) {
                            ZipFileDetector.cleanupStagedFiles(pendingZipFiles.value)
                            pendingZipFiles.value = emptyList()
                            showConfirmationDialog.value = false
                            lifecycleScope.launch {
                                UltraActivityUtils.detectZipTypeAndShowConfirmation(this@MainActivity, zipUri) { infos ->
                                    if (intentState.value != intentStateValue) {
                                        ZipFileDetector.cleanupStagedFiles(infos)
                                        return@detectZipTypeAndShowConfirmation
                                    }
                                    if (infos.isNotEmpty()) {
                                        pendingZipFiles.value = infos
                                        showConfirmationDialog.value = true
                                    } else {
                                        lifecycleScope.launch {
                                            snackBarHostState.showSnackbar(
                                                getString(R.string.unsupported_file_format),
                                                withDismissAction = true
                                            )
                                        }
                                        // 停留在当前 Activity，让用户看到提示
                                    }
                                }
                            }
                        }
                    }

                    LaunchedEffect(Unit) {
                        initPlatform()
                        if (getSharedPreferences("settings", MODE_PRIVATE)
                                .getBoolean("auto_update_ksud", false)
                        ) {
                            KsuCli.autoSyncKsudIfNeeded()
                        }
                    }

                    CompositionLocalProvider(
                        LocalSnackbarHost provides snackBarHostState,
                        LocalNavigationLeaveGuard provides navigationLeaveGuard,
                    ) {
                        Scaffold(
                            containerColor = if (predictiveBackEnabled) Color.Transparent else MaterialTheme.colorScheme.surface,
                            snackbarHost = {
                                if (!predictiveBackEnabled) SnackbarHost(hostState = snackBarHostState.hostState)
                            },
                            bottomBar = {
                                if (!predictiveBackEnabled) {
                                    AnimatedBottomBar.AnimatedBottomBarWrapper(
                                        showBottomBar = currentDestination?.route in bottomBarRoutes,
                                        content = { BottomBar(navController) },
                                    )
                                }
                            },
                            contentWindowInsets = WindowInsets(0, 0, 0, 0),
                        ) { outerPadding ->
                            DestinationsNavHost(
                                modifier = Modifier.fillMaxSize().padding(outerPadding)
                                    .consumeWindowInsets(outerPadding).imePadding(),
                                navGraph = NavGraphs.root as NavHostGraphSpec,
                                navController = navController,
                                engine = rememberPredictiveBackNavHostEngine(bottomBarRoutes, predictiveBackEnabled),
                            ) {
                                predictiveBackSurfaces(NavGraphs.root) { route, contentModifier, content ->
                                    val predictive = LocalPredictiveBackEnabled.current
                                    Scaffold(
                                        containerColor = if (predictive) MaterialTheme.colorScheme.surface else Color.Transparent,
                                        snackbarHost = {
                                            if (predictive) SnackbarHost(hostState = snackBarHostState.hostState)
                                        },
                                        bottomBar = {
                                            if (predictive) {
                                                AnimatedBottomBar.AnimatedBottomBarWrapper(
                                                    showBottomBar = route in bottomBarRoutes,
                                                    content = { BottomBar(navController) },
                                                )
                                            }
                                        },
                                        contentWindowInsets = WindowInsets(0, 0, 0, 0),
                                    ) { innerPadding ->
                                        Box(
                                            Modifier.fillMaxSize().padding(innerPadding)
                                                .consumeWindowInsets(innerPadding)
                                                .windowInsetsPadding(WindowInsets.safeDrawing.only(WindowInsetsSides.Horizontal))
                                                .then(contentModifier),
                                            contentAlignment = Alignment.TopCenter,
                                        ) {
                                            Box(Modifier.widthIn(max = 840.dp).fillMaxSize()) { content() }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        } catch (e: Exception) {
            e.printStackTrace()
        }
    }

    private fun initializeViewModels() {
        superUserViewModel = SuperUserViewModel()
        homeViewModel = HomeViewModel()

        themeChangeObserver = ThemeUtils.registerThemeChangeObserver(this)
    }

    private fun initializeData() {
        lifecycleScope.launch {
            try {
                superUserViewModel.fetchAppList()
            } catch (e: Exception) {
                e.printStackTrace()
            }
        }

        DataRefreshUtils.startDataRefreshCoroutine(lifecycleScope)
        DataRefreshUtils.startSettingsMonitorCoroutine(lifecycleScope, this, settingsStateFlow)

        ThemeUtils.initializeThemeSettings(this, settingsStateFlow)
    }

    override fun onResume() {
        try {
            super.onResume()
            resetTaskDescriptionToAppName()
            ThemeUtils.onActivityResume()

            lifecycleScope.launch {
                withContext(Dispatchers.IO) {
                    val manager = runCatching { Natives.isManager }.getOrDefault(false)
                    if (manager && !KsuCli.SHELL.isRoot) {
                        KsuCli.refreshShells(checkKsud = false)
                    }
                    KsudIntegrity.refresh(this@MainActivity, notifyOnMismatch = false)
                }
                if (isInitialized) {
                    refreshData()
                }
            }
        } catch (e: Exception) {
            e.printStackTrace()
        }
    }

    private fun refreshData() {
        lifecycleScope.launch {
            try {
                superUserViewModel.fetchAppList()
                DataRefreshUtils.refreshData(lifecycleScope)
            } catch (e: Exception) {
                e.printStackTrace()
            }
        }
    }

    override fun onPause() {
        try {
            super.onPause()
            ThemeUtils.onActivityPause(this)
        } catch (e: Exception) {
            e.printStackTrace()
        }
    }

    override fun onDestroy() {
        try {
            if (isFinishing) {
                ZipFileDetector.cleanupStagedFiles(pendingZipFiles.value)
            }
            ThemeUtils.unregisterThemeChangeObserver(this, themeChangeObserver)
            super.onDestroy()
        } catch (e: Exception) {
            e.printStackTrace()
        }
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        setIntent(intent)
        intentState.value++
    }

    private fun extractZipUris(intent: Intent?): ArrayList<Uri>? {
        return when (intent?.action) {
            Intent.ACTION_SEND -> {
                val uri = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                    intent.getParcelableExtra(Intent.EXTRA_STREAM, Uri::class.java)
                } else {
                    @Suppress("DEPRECATION")
                    intent.getParcelableExtra(Intent.EXTRA_STREAM)
                }
                uri?.let(::arrayListOf)
            }

            Intent.ACTION_SEND_MULTIPLE -> {
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                    intent.getParcelableArrayListExtra(Intent.EXTRA_STREAM, Uri::class.java)
                } else {
                    @Suppress("DEPRECATION")
                    intent.getParcelableArrayListExtra(Intent.EXTRA_STREAM)
                }
            }

            else -> when {
                intent?.data != null -> arrayListOf(intent.data!!)
                Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU -> {
                    intent?.getParcelableArrayListExtra("uris", Uri::class.java)
                }

                else -> {
                    @Suppress("DEPRECATION")
                    intent?.getParcelableArrayListExtra("uris")
                }
            }
        }
    }
}

@androidx.compose.runtime.Composable
private fun ShortcutIntentHandler(
    intentState: MutableStateFlow<Int>,
    navigator: com.ramcosta.composedestinations.navigation.DestinationsNavigator
) {
    val activity = androidx.activity.compose.LocalActivity.current ?: return
    val context = LocalContext.current
    val intentStateValue by intentState.collectAsState()
    LaunchedEffect(intentStateValue) {
        val intent = activity.intent
        val type = intent?.getStringExtra("shortcut_type") ?: return@LaunchedEffect
        when (type) {
            "module_action" -> {
                val moduleId = intent.getStringExtra("module_id") ?: return@LaunchedEffect
                navigator.navigate(ExecuteModuleActionScreenDestination(moduleId)) {
                    launchSingleTop = true
                }
            }

            "module_webui" -> {
                val moduleId = intent.getStringExtra("module_id") ?: return@LaunchedEffect
                val moduleName = intent.getStringExtra("module_name") ?: moduleId
                val webIntent = Intent(context, WebUIActivity::class.java)
                    .setData("kernelsu://webui/$moduleId".toUri())
                    .putExtra("id", moduleId)
                    .putExtra("name", moduleName)
                    .putExtra("from_webui_shortcut", true)
                    .addFlags(
                        Intent.FLAG_ACTIVITY_NEW_TASK or
                                Intent.FLAG_ACTIVITY_CLEAR_TASK
                    )
                context.startActivity(webIntent)
            }

            else -> return@LaunchedEffect
        }
    }
}
