package com.anatdx.yukisu.ui.theme

import android.content.res.Configuration
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.outlined.DarkMode
import androidx.compose.material.icons.outlined.Info
import androidx.compose.material.icons.outlined.Security
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp
import ui.screen.moreSettings.component.*
import com.anatdx.yukisu.ui.screen.WorkspaceTabs

/** Device-independent previews: the same production settings rows and surface tokens. */
@Preview(name = "Light · compact phone", widthDp = 360, heightDp = 740)
@Preview(name = "Dark · compact phone", widthDp = 360, heightDp = 740, uiMode = Configuration.UI_MODE_NIGHT_YES)
@Preview(name = "Large text · narrow phone", widthDp = 320, heightDp = 740, fontScale = 2f)
@Preview(name = "Landscape", widthDp = 800, heightDp = 360)
@Composable
private fun UtilitySettingsPreview() {
    UtilityPreviewTheme {
        UtilitySettingsPreviewContent()
    }
}

/** A preview-only theme that does not read preferences or initialize Root services. */
@Composable
internal fun UtilityPreviewTheme(content: @Composable () -> Unit) {
    val dark = isSystemInDarkTheme()
    val brand = ThemeColors.Default
    val scheme = (if (dark) darkColorScheme() else lightColorScheme()).copy(
        primary = if (dark) brand.primaryDark else brand.primaryLight,
        onPrimary = if (dark) brand.onPrimaryDark else brand.onPrimaryLight,
        primaryContainer = if (dark) brand.primaryContainerDark else brand.primaryContainerLight,
        onPrimaryContainer = if (dark) brand.onPrimaryContainerDark else brand.onPrimaryContainerLight,
    ).utilitySurfaces(dark, false)
    MaterialTheme(colorScheme = scheme, typography = Typography, shapes = ExpressiveShapes) {
        content()
    }
}

@Composable
private fun UtilitySettingsPreviewContent() {
    Surface(color = MaterialTheme.colorScheme.background) {
        Column(
            Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp)
        ) {
            Text("YukiSU", style = MaterialTheme.typography.titleLarge)
            SettingsCard(title = "管理设置") {
                SettingItem(
                    icon = Icons.Outlined.Security,
                    title = "Root 授权管理",
                    subtitle = "应用名称、软件包与授权状态保持清晰可读",
                    onClick = {},
                )
                SwitchSettingItem(
                    icon = Icons.Outlined.DarkMode,
                    title = "跟随系统外观",
                    summary = "浅色与深色模式使用相同的信息层级",
                    checked = true,
                    onChange = {},
                )
                SettingItem(
                    icon = Icons.Outlined.Info,
                    title = "A long application name for checking text wrapping",
                    subtitle = "com.example.application.with.a.long.package.name · 1.0.0",
                    groupPosition = MoreSettingsItemPosition.Last,
                    onClick = {},
                )
            }
        }
    }
}

@Preview(name = "Equal tabs · light", widthDp = 360, heightDp = 300)
@Preview(name = "Equal tabs · dark", widthDp = 360, heightDp = 300, uiMode = Configuration.UI_MODE_NIGHT_YES)
@Preview(name = "Equal tabs · large text", widthDp = 320, heightDp = 540, fontScale = 2f)
@Preview(name = "Equal tabs · landscape", widthDp = 800, heightDp = 320)
@Composable
private fun WorkspaceTabsPreview() {
    UtilityPreviewTheme {
        Surface(color = MaterialTheme.colorScheme.background) {
            Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState())) {
                WorkspaceTabs(listOf("模块", "插件"), 0, {})
                Spacer(Modifier.height(16.dp))
                WorkspaceTabs(listOf("概览", "配置", "诊断"), 1, {})
                Spacer(Modifier.height(16.dp))
                WorkspaceTabs(listOf("挂载", "隔离设置", "生效规则", "内核功能", "高级安全"), 0, {})
            }
        }
    }
}
