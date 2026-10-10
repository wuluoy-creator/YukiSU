package com.zying.zysu.ui.webui

import androidx.compose.foundation.layout.*
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.outlined.ArrowBack
import androidx.compose.material.icons.outlined.Extension
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import com.zying.zysu.R
import com.zying.zysu.ui.component.YukiIcon
import com.zying.zysu.ui.component.YukiIconBadge
import com.zying.zysu.ui.component.YukiPanel
import com.zying.zysu.ui.component.YukiTopAppBar
import com.zying.zysu.ui.component.YukiTopBarTitle

/** App-owned launch state; module HTML retains its own viewport and inset contract. */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
internal fun WebUILoadingScreen(onBack: () -> Unit) {
    Scaffold(
        topBar = {
            YukiTopAppBar(
                title = { YukiTopBarTitle(stringResource(R.string.ui_module_webui)) },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        YukiIcon(Icons.AutoMirrored.Outlined.ArrowBack, stringResource(R.string.back))
                    }
                },
            )
        },
        contentWindowInsets = WindowInsets.safeDrawing,
    ) { padding ->
        Box(
            Modifier.fillMaxSize().padding(padding).consumeWindowInsets(padding).padding(20.dp),
            contentAlignment = Alignment.Center,
        ) {
            YukiPanel(Modifier.widthIn(max = 420.dp)) {
                Column(
                    Modifier.fillMaxWidth().padding(28.dp),
                    horizontalAlignment = Alignment.CenterHorizontally,
                    verticalArrangement = Arrangement.spacedBy(20.dp),
                ) {
                    YukiIconBadge(Icons.Outlined.Extension)
                    Text(stringResource(R.string.ui_webui_loading), textAlign = TextAlign.Center)
                    CircularProgressIndicator(Modifier.size(28.dp), strokeWidth = 3.dp)
                }
            }
        }
    }
}
