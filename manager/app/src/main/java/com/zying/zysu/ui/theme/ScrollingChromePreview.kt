package com.zying.zysu.ui.theme

import android.content.res.Configuration
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.outlined.ArrowBack
import androidx.compose.material.icons.outlined.Info
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp
import com.zying.zysu.R
import com.zying.zysu.ui.component.*

/** Production body/header layers, with the body already scrolled beneath the floating header. */
@Preview(name = "Scrolling header · light", widthDp = 360, heightDp = 700)
@Preview(name = "Scrolling header · dark", widthDp = 360, heightDp = 700, uiMode = Configuration.UI_MODE_NIGHT_YES)
@Preview(name = "Scrolling header · large text", widthDp = 320, heightDp = 700, fontScale = 2f)
@Preview(name = "Scrolling header · landscape", widthDp = 800, heightDp = 360)
@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun ScrollingChromePreview() {
    UtilityPreviewTheme {
        val scroll = rememberScrollState(with(LocalDensity.current) { 140.dp.roundToPx() })
        YukiPageScaffold(
            topBar = {
                YukiTopAppBar(
                    title = { YukiTopBarTitle(stringResource(R.string.settings_category_display)) },
                    navigationIcon = {
                        IconButton(onClick = {}) {
                            YukiIcon(Icons.AutoMirrored.Outlined.ArrowBack, stringResource(R.string.back))
                        }
                    },
                )
            },
        ) { padding ->
            Column(
                Modifier.fillMaxSize().verticalScroll(scroll).padding(padding).padding(20.dp),
                verticalArrangement = Arrangement.spacedBy(16.dp),
            ) {
                repeat(8) {
                    YukiPanel {
                        SwitchItem(
                            icon = Icons.Outlined.Info,
                            title = stringResource(R.string.settings_category_display),
                            summary = stringResource(R.string.settings_category_display_summary),
                            checked = it % 2 == 0,
                            onCheckedChange = {},
                        )
                    }
                }
            }
        }
    }
}
