package com.anatdx.yukisu.ui.component

import androidx.compose.foundation.layout.*
import androidx.compose.material3.ElevatedCard
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import com.anatdx.yukisu.ui.theme.getCardColors
import com.anatdx.yukisu.ui.theme.getCardElevation

/** A compact, fully tappable summary; its details live in a native dialog. */
@Composable
internal fun HomeSummaryCard(
    title: String,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    description: String? = null,
    descriptionColor: Color = Color.Unspecified,
) {
    ElevatedCard(
        onClick = onClick,
        modifier = modifier.heightIn(min = 88.dp),
        colors = getCardColors(MaterialTheme.colorScheme.surfaceContainerLow),
        elevation = getCardElevation(),
    ) {
        Column(
            modifier = Modifier.fillMaxSize().padding(16.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.spacedBy(4.dp, Alignment.CenterVertically),
        ) {
            Text(title, style = MaterialTheme.typography.titleSmall, textAlign = TextAlign.Center)
            if (!description.isNullOrBlank()) {
                Text(
                    description,
                    style = MaterialTheme.typography.bodySmall,
                    textAlign = TextAlign.Center,
                    color = if (descriptionColor == Color.Unspecified) MaterialTheme.colorScheme.onSurfaceVariant else descriptionColor,
                )
            }
        }
    }
}

/** Equal columns, with the left status spanning both detail tiles. Height follows text. */
@Composable
internal fun HomeStatusLayout(
    modifier: Modifier = Modifier,
    status: @Composable (Modifier) -> Unit,
    daemon: @Composable (Modifier) -> Unit,
    kernel: @Composable (Modifier) -> Unit,
) {
    Row(
        modifier = modifier.fillMaxWidth().height(IntrinsicSize.Min),
        horizontalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        status(Modifier.weight(1f).fillMaxHeight())
        Column(
            modifier = Modifier.weight(1f).fillMaxHeight(),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            daemon(Modifier.fillMaxWidth().weight(1f))
            kernel(Modifier.fillMaxWidth().weight(1f))
        }
    }
}
