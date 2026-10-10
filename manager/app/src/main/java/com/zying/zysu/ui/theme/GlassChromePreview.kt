package com.zying.zysu.ui.theme

import android.content.res.Configuration
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.outlined.ArrowBack
import androidx.compose.material.icons.outlined.*
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp
import com.zying.zysu.ui.activity.component.GlassNavigationItem
import com.zying.zysu.ui.activity.component.GlassNavigationIcon
import com.zying.zysu.ui.component.*
import com.zying.zysu.R

/** Production chrome and controls, previewable without a device, JNI or navigation graph. */
@Preview(name = "Glass chrome · light", widthDp = 360, heightDp = 780)
@Preview(name = "Glass chrome · dark", widthDp = 360, heightDp = 780, uiMode = Configuration.UI_MODE_NIGHT_YES)
@Preview(name = "Glass chrome · large text", widthDp = 320, heightDp = 780, fontScale = 2f)
@Preview(name = "Glass chrome · 中文大字体", widthDp = 320, heightDp = 780, fontScale = 2f, locale = "zh-rCN")
@Preview(name = "Glass chrome · landscape", widthDp = 800, heightDp = 360)
@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun GlassChromePreview() {
    UtilityPreviewTheme {
        Surface(color = MaterialTheme.colorScheme.background) {
            Column(Modifier.fillMaxSize()) {
                YukiTopAppBar(
                    title = { YukiTopBarTitle("外观设置") },
                    navigationIcon = {
                        IconButton(onClick = {}) { YukiIcon(Icons.AutoMirrored.Outlined.ArrowBack, "返回") }
                    },
                    actions = {
                        IconButton(onClick = {}) { YukiIcon(Icons.Outlined.Search, "搜索") }
                    },
                )
                Column(
                    Modifier.weight(1f).verticalScroll(rememberScrollState()).padding(horizontal = 16.dp),
                ) {
                    YukiSectionHeading("界面与显示")
                    YukiPanel {
                        SwitchItem(
                            icon = Icons.Outlined.DarkMode,
                            title = "跟随系统外观",
                            summary = "浅色与深色模式保持清晰的内容层级",
                            checked = true,
                            onCheckedChange = {},
                        )
                        SwitchItem(
                            icon = Icons.Outlined.Palette,
                            title = "动态主题色",
                            summary = "根据壁纸选择配色",
                            checked = false,
                            onCheckedChange = {},
                        )
                    }
                    Spacer(Modifier.height(16.dp))
                }
                Box(Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 8.dp), contentAlignment = Alignment.Center) {
                    LiquidGlassSurface(Modifier.widthIn(max = 416.dp).fillMaxWidth()) {
                        Row(
                            Modifier.fillMaxWidth().height(IntrinsicSize.Min).padding(4.dp),
                            horizontalArrangement = Arrangement.spacedBy(4.dp),
                        ) {
                            listOf(
                                Icons.Outlined.Home to R.string.home,
                                Icons.Outlined.AdminPanelSettings to R.string.nav_authorization,
                                Icons.Outlined.Extension to R.string.module,
                                Icons.Outlined.Settings to R.string.settings,
                            ).forEachIndexed { index, (icon, label) ->
                                GlassNavigationItem(
                                    modifier = Modifier.weight(1f).fillMaxHeight(),
                                    selected = index == 1,
                                    onClick = {},
                                    icon = {
                                        GlassNavigationIcon(icon, selected = index == 1, count = if (index in 1..2) 142 else 0)
                                    },
                                    label = {
                                        Text(
                                            stringResource(label),
                                            style = MaterialTheme.typography.labelMedium,
                                            fontWeight = if (index == 1) FontWeight.SemiBold else FontWeight.Medium,
                                            textAlign = TextAlign.Center,
                                            maxLines = 2,
                                            overflow = TextOverflow.Ellipsis,
                                        )
                                    },
                                )
                            }
                        }
                    }
                }
            }
        }
    }
}

/** Active search must retain room for the query with back navigation and ancillary actions. */
@Preview(name = "Glass search · narrow large text", widthDp = 320, heightDp = 220, fontScale = 2f)
@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun GlassSearchPreview() {
    UtilityPreviewTheme {
        Surface(color = MaterialTheme.colorScheme.background) {
            SearchAppBar(
                title = { YukiTopBarTitle("日志") },
                searchText = "com.example.app",
                onSearchTextChange = {},
                onClearClick = {},
                onBackClick = {},
                dropdownContent = {
                    IconButton(onClick = {}) { YukiIcon(Icons.Outlined.Refresh, "刷新") }
                    IconButton(onClick = {}) { YukiIcon(Icons.Outlined.Delete, "删除") }
                },
            )
        }
    }
}
