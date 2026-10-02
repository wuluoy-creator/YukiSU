package com.anatdx.yukisu.ui.kasumi

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import com.anatdx.yukisu.R
import com.anatdx.yukisu.ui.component.YukiIcon
import com.anatdx.yukisu.ui.component.YukiTopAppBar
import com.anatdx.yukisu.ui.component.YukiTopBarTitle
import com.anatdx.yukisu.ui.screen.WorkspaceTabs
import ui.screen.moreSettings.component.MoreSettingsItemPosition
import ui.screen.moreSettings.component.SettingsControlGroup

@Composable
internal fun kasumiCardShape(): Shape = MaterialTheme.shapes.medium

@Composable
internal fun KasumiControlGroup(
    position: MoreSettingsItemPosition = MoreSettingsItemPosition.Only,
    content: @Composable ColumnScope.() -> Unit,
) {
    SettingsControlGroup(groupPosition = position, content = content)
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
internal fun KasumiTopBar(
    onBack: () -> Unit,
    onRefresh: () -> Unit,
) {
    YukiTopAppBar(
        title = {
            YukiTopBarTitle(stringResource(R.string.kasumi_title))
        },
        navigationIcon = {
            IconButton(onClick = onBack) {
                YukiIcon(Icons.AutoMirrored.Filled.ArrowBack, stringResource(R.string.back))
            }
        },
        actions = {
            IconButton(onClick = onRefresh) {
                YukiIcon(Icons.Filled.Refresh, stringResource(R.string.kasumi_rules_refresh))
            }
        },
    )
}

@Composable
internal fun KasumiLogActions(content: @Composable RowScope.() -> Unit) {
    Row(
        modifier = Modifier.fillMaxWidth().padding(bottom = 8.dp),
        horizontalArrangement = Arrangement.spacedBy(4.dp),
        verticalAlignment = Alignment.CenterVertically,
        content = content,
    )
}

@Composable
internal fun KasumiTabs(selected: KasumiSection, onSelect: (KasumiSection) -> Unit) {
    WorkspaceTabs(
        labels = KasumiSection.entries.map { stringResource(it.displayNameRes) },
        selected = selected.ordinal,
        onSelected = { onSelect(KasumiSection.entries[it]) },
    )
}
