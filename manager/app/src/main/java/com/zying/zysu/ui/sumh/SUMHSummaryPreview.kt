package com.zying.zysu.ui.sumh

import android.content.res.Configuration
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp
import com.zying.zysu.R
import com.zying.zysu.ui.sumh.util.SUMHUiAdapter as Controller
import com.zying.zysu.ui.theme.UtilityPreviewTheme

@Preview(name = "SUMH cards · compact states", widthDp = 320, heightDp = 1000, locale = "zh-rCN")
@Preview(name = "SUMH cards · dark", widthDp = 360, heightDp = 1000, uiMode = Configuration.UI_MODE_NIGHT_YES)
@Preview(name = "SUMH cards · large text", widthDp = 320, heightDp = 1600, fontScale = 2f, locale = "zh-rCN")
@Preview(name = "SUMH cards · RTL", widthDp = 360, heightDp = 1000, locale = "ar")
@Composable
private fun SUMHSummaryPreview() {
    UtilityPreviewTheme {
        Surface(color = MaterialTheme.colorScheme.background) {
            Column(
                Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp),
                verticalArrangement = Arrangement.spacedBy(12.dp),
            ) {
                (0..7).toList().chunked(2).forEach { pair ->
                    if (LocalDensity.current.fontScale >= 1.3f) {
                        pair.forEach { SUMHSampleCard(it, Modifier.fillMaxWidth()) }
                    } else {
                        Row(Modifier.height(IntrinsicSize.Min), horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                            pair.forEach { SUMHSampleCard(it, Modifier.weight(1f).fillMaxHeight()) }
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun SUMHSampleCard(sample: Int, modifier: Modifier) {
    SUMHStatusSummaryCard(
        status = when (sample) {
            5 -> Controller.SUMHStatus.MODULE_TOO_OLD
            6 -> Controller.SUMHStatus.NOT_PRESENT
            7 -> null
            else -> Controller.SUMHStatus.AVAILABLE
        },
        version = when (sample) {
            5 -> "2"
            6, 7 -> null
            else -> "1"
        },
        systemInfo = Controller.SystemInfo(
            kernel = "6.12.52-android16", mountBase = "none",
            activeMounts = emptyList(), sumhModuleIds = emptyList(),
            sumhMismatch = sample == 5, mismatchMessage = null,
            viewsEnabled = when (sample) { 1 -> false; 2 -> null; else -> true },
        ),
        loading = sample == 3,
        loadError = if (sample == 4) "Update ksud and restart the SUMHP controller" else null,
        unavailableReason = if (sample == 7) stringResource(R.string.home_sumh_requires_manager) else null,
        onRefresh = {},
        modifier = modifier,
    )
}
