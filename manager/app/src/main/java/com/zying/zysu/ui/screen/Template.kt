package com.zying.zysu.ui.screen

import android.content.ClipData
import android.content.ClipboardManager
import android.widget.Toast
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Archive
import androidx.compose.material.icons.filled.Sync
import androidx.compose.material.icons.filled.Security
import androidx.compose.material.icons.automirrored.filled.NavigateNext
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalResources
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.core.content.getSystemService
import androidx.lifecycle.compose.dropUnlessResumed
import androidx.lifecycle.viewmodel.compose.viewModel
import com.ramcosta.composedestinations.annotation.Destination
import com.ramcosta.composedestinations.annotation.RootGraph
import com.ramcosta.composedestinations.generated.destinations.TemplateEditorScreenDestination
import com.ramcosta.composedestinations.navigation.DestinationsNavigator
import com.ramcosta.composedestinations.result.ResultRecipient
import com.ramcosta.composedestinations.result.getOr
import com.zying.zysu.R
import com.zying.zysu.ui.component.YukiIcon
import com.zying.zysu.ui.component.YukiIconBadge
import com.zying.zysu.ui.component.YukiPanel
import com.zying.zysu.ui.component.YukiTopAppBar
import com.zying.zysu.ui.component.YukiTopBarTitle
import com.zying.zysu.ui.component.YukiPullToRefreshBox
import com.zying.zysu.ui.component.clickHapticFeedback
import com.zying.zysu.ui.viewmodel.TemplateViewModel
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch

/**
 * @author weishu
 * @date 2023/10/20.
 */

@OptIn(ExperimentalMaterial3Api::class)
@Destination<RootGraph>
@Composable
fun AppProfileTemplateScreen(
    navigator: DestinationsNavigator,
    resultRecipient: ResultRecipient<TemplateEditorScreenDestination, Boolean>
) {
    val viewModel = viewModel<TemplateViewModel>()
    val scope = rememberCoroutineScope()
    val resources = LocalResources.current
    val topAppBarState = rememberTopAppBarState()
    val scrollBehavior = TopAppBarDefaults.pinnedScrollBehavior(topAppBarState)

    LaunchedEffect(Unit) {
        if (viewModel.templateList.isEmpty()) {
            viewModel.fetchTemplates()
        }
    }

    // handle result from TemplateEditorScreen, refresh if needed
    resultRecipient.onNavResult { result ->
        if (result.getOr { false }) {
            scope.launch { viewModel.fetchTemplates() }
        }
    }

    Scaffold(
        topBar = {
            val context = LocalContext.current
            val clipboardManager = context.getSystemService<ClipboardManager>()
            val showToast = fun(msg: String) {
                scope.launch(Dispatchers.Main) {
                    Toast.makeText(context, msg, Toast.LENGTH_SHORT).show()
                }
            }
            TopBar(
                onBack = dropUnlessResumed { navigator.popBackStack() },
                onSync = {
                    scope.launch { viewModel.fetchTemplates(true) }
                },
                onImport = {
                    scope.launch {
                        val clipboardText = clipboardManager?.primaryClip?.getItemAt(0)?.text?.toString()
                        if (clipboardText.isNullOrEmpty()) {
                            showToast(resources.getString(R.string.app_profile_template_import_empty))
                            return@launch
                        }
                        viewModel.importTemplates(
                            clipboardText,
                            {
                                showToast(resources.getString(R.string.app_profile_template_import_success))
                                viewModel.fetchTemplates(false)
                            },
                            showToast
                        )
                    }
                },
                onExport = {
                    scope.launch {
                        viewModel.exportTemplates(
                            {
                                showToast(resources.getString(R.string.app_profile_template_export_empty))
                            }
                        ) { text ->
                            clipboardManager?.setPrimaryClip(ClipData.newPlainText("", text))
                        }
                    }
                },
                scrollBehavior = scrollBehavior
            )
        },
        floatingActionButton = {
            ExtendedFloatingActionButton(
                onClick = {
                    navigator.navigate(
                        TemplateEditorScreenDestination(
                            TemplateViewModel.TemplateInfo(),
                            false
                        )
                    )
                },
                icon = { YukiIcon(Icons.Filled.Add, null) },
                text = { Text(stringResource(id = R.string.app_profile_template_create)) },
                shape = RoundedCornerShape(percent = 50),
                containerColor = MaterialTheme.colorScheme.primary,
                contentColor = MaterialTheme.colorScheme.onPrimary
            )
        },
        contentWindowInsets = WindowInsets.safeDrawing
    ) { innerPadding ->
        YukiPullToRefreshBox(
            modifier = Modifier.padding(innerPadding),
            isRefreshing = viewModel.isRefreshing,
            onRefresh = {
                scope.launch { viewModel.fetchTemplates() }
            }
        ) {
            LazyColumn(
                modifier = Modifier
                    .fillMaxSize()
                    .nestedScroll(scrollBehavior.nestedScrollConnection),
                verticalArrangement = Arrangement.spacedBy(12.dp),
                contentPadding = PaddingValues(start = 16.dp, end = 16.dp, top = 8.dp, bottom = 96.dp)
            ) {
                items(
                    items = viewModel.templateList,
                    key = { template -> template.id },
                ) { template ->
                    TemplateItem(
                        navigator = navigator,
                        template = template,
                    )
                }
            }
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun TemplateItem(
    navigator: DestinationsNavigator,
    template: TemplateViewModel.TemplateInfo,
) {
    YukiPanel {
        ListItem(
            modifier = Modifier
                .defaultMinSize(minHeight = 88.dp)
                .clickable {
                    navigator.navigate(TemplateEditorScreenDestination(template, !template.local))
                },
            colors = ListItemDefaults.colors(
                containerColor = Color.Transparent,
            ),
            leadingContent = { YukiIconBadge(Icons.Filled.Security) },
            trailingContent = { YukiIcon(Icons.AutoMirrored.Filled.NavigateNext, null) },
            content = {
                Text(
                    text = template.name,
                    style = MaterialTheme.typography.titleMedium,
                )
            },
            supportingContent = {
                Column {
                    Text(
                        text = "${template.id}${if (template.author.isEmpty()) "" else "@${template.author}"}",
                        style = MaterialTheme.typography.bodySmall,
                        fontSize = MaterialTheme.typography.bodySmall.fontSize,
                    )
                    Text(
                        text = template.description,
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                    FlowRow {
                        LabelText(label = "UID: ${template.uid}")
                        LabelText(label = "GID: ${template.gid}")
                        LabelText(label = template.context)
                        if (template.local) {
                            LabelText(label = "local")
                        } else {
                            LabelText(label = "remote")
                        }
                    }
                }
            }
        )
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun TopBar(
    onBack: () -> Unit,
    onSync: () -> Unit = {},
    onImport: () -> Unit = {},
    onExport: () -> Unit = {},
    scrollBehavior: TopAppBarScrollBehavior? = null
) {
    val title: @Composable () -> Unit = {
        YukiTopBarTitle(text = stringResource(R.string.settings_profile_template))
    }
    val navigationIcon: @Composable () -> Unit = {
        IconButton(onClick = onBack) {
            YukiIcon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = stringResource(R.string.back))
        }
    }
    val actions: @Composable RowScope.() -> Unit = {
        IconButton(onClick = onSync) {
            YukiIcon(
                Icons.Filled.Sync,
                contentDescription = stringResource(id = R.string.app_profile_template_sync)
            )
        }

        var showDropdown by remember { mutableStateOf(false) }
        IconButton(onClick = { showDropdown = true }) {
            YukiIcon(
                imageVector = Icons.Filled.Archive,
                contentDescription = stringResource(id = R.string.app_profile_import_export)
            )

            DropdownMenu(expanded = showDropdown, onDismissRequest = {
                showDropdown = false
            }, modifier = Modifier.clickHapticFeedback()) {
                DropdownMenuItem(text = {
                    Text(stringResource(id = R.string.app_profile_import_from_clipboard))
                }, onClick = {
                    onImport()
                    showDropdown = false
                })
                DropdownMenuItem(text = {
                    Text(stringResource(id = R.string.app_profile_export_to_clipboard))
                }, onClick = {
                    onExport()
                    showDropdown = false
                })
            }
        }
    }
    YukiTopAppBar(
        title = title,
        navigationIcon = navigationIcon,
        actions = actions,
        scrollBehavior = scrollBehavior,
    )
}

@Composable
fun LabelText(label: String) {
    Box(
        modifier = Modifier
            .padding(top = 4.dp, end = 4.dp)
            .background(
                MaterialTheme.colorScheme.secondaryContainer.copy(alpha = 0.65f),
                shape = RoundedCornerShape(percent = 50)
            )
    ) {
        Text(
            text = label,
            modifier = Modifier.padding(vertical = 4.dp, horizontal = 8.dp),
            style = MaterialTheme.typography.labelMedium,
            color = MaterialTheme.colorScheme.onSecondaryContainer,
        )
    }
}
