package com.anatdx.yukisu.ui.screen

import android.Manifest
import android.annotation.SuppressLint
import android.content.Context
import android.content.pm.PackageManager
import android.os.Build
import android.system.Os
import androidx.annotation.DrawableRes
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.annotation.StringRes
import androidx.compose.animation.*
import androidx.compose.animation.core.tween
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Engineering
import androidx.compose.material.icons.filled.*
import androidx.compose.material.icons.automirrored.filled.NavigateNext
import androidx.compose.material.icons.outlined.Block
import androidx.compose.material.icons.outlined.TaskAlt
import androidx.compose.material.icons.outlined.Warning
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalResources
import androidx.compose.ui.platform.LocalUriHandler
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp
import androidx.core.content.edit
import androidx.core.content.pm.PackageInfoCompat
import androidx.lifecycle.viewmodel.compose.viewModel
import com.ramcosta.composedestinations.annotation.Destination
import com.ramcosta.composedestinations.annotation.RootGraph
import com.ramcosta.composedestinations.generated.destinations.InstallScreenDestination
import com.ramcosta.composedestinations.generated.destinations.DiagnosticsScreenDestination
import com.ramcosta.composedestinations.generated.destinations.ExtensionRuntimeScreenDestination
import com.ramcosta.composedestinations.navigation.DestinationsNavigator
import com.anatdx.yukisu.KernelVersion
import com.anatdx.yukisu.Natives
import com.anatdx.yukisu.R
import com.anatdx.yukisu.integrity.KsudIntegrity
import com.anatdx.yukisu.integrity.KsudIntegrityStatus
import com.anatdx.yukisu.superkey.SuperKeyHelper
import com.anatdx.yukisu.ui.component.KsuIsValid
import com.anatdx.yukisu.ui.kasumi.KasumiSection
import com.anatdx.yukisu.ui.kasumi.KasumiWorkspace
import com.anatdx.yukisu.ui.component.rememberConfirmDialog
import com.anatdx.yukisu.ui.component.YukiIcon
import com.anatdx.yukisu.ui.component.YukiPullToRefreshBox
import com.anatdx.yukisu.ui.component.YukiAlertDialog
import com.anatdx.yukisu.ui.component.clickHapticFeedback
import com.anatdx.yukisu.ui.theme.CardConfig
import com.anatdx.yukisu.ui.theme.getCardColors
import com.anatdx.yukisu.ui.theme.getCardElevation
import com.anatdx.yukisu.ui.theme.isExpressiveUi
import com.anatdx.yukisu.ui.util.LocalSnackbarHost
import com.anatdx.yukisu.ui.util.checkNewVersion
import com.anatdx.yukisu.ui.util.module.LatestVersionInfo
import com.anatdx.yukisu.ui.util.reboot
import com.anatdx.yukisu.ui.viewmodel.HomeViewModel
import com.anatdx.yukisu.ui.component.SuperKeyDialog
import com.anatdx.yukisu.ui.component.rememberSuperKeyDialog
import com.anatdx.yukisu.ui.component.SuperKeyAuthResult
import com.anatdx.yukisu.ui.util.KsuCli
import com.anatdx.yukisu.ui.activity.util.AppData
import com.anatdx.yukisu.update.CiUpdateCard
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

/**
 * @author ShirkNeko
 * @date 2025/9/29.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Destination<RootGraph>(start = true)
@Composable
fun HomeScreen(navigator: DestinationsNavigator) {
    val context = LocalContext.current
    val viewModel = viewModel<HomeViewModel>()
    val coroutineScope = rememberCoroutineScope()
    val ksudIntegrityStatus by KsudIntegrity.status.collectAsState()
    val notificationPermissionLauncher = rememberLauncherForActivityResult(
        ActivityResultContracts.RequestPermission(),
    ) { granted ->
        if (granted) {
            coroutineScope.launch {
                KsudIntegrity.refresh(context, notifyOnMismatch = false)
            }
        }
    }

    LaunchedEffect(key1 = navigator) {
        viewModel.loadUserSettings(context)
        coroutineScope.launch {
            viewModel.loadCoreData()
            delay(100)
            viewModel.loadExtendedData(context)
        }

        // 启动数据变化监听（降低频率减少卡顿）
        coroutineScope.launch {
            while (true) {
                delay(15000) // 每15秒检查一次
                viewModel.autoRefreshIfNeeded(context)
            }
        }
    }

    val homeRefreshKey by viewModel.dataRefreshTrigger.collectAsState()

    // SuperKey 对话框
    val superKeyDialog = rememberSuperKeyDialog()
    var superKeyAuthSuccess by remember { mutableStateOf(false) }
    val snackbarHostState = LocalSnackbarHost.current

    LaunchedEffect(viewModel.isCoreDataLoaded, superKeyAuthSuccess) {
        if (viewModel.isCoreDataLoaded) {
            KsudIntegrity.refresh(context, notifyOnMismatch = false)
        }
    }

    LaunchedEffect(viewModel.isCoreDataLoaded, viewModel.systemStatus.ksuVersion) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU &&
            viewModel.isCoreDataLoaded &&
            viewModel.systemStatus.ksuVersion != null &&
            context.checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) !=
            PackageManager.PERMISSION_GRANTED
        ) {
            val preferences = context.getSharedPreferences("settings", Context.MODE_PRIVATE)
            if (!preferences.getBoolean(
                    KsudIntegrity.NOTIFICATION_PERMISSION_REQUESTED,
                    false,
                )
            ) {
                preferences.edit {
                    putBoolean(KsudIntegrity.NOTIFICATION_PERMISSION_REQUESTED, true)
                }
                notificationPermissionLauncher.launch(Manifest.permission.POST_NOTIFICATIONS)
            }
        }
    }

    // 检查内核是否配置了 SuperKey / 签名（异步，避免阻塞主线程）
    val isSuperKeyConfigured by produceState(initialValue = false) {
        value = withContext(Dispatchers.IO) { Natives.isSuperKeyConfigured() }
    }
    val isSignatureOk by produceState(initialValue = false) {
        value = withContext(Dispatchers.IO) { Natives.isSignatureOk() }
    }
    SuperKeyDialog(
        state = superKeyDialog,
        onAuthenticate = { superKey ->
            // 在 IO 线程执行 Native 调用，避免阻塞主线程
            try {
                kotlinx.coroutines.withContext(kotlinx.coroutines.Dispatchers.IO) {
                    val success = Natives.authenticateSuperKey(superKey)
                    if (success) {
                        SuperKeyHelper.saveSuperKey(context, superKey)
                    }
                    success
                }
            } catch (e: Exception) {
                android.util.Log.e("SuperKey", "Authentication error", e)
                false
            }
        },
        onResult = { result ->
            when (result) {
                is SuperKeyAuthResult.Success -> {
                    superKeyAuthSuccess = true
                    // 强制刷新状态 - 认证成功后需要重新创建 Shell
                    coroutineScope.launch {
                        // 等待内核状态更新
                        delay(100)
                        // 重新创建 Shell（之前的 Shell 没有 root 权限）
                        withContext(Dispatchers.IO) {
                            KsuCli.refreshShells()
                        }
                        // 强制刷新数据
                        viewModel.refreshData(context, forceRefresh = true)
                        withContext(Dispatchers.IO) {
                            AppData.DataRefreshManager.refreshData()
                        }
                    }
                }
                is SuperKeyAuthResult.Error -> {
                    coroutineScope.launch {
                        snackbarHostState.showSnackbar(result.message)
                    }
                }
                SuperKeyAuthResult.Canceled -> {}
            }
        }
    )

    // 自动尝试用保存的 SuperKey 认证
    LaunchedEffect(viewModel.isCoreDataLoaded) {
        if (viewModel.isCoreDataLoaded && !viewModel.systemStatus.isManager) {
            val savedKey = SuperKeyHelper.getSavedSuperKey(context)
            if (!savedKey.isNullOrBlank()) {
                try {
                    // 在 IO 线程执行 Native 调用和 Shell 刷新
                    val success = withContext(Dispatchers.IO) {
                        val authSuccess = Natives.authenticateSuperKey(savedKey)
                        if (authSuccess) {
                            // 重新创建 Shell（之前的 Shell 没有 root 权限）
                            KsuCli.refreshShells()
                        }
                        authSuccess
                    }
                    if (success) {
                        superKeyAuthSuccess = true
                        // 强制刷新数据
                        viewModel.refreshData(context, forceRefresh = true)
                        withContext(Dispatchers.IO) {
                            AppData.DataRefreshManager.refreshData()
                        }
                    }
                } catch (e: Exception) {
                    android.util.Log.e("SuperKey", "Auto-auth error", e)
                }
            }
        }
    }

    val scrollBehavior = TopAppBarDefaults.pinnedScrollBehavior(rememberTopAppBarState())
    val scrollState = rememberScrollState()

    Scaffold(
        topBar = {
            TopBar(
                scrollBehavior = scrollBehavior,
                navigator = navigator,
                isDataLoaded = viewModel.isCoreDataLoaded
            )
        },
        contentWindowInsets = WindowInsets.safeDrawing.only(
            WindowInsetsSides.Top + WindowInsetsSides.Horizontal
        )
    ) { innerPadding ->
        YukiPullToRefreshBox(
            isRefreshing = viewModel.isRefreshing,
            onRefresh = { viewModel.onPullRefresh(context) },
            modifier = Modifier
                .fillMaxSize()
                .padding(innerPadding)
                .consumeWindowInsets(innerPadding)
        ) {
            Column(
                modifier = Modifier
                    .fillMaxSize()
                    .verticalScroll(scrollState)
                    .padding(top = 12.dp, start = 16.dp, end = 16.dp),
                verticalArrangement = Arrangement.spacedBy(12.dp)
            ) {
                // 状态卡片
                if (viewModel.isCoreDataLoaded) {
                    val isNotManager = !viewModel.systemStatus.isManager
                    val needsSuperKeyAuth = isNotManager && !superKeyAuthSuccess && viewModel.systemStatus.ksuVersion == null

                    StatusCard(
                        systemStatus = viewModel.systemStatus,
                        // SuperKey 模式用于表示「主要依赖 SuperKey」，
                        // 显示规则交给 StatusCard 内部根据 isSuperKeyMode + isSignatureOk 决定徽章组合。
                        isSuperKeyMode = isSuperKeyConfigured || superKeyAuthSuccess,
                        needsSuperKeyAuth = needsSuperKeyAuth,
                        onClickInstall = {
                            navigator.navigate(InstallScreenDestination)
                        },
                        onSuperKeyAuth = {
                            superKeyDialog.show()
                        },
                        isSignatureOk = isSignatureOk,
                    )

                    CiUpdateCard()

                    if (ksudIntegrityStatus == KsudIntegrityStatus.MISMATCH) {
                        WarningCard(stringResource(R.string.ksud_integrity_warning))
                    }

                    if (viewModel.systemStatus.requireNewKernel) {
                        WarningCard(
                            message = stringResource(R.string.require_kernel_version),
                            onClick = { navigator.navigate(InstallScreenDestination) },
                        )
                    }
                    if (viewModel.systemStatus.requireNewManager) {
                        WarningCard(stringResource(R.string.require_manager_version))
                    }
                    if (viewModel.systemStatus.showLkmUpdate) {
                        WarningCard(
                            message = stringResource(R.string.home_lkm_update_available),
                            color = MaterialTheme.colorScheme.primary,
                            onClick = { navigator.navigate(InstallScreenDestination) },
                        )
                    }

                    if (viewModel.systemStatus.ksuVersion != null && !viewModel.systemStatus.isRootAvailable) {
                        WarningCard(
                            stringResource(id = R.string.grant_root_failed)
                        )
                    }

                }

                if (viewModel.isExtendedDataLoaded) {
                    val checkUpdate = context.getSharedPreferences("settings", Context.MODE_PRIVATE)
                        .getBoolean("check_update", true)
                    if (checkUpdate) {
                        UpdateCard()
                    }

                    // 信息卡片
                    InfoCard(
                        systemInfo = viewModel.systemInfo,
                        isSimpleMode = viewModel.isSimpleMode,
                        canReadHookType = viewModel.systemStatus.isManager &&
                            viewModel.systemStatus.ksuVersion != null,
                        isHideZygiskImplement = viewModel.isHideZygiskImplement,
                        isHideMetaModuleImplement = viewModel.isHideMetaModuleImplement,
                        isHideSeccompStatus = viewModel.isHideSeccompStatus,
                        ksudIntegrityStatus = ksudIntegrityStatus,
                        onYukiZygiskClick = { navigator.navigate(ExtensionRuntimeScreenDestination(initialPage = 1)) },
                    )

                }

                if (viewModel.systemStatus.isManager && viewModel.systemStatus.ksuVersion != null) {
                    KsuIsValid {
                        KasumiWorkspace(
                            section = KasumiSection.Status,
                            scrollable = false,
                            contentPadding = PaddingValues(0.dp),
                            refreshKey = homeRefreshKey.toInt(),
                        )
                    }
                }

                if (viewModel.isExtendedDataLoaded) {
                    // 链接卡片
                    if (!viewModel.isSimpleMode && !viewModel.isHideLinkCard) {
                        ElevatedCard(
                            colors = getCardColors(MaterialTheme.colorScheme.surfaceContainerLow),
                            elevation = getCardElevation(),
                        ) {
                            ContributionCard()
                            HorizontalDivider(
                                modifier = Modifier.padding(start = 56.dp, end = 16.dp),
                                color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.6f),
                            )
                            DonateCard()
                        }
                    }
                }

                if (!viewModel.isExtendedDataLoaded) {
                    Box(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(24.dp),
                        contentAlignment = Alignment.Center
                    ) {
                        CircularProgressIndicator()
                    }
                }

                Spacer(Modifier.height(16.dp))
            }
        }
    }
}

@Composable
fun UpdateCard() {
    val context = LocalContext.current
    val latestVersionInfo = LatestVersionInfo()
    val newVersion by produceState(initialValue = latestVersionInfo) {
        value = withContext(Dispatchers.IO) {
            checkNewVersion()
        }
    }

    val currentVersionCode = getManagerVersion(context).second
    val newVersionCode = newVersion.versionCode
    val newVersionUrl = newVersion.downloadUrl
    val changelog = newVersion.changelog

    val uriHandler = LocalUriHandler.current
    val title = stringResource(id = R.string.module_changelog)
    val updateText = stringResource(id = R.string.module_update)

    AnimatedVisibility(
        visible = newVersionCode > currentVersionCode,
        enter = fadeIn(tween(180)),
        exit = fadeOut(tween(120))
    ) {
        val updateDialog = rememberConfirmDialog(onConfirm = { uriHandler.openUri(newVersionUrl) })
        WarningCard(
            message = stringResource(id = R.string.new_version_available).format(newVersionCode),
            color = MaterialTheme.colorScheme.outlineVariant,
            onClick = {
                if (changelog.isEmpty()) {
                    uriHandler.openUri(newVersionUrl)
                } else {
                    updateDialog.showConfirm(
                        title = title,
                        content = changelog,
                        markdown = true,
                        confirm = updateText
                    )
                }
            }
        )
    }
}

private data class RebootMenuOption(
    @param:StringRes val label: Int,
    val reason: String = "",
)

@Composable
private fun RebootDropdownItem(
    option: RebootMenuOption,
    index: Int,
    count: Int,
    onSelected: () -> Unit,
) {
    if (isExpressiveUi) {
        val shape = MenuDefaults.itemShape(index, count).shape
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(vertical = ListItemDefaults.SegmentedGap / 2)
                .defaultMinSize(minHeight = 48.dp)
                .clip(shape)
                .background(
                    MaterialTheme.colorScheme.surfaceContainer.copy(
                        alpha = CardConfig.cardAlpha
                    )
                )
                .clickable {
                    onSelected()
                    reboot(option.reason)
                }
                .padding(horizontal = 12.dp, vertical = 6.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Text(
                text = stringResource(option.label),
                style = MaterialTheme.typography.labelLarge,
                fontWeight = FontWeight.Normal,
            )
        }
    } else {
        DropdownMenuItem(
            text = { Text(stringResource(option.label)) },
            onClick = {
                onSelected()
                reboot(option.reason)
            },
        )
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun TopBar(
    scrollBehavior: TopAppBarScrollBehavior? = null,
    navigator: DestinationsNavigator,
    isDataLoaded: Boolean = false
) {
    val context = LocalContext.current
    val colorScheme = MaterialTheme.colorScheme
    val cardColor = if (CardConfig.isCustomBackgroundEnabled) {
        colorScheme.surfaceContainerLow
    } else {
        colorScheme.background
    }

    TopAppBar(
        title = {
            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(10.dp),
            ) {
                Icon(
                    painter = painterResource(R.drawable.ic_launcher_monochrome),
                    contentDescription = null,
                    tint = MaterialTheme.colorScheme.primary,
                    modifier = Modifier.size(28.dp),
                )
                Text(
                    text = stringResource(R.string.app_name),
                    style = MaterialTheme.typography.titleLarge,
                    fontWeight = FontWeight.SemiBold,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                )
            }
        },
        colors = TopAppBarDefaults.topAppBarColors(
            containerColor = cardColor,
            scrolledContainerColor = cardColor
        ),
        actions = {
            if (isDataLoaded) {
                var showDropdown by remember { mutableStateOf(false) }
                KsuIsValid {
                    IconButton(onClick = { navigator.navigate(DiagnosticsScreenDestination) }) {
                        Icon(Icons.Filled.BugReport, contentDescription = stringResource(R.string.injection_section_diagnostics))
                    }
                    IconButton(onClick = {
                        showDropdown = true
                    }) {
                        YukiIcon(
                            imageVector = Icons.Filled.PowerSettingsNew,
                            contentDescription = stringResource(id = R.string.reboot)
                        )

                        val rebootOptions = buildList {
                            add(RebootMenuOption(R.string.reboot))
                            add(RebootMenuOption(R.string.reboot_recovery, "recovery"))
                            add(RebootMenuOption(R.string.reboot_bootloader, "bootloader"))
                            add(RebootMenuOption(R.string.reboot_fastbootd, "fastboot"))
                            add(RebootMenuOption(R.string.reboot_download, "download"))
                            add(RebootMenuOption(R.string.reboot_edl, "edl"))
                        }

                        DropdownMenu(
                            expanded = showDropdown,
                            onDismissRequest = { showDropdown = false },
                            modifier = Modifier.clickHapticFeedback(),
                        ) {
                            rebootOptions.forEachIndexed { index, option ->
                                RebootDropdownItem(
                                    option = option,
                                    index = index,
                                    count = rebootOptions.size,
                                    onSelected = { showDropdown = false },
                                )
                            }
                        }
                    }
                }
            }
        },
        windowInsets = WindowInsets.safeDrawing.only(WindowInsetsSides.Top + WindowInsetsSides.Horizontal),
        scrollBehavior = scrollBehavior
    )
}

@Composable
private fun StatusCard(
    systemStatus: HomeViewModel.SystemStatus,
    isSuperKeyMode: Boolean = false,
    needsSuperKeyAuth: Boolean = false,
    isSignatureOk: Boolean = false,
    onClickInstall: () -> Unit = {},
    onSuperKeyAuth: () -> Unit = {},
) {
    ElevatedCard(
        colors = getCardColors(MaterialTheme.colorScheme.surfaceContainerLow),
        elevation = getCardElevation(),
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .clickable { onClickInstall() }
                .padding(20.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            when {
                systemStatus.ksuVersion != null -> {

                    val workingModeText = when {
                        Natives.isSafeMode -> stringResource(id = R.string.safe_mode)
                        else -> stringResource(id = R.string.home_working)
                    }

                    YukiIcon(
                        Icons.Outlined.TaskAlt,
                        contentDescription = stringResource(R.string.home_working),
                        tint = MaterialTheme.colorScheme.primary,
                        modifier = Modifier
                            .size(28.dp)
                            .padding(
                                horizontal = 4.dp
                            ),
                    )

                    Column(Modifier.padding(start = 16.dp).weight(1f)) {
                        FlowRow(
                            horizontalArrangement = Arrangement.spacedBy(6.dp),
                            verticalArrangement = Arrangement.spacedBy(4.dp),
                            itemVerticalAlignment = Alignment.CenterVertically,
                            modifier = Modifier.fillMaxWidth(),
                        ) {
                            Text(
                                text = workingModeText,
                                style = MaterialTheme.typography.titleMedium,
                                color = MaterialTheme.colorScheme.primary,
                            )

                            when {
                                isSignatureOk && isSuperKeyMode -> {
                                    Surface(
                                        shape = RoundedCornerShape(4.dp),
                                        color = MaterialTheme.colorScheme.secondaryContainer,
                                        modifier = Modifier
                                    ) {
                                        Text(
                                            text = stringResource(id = R.string.home_auth_signature_tag),
                                            style = MaterialTheme.typography.labelMedium,
                                            modifier = Modifier.padding(horizontal = 6.dp, vertical = 2.dp),
                                            color = MaterialTheme.colorScheme.onSecondaryContainer
                                        )
                                    }
                                    Surface(
                                        shape = RoundedCornerShape(4.dp),
                                        color = MaterialTheme.colorScheme.tertiaryContainer,
                                        modifier = Modifier
                                    ) {
                                        Text(
                                            text = stringResource(id = R.string.home_auth_superkey_tag),
                                            style = MaterialTheme.typography.labelMedium,
                                            modifier = Modifier.padding(horizontal = 6.dp, vertical = 2.dp),
                                            color = MaterialTheme.colorScheme.onTertiaryContainer
                                        )
                                    }
                                }
                                isSignatureOk && !isSuperKeyMode -> {
                                    Surface(
                                        shape = RoundedCornerShape(4.dp),
                                        color = MaterialTheme.colorScheme.secondaryContainer,
                                        modifier = Modifier
                                    ) {
                                        Text(
                                            text = stringResource(id = R.string.home_auth_signature_tag),
                                            style = MaterialTheme.typography.labelMedium,
                                            modifier = Modifier.padding(horizontal = 6.dp, vertical = 2.dp),
                                            color = MaterialTheme.colorScheme.onSecondaryContainer
                                        )
                                    }
                                }
                                !isSignatureOk && isSuperKeyMode -> {
                                    Surface(
                                        shape = RoundedCornerShape(4.dp),
                                        color = MaterialTheme.colorScheme.tertiaryContainer,
                                        modifier = Modifier
                                    ) {
                                        Text(
                                            text = stringResource(id = R.string.home_auth_superkey_tag),
                                            style = MaterialTheme.typography.labelMedium,
                                            modifier = Modifier.padding(horizontal = 6.dp, vertical = 2.dp),
                                            color = MaterialTheme.colorScheme.onTertiaryContainer
                                        )
                                    }
                                }
                            }

                            val machine = remember { Os.uname().machine }
                            if (machine != "aarch64") {
                                Surface(
                                    shape = RoundedCornerShape(4.dp),
                                    color = MaterialTheme.colorScheme.primary,
                                    modifier = Modifier
                                ) {
                                    Text(
                                        text = machine,
                                        style = MaterialTheme.typography.labelMedium,
                                        modifier = Modifier.padding(
                                            horizontal = 6.dp,
                                            vertical = 2.dp
                                        ),
                                        color = MaterialTheme.colorScheme.onPrimary
                                    )
                                }
                            }
                        }

                        val ctx = LocalContext.current
                        val isHideVersion by produceState(initialValue = false) {
                            value = withContext(Dispatchers.IO) {
                                ctx.getSharedPreferences("settings", Context.MODE_PRIVATE)
                                    .getBoolean("is_hide_version", false)
                            }
                        }

                        if (!isHideVersion) {
                            Spacer(Modifier.height(4.dp))
                            systemStatus.ksuFullVersion?.let {
                                val ksuver = systemStatus.ksuVersion
                                val versionText = when {
                                    systemStatus.kernelUapiVersion > 0 ->
                                        "$it ($ksuver/${systemStatus.kernelUapiVersion})"
                                    else -> "$it ($ksuver)"
                                }
                                FlowRow(
                                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                                    verticalArrangement = Arrangement.spacedBy(4.dp),
                                ) {
                                    Text(
                                        text = stringResource(R.string.home_working_version, versionText),
                                        style = MaterialTheme.typography.bodyMedium,
                                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                                    )
                                    if (systemStatus.showCustomLkmBadge) {
                                        Surface(
                                            shape = RoundedCornerShape(4.dp),
                                            color = MaterialTheme.colorScheme.tertiaryContainer,
                                        ) {
                                            Text(
                                                text = stringResource(R.string.home_lkm_custom),
                                                style = MaterialTheme.typography.labelMedium,
                                                modifier = Modifier.padding(horizontal = 6.dp, vertical = 2.dp),
                                                color = MaterialTheme.colorScheme.onTertiaryContainer,
                                            )
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                // 需要 SuperKey 认证（未安装或未认证）
                needsSuperKeyAuth -> {
                    YukiIcon(
                        Icons.Outlined.Warning,
                        contentDescription = stringResource(R.string.home_not_installed),
                        tint = MaterialTheme.colorScheme.error,
                        modifier = Modifier
                            .size(28.dp)
                            .padding(horizontal = 4.dp),
                    )

                    Column(Modifier.padding(start = 20.dp).weight(1f)) {
                        Text(
                            text = stringResource(R.string.home_not_installed),
                            style = MaterialTheme.typography.titleMedium,
                            color = MaterialTheme.colorScheme.error
                        )

                        Spacer(Modifier.height(4.dp))
                        Text(
                            text = stringResource(R.string.home_click_to_install),
                            style = MaterialTheme.typography.bodyMedium,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }

                    // 超级密钥认证按钮
                    IconButton(
                        onClick = onSuperKeyAuth,
                        modifier = Modifier.size(48.dp)
                    ) {
                        YukiIcon(
                            imageVector = Icons.Default.Key,
                            contentDescription = stringResource(R.string.superkey_auth_title),
                            tint = MaterialTheme.colorScheme.tertiary
                        )
                    }
                }

                else -> {
                    YukiIcon(
                        Icons.Outlined.Warning,
                        contentDescription = stringResource(R.string.home_not_installed),
                        tint = MaterialTheme.colorScheme.error,
                        modifier = Modifier
                            .size(28.dp)
                            .padding(
                                horizontal = 4.dp
                            ),
                    )

                    Column(
                        Modifier
                            .padding(start = 20.dp)
                            .weight(1f)
                    ) {
                        Text(
                            text = stringResource(R.string.home_not_installed),
                            style = MaterialTheme.typography.titleMedium,
                            color = MaterialTheme.colorScheme.error
                        )

                        Spacer(Modifier.height(4.dp))
                        Text(
                            text = stringResource(R.string.home_click_to_install),
                            style = MaterialTheme.typography.bodyMedium,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                }

            }
        }
    }
}

@Composable
fun WarningCard(
    message: String,
    color: Color = MaterialTheme.colorScheme.error,
    onClick: (() -> Unit)? = null
) {
    ElevatedCard(
        colors = getCardColors(MaterialTheme.colorScheme.surfaceContainerLow),
        elevation = getCardElevation(),
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .then(onClick?.let { Modifier.clickable { it() } } ?: Modifier)
                .padding(16.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            YukiIcon(
                imageVector = Icons.Outlined.Warning,
                contentDescription = null,
                tint = color,
                modifier = Modifier.size(24.dp),
            )
            Spacer(Modifier.width(12.dp))
            Text(
                text = message,
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurface,
                modifier = Modifier.weight(1f),
            )
        }
    }
}

@Composable
private fun HomeLinkRow(
    title: String,
    content: String,
    icon: ImageVector,
    onClick: () -> Unit,
) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .defaultMinSize(minHeight = 64.dp)
            .clickable(onClick = onClick)
            .padding(horizontal = 16.dp, vertical = 14.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(16.dp),
    ) {
        YukiIcon(
            imageVector = icon,
            contentDescription = null,
            tint = MaterialTheme.colorScheme.onSurfaceVariant,
            modifier = Modifier.size(24.dp),
        )
        Column(modifier = Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(4.dp)) {
            Text(text = title, style = MaterialTheme.typography.titleSmall)
            Text(
                text = content,
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
        YukiIcon(
            imageVector = Icons.AutoMirrored.Filled.NavigateNext,
            contentDescription = null,
            tint = MaterialTheme.colorScheme.onSurfaceVariant,
            modifier = Modifier.size(20.dp),
        )
    }
}

@Composable
fun ContributionCard() {
    val uriHandler = LocalUriHandler.current
    HomeLinkRow(
        title = stringResource(R.string.home_ContributionCard_kernelsu),
        content = stringResource(R.string.home_click_to_ContributionCard_kernelsu),
        icon = Icons.Default.Code,
        onClick = { uriHandler.openUri("https://github.com/Rouyashiki/YukiSU") },
    )
}

@Composable
fun DonateCard() {
    val uriHandler = LocalUriHandler.current
    HomeLinkRow(
        title = stringResource(R.string.home_support_title),
        content = stringResource(R.string.home_support_content),
        icon = Icons.Default.FavoriteBorder,
        onClick = { uriHandler.openUri("https://patreon.com/weishu") },
    )
}

@Composable
private fun HomeInfoItem(
    label: String,
    content: String,
    icon: ImageVector? = null,
    @DrawableRes iconRes: Int? = null,
    contentColor: Color = Color.Unspecified,
    onClick: (() -> Unit)? = null,
    trailing: @Composable (() -> Unit)? = null,
) {
    Row(
        verticalAlignment = Alignment.CenterVertically,
        modifier = Modifier
            .fillMaxWidth()
            .defaultMinSize(minHeight = 64.dp)
            .then(onClick?.let { Modifier.clickable(onClick = it) } ?: Modifier)
            .padding(horizontal = 16.dp, vertical = 12.dp),
        horizontalArrangement = Arrangement.spacedBy(16.dp),
    ) {
        if (icon != null || iconRes != null) {
            if (iconRes != null) {
                Icon(
                    painter = painterResource(iconRes),
                    contentDescription = null,
                    tint = MaterialTheme.colorScheme.onSurfaceVariant,
                    modifier = Modifier.size(24.dp),
                )
            } else {
                YukiIcon(
                    imageVector = icon!!,
                    contentDescription = null,
                    tint = MaterialTheme.colorScheme.onSurfaceVariant,
                    modifier = Modifier.size(24.dp),
                )
            }
        }
        Column(modifier = Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(4.dp)) {
            Text(
                text = label,
                style = MaterialTheme.typography.labelMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            Text(
                text = content,
                style = MaterialTheme.typography.bodyMedium,
                color = if (contentColor == Color.Unspecified) MaterialTheme.colorScheme.onSurface else contentColor,
            )
        }
        trailing?.invoke()
        if (onClick != null && trailing == null) {
            YukiIcon(
                imageVector = Icons.AutoMirrored.Filled.NavigateNext,
                contentDescription = null,
                tint = MaterialTheme.colorScheme.onSurfaceVariant,
                modifier = Modifier.size(20.dp),
            )
        }
    }
}

private data class HomeInfoEntry(
    val label: String,
    val content: String,
    val icon: ImageVector? = null,
    @DrawableRes val iconRes: Int? = null,
    val contentColor: Color = Color.Unspecified,
    val onClick: (() -> Unit)? = null,
    val trailing: (@Composable () -> Unit)? = null,
)

@Composable
private fun InfoCard(
    systemInfo: HomeViewModel.SystemInfo,
    isSimpleMode: Boolean,
    canReadHookType: Boolean,
    isHideZygiskImplement: Boolean,
    isHideMetaModuleImplement: Boolean,
    isHideSeccompStatus: Boolean = false,
    ksudIntegrityStatus: KsudIntegrityStatus = KsudIntegrityStatus.UNKNOWN,
    onYukiZygiskClick: () -> Unit = {},
) {
    var showKsudDialog by remember { mutableStateOf(false) }
    var ksudApkVersion by remember { mutableStateOf<String?>(null) }
    var ksudInstalledVersion by remember { mutableStateOf<String?>(null) }

    LaunchedEffect(ksudIntegrityStatus) {
        val (apk, installed) = withContext(Dispatchers.IO) {
            KsuCli.getKsudVersionsForUi()
        }
        ksudApkVersion = apk
        ksudInstalledVersion = installed
    }

    val ksudUnknown = stringResource(id = R.string.home_ksud_daemon_unknown)
    val apkVer = ksudApkVersion
    val installedVer = ksudInstalledVersion
    val hasMismatch = ksudIntegrityStatus == KsudIntegrityStatus.MISMATCH ||
        (apkVer != null && installedVer != null && apkVer != installedVer)
    val ksudContent = when {
        apkVer == null && installedVer == null -> ksudUnknown
        installedVer == null -> apkVer ?: ksudUnknown
        apkVer == null -> installedVer
        apkVer == installedVer -> apkVer
        else -> "$installedVer / APK: $apkVer"
    }
    val seccompText = when (systemInfo.seccompStatus) {
        -1 -> stringResource(R.string.seccomp_status_not_supported)
        0 -> stringResource(R.string.seccomp_status_disabled)
        1 -> stringResource(R.string.seccomp_status_strict)
        2 -> stringResource(R.string.seccomp_status_filter)
        else -> stringResource(R.string.seccomp_status_unknown)
    }
    val hookType = remember(canReadHookType) {
        if (canReadHookType) {
            runCatching(Natives::getHookType).getOrNull()?.takeIf(String::isNotBlank)
        } else {
            null
        }
    }
    val patchType = remember(canReadHookType) {
        if (canReadHookType) {
            when (runCatching(Natives::getLoadMode).getOrDefault(Natives.LOAD_MODE_UNKNOWN)) {
                Natives.LOAD_MODE_RAMDISK -> "Ramdisk"
                Natives.LOAD_MODE_IMAGE_PATCH -> "ImgPatch"
                Natives.LOAD_MODE_LATE -> "Lateload"
                else -> null
            }
        } else {
            null
        }
    }
    val workingMode = hookType?.let { hook ->
        patchType?.let { patch -> "$hook | $patch" } ?: hook
    }
    val entries = buildList {
        add(HomeInfoEntry(
            label = stringResource(R.string.home_kernel),
            content = systemInfo.kernelRelease,
            icon = Icons.Default.Memory,
        ))
        if (!isSimpleMode) {
            add(HomeInfoEntry(
                label = stringResource(R.string.home_android_version),
                content = systemInfo.androidVersion,
                icon = Icons.Default.Android,
            ))
        }
        add(HomeInfoEntry(
            label = stringResource(R.string.home_device_model),
            content = systemInfo.deviceModel,
            icon = Icons.Default.PhoneAndroid,
        ))
        add(HomeInfoEntry(
            label = stringResource(R.string.home_manager_version),
            content = "${systemInfo.managerVersion.first} (${systemInfo.managerVersion.second.toInt()}/${Natives.getManagerUapiVersion()})",
            icon = Icons.Default.SettingsSuggest,
        ))
        add(HomeInfoEntry(
            label = stringResource(id = R.string.home_ksud_daemon_title),
            content = ksudContent,
            icon = Icons.Filled.Engineering,
            contentColor = if (hasMismatch) MaterialTheme.colorScheme.error else Color.Unspecified,
            onClick = { showKsudDialog = true },
        ))
        if (!isSimpleMode && workingMode != null) {
            add(HomeInfoEntry(
                label = stringResource(R.string.home_working_mode),
                content = workingMode,
                icon = Icons.Default.Link,
            ))
        }
        add(HomeInfoEntry(
            label = stringResource(R.string.home_selinux_status),
            content = systemInfo.seLinuxStatus,
            icon = Icons.Default.Security,
        ))
        if (!isHideSeccompStatus) {
            add(HomeInfoEntry(
                label = stringResource(R.string.home_seccomp_status),
                content = seccompText,
                icon = Icons.Default.LocalPolice,
            ))
        }
        if (!isHideZygiskImplement && !isSimpleMode && systemInfo.zygiskImplement != "None") {
            val isYukiZygisk = systemInfo.zygiskImplement == "YukiZygisk"
            add(HomeInfoEntry(
                label = stringResource(R.string.home_zygisk_implement),
                content = systemInfo.zygiskImplement,
                iconRes = R.drawable.ms_syringe,
                trailing = if (isYukiZygisk) {
                    {
                        IconButton(onClick = onYukiZygiskClick) {
                            YukiIcon(
                                imageVector = Icons.Filled.Build,
                                contentDescription = stringResource(R.string.settings_yukizygisk),
                                tint = MaterialTheme.colorScheme.primary,
                            )
                        }
                    }
                } else null,
            ))
        }
        if (!isHideMetaModuleImplement && !isSimpleMode && systemInfo.metaModuleImplement != "None") {
            add(HomeInfoEntry(
                label = stringResource(R.string.home_meta_module_implement),
                content = systemInfo.metaModuleImplement,
                icon = Icons.Default.Extension,
            ))
        }
    }

    ElevatedCard(
        colors = getCardColors(MaterialTheme.colorScheme.surfaceContainerLow),
        elevation = getCardElevation(),
    ) {
        Column(modifier = Modifier.fillMaxWidth()) {
            entries.forEachIndexed { index, entry ->
                HomeInfoItem(
                    label = entry.label,
                    content = entry.content,
                    icon = entry.icon,
                    iconRes = entry.iconRes,
                    contentColor = entry.contentColor,
                    onClick = entry.onClick,
                    trailing = entry.trailing,
                )
                if (index < entries.lastIndex) {
                    HorizontalDivider(
                        modifier = Modifier.padding(start = 56.dp, end = 16.dp),
                        color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.6f),
                    )
                }
            }
        }
    }

    if (showKsudDialog) {
        KsudVersionDialog(
            onDismiss = { showKsudDialog = false },
            onVersionsUpdated = { apk, installed ->
                ksudApkVersion = apk
                ksudInstalledVersion = installed
            }
        )
    }
}

@Composable
private fun KsudVersionDialog(
    onDismiss: () -> Unit,
    onVersionsUpdated: (apk: String?, installed: String?) -> Unit = { _, _ -> }
) {
    val scope = rememberCoroutineScope()
    val context = LocalContext.current
    val resources = LocalResources.current

    var apkVersion by remember { mutableStateOf<String?>(null) }
    var installedVersion by remember { mutableStateOf<String?>(null) }
    var loading by remember { mutableStateOf(true) }
    var syncing by remember { mutableStateOf(false) }

    LaunchedEffect(Unit) {
        loading = true
        val (apk, installed) = KsuCli.getKsudVersionsForUi()
        apkVersion = apk
        installedVersion = installed
        loading = false
    }

    YukiAlertDialog(
        onDismissRequest = { if (!syncing) onDismiss() },
        title = { Text(stringResource(id = R.string.home_ksud_daemon_title)) },
        text = {
            if (loading) {
                Box(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(vertical = 16.dp),
                    contentAlignment = Alignment.Center
                ) {
                    CircularProgressIndicator()
                }
            } else {
                Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    Text(
                        text = stringResource(
                            id = R.string.home_ksud_daemon_apk_version,
                            apkVersion ?: resources.getString(R.string.home_ksud_daemon_unknown)
                        ),
                        style = MaterialTheme.typography.bodyMedium
                    )
                    Text(
                        text = stringResource(
                            id = R.string.home_ksud_daemon_installed_version,
                            installedVersion
                                ?: resources.getString(R.string.home_ksud_daemon_unknown)
                        ),
                        style = MaterialTheme.typography.bodyMedium
                    )
                }
            }
        },
        confirmButton = {
            TextButton(
                onClick = { if (!syncing) onDismiss() }
            ) {
                Text(stringResource(id = R.string.close))
            }
        },
        dismissButton = {
            TextButton(
                enabled = !loading && !syncing,
                onClick = {
                    if (loading || syncing) return@TextButton
                    syncing = true
                    scope.launch {
                        KsuCli.updateKsudDaemonForUi()
                        KsudIntegrity.refresh(context, notifyOnMismatch = false)
                        val (apk, installed) = KsuCli.getKsudVersionsForUi()
                        apkVersion = apk
                        installedVersion = installed
                        onVersionsUpdated(apk, installed)
                        syncing = false
                    }
                }
            ) {
                Text(
                    text = if (syncing)
                        stringResource(id = R.string.home_ksud_daemon_syncing)
                    else
                        stringResource(id = R.string.home_ksud_daemon_sync)
                )
            }
        }
    )
}

fun getManagerVersion(context: Context): Pair<String, Long> {
    val packageInfo = context.packageManager.getPackageInfo(context.packageName, 0)!!
    val versionCode = PackageInfoCompat.getLongVersionCode(packageInfo)
    return Pair(packageInfo.versionName!!, versionCode)
}

@Preview
@Composable
private fun StatusCardPreview() {
    Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
        StatusCard(
            HomeViewModel.SystemStatus(
                isManager = true,
                ksuVersion = 1,
                kernelVersion = KernelVersion(6, 1, 101),
                isRootAvailable = true
            )
        )

        StatusCard(
            HomeViewModel.SystemStatus(
                isManager = true,
                ksuVersion = 10000,
                kernelVersion = KernelVersion(6, 1, 101),
                isRootAvailable = true
            )
        )

        StatusCard(
            HomeViewModel.SystemStatus(
                isManager = false,
                ksuVersion = null,
                kernelVersion = KernelVersion(6, 1, 101),
                isRootAvailable = false
            )
        )

        StatusCard(
            HomeViewModel.SystemStatus(
                isManager = false,
                ksuVersion = null,
                kernelVersion = KernelVersion(4, 10, 101),
                isRootAvailable = false
            )
        )
    }
}

@Composable
private fun IncompatibleKernelCard() {
    val currentKver = remember { Natives.version }
    val threshold   = Natives.MINIMAL_NEW_IOCTL_KERNEL

    val msg = stringResource(
        id = R.string.incompatible_kernel_msg,
        currentKver,
        threshold
    )

    WarningCard(
        message = msg,
        color = MaterialTheme.colorScheme.error
    )
}

@Preview
@Composable
private fun WarningCardPreview() {
    Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
        WarningCard(message = "Warning message")
        WarningCard(
            message = "Warning message ",
            MaterialTheme.colorScheme.outlineVariant,
            onClick = {})
    }
}
