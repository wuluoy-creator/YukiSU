package com.zying.zysu.ui.component

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.selection.selectable
import androidx.compose.foundation.selection.toggleable
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.unit.dp
import com.dergoogler.mmrl.ui.component.LabelItem

/** A single semantic switch target for touch, keyboard and TalkBack. */
@Composable
fun SwitchItem(
    icon: ImageVector? = null,
    title: String,
    summary: String? = null,
    checked: Boolean,
    enabled: Boolean = true,
    beta: Boolean = false,
    onCheckedChange: (Boolean) -> Unit,
) {
    ListItem(
        modifier = Modifier.toggleable(
            value = checked, role = Role.Switch, enabled = enabled,
            onValueChange = onCheckedChange,
        ).alpha(if (enabled) 1f else 0.5f),
        colors = ListItemDefaults.colors(containerColor = Color.Transparent),
        content = {
            Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                Text(title, style = MaterialTheme.typography.bodyLarge)
                if (beta) LabelItem(text = "Beta")
            }
        },
        leadingContent = icon?.let { { YukiIconBadge(it) } },
        trailingContent = {
            YukiSwitch(checked = checked, enabled = enabled, onCheckedChange = null)
        },
        supportingContent = summary?.let {
            { Text(it, style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant) }
        },
    )
}

@Composable
fun RadioItem(title: String, selected: Boolean, onClick: () -> Unit) {
    ListItem(
        modifier = Modifier.selectable(selected = selected, role = Role.RadioButton, onClick = onClick),
        colors = ListItemDefaults.colors(containerColor = Color.Transparent),
        content = { Text(title, style = MaterialTheme.typography.bodyLarge) },
        leadingContent = { RadioButton(selected = selected, onClick = null) },
    )
}
