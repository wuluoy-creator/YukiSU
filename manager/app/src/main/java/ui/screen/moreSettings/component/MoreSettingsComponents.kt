package ui.screen.moreSettings.component

import androidx.compose.foundation.*
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.selection.toggleable
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.NavigateNext
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import com.anatdx.yukisu.ui.theme.CardConfig
import com.anatdx.yukisu.ui.component.YukiSwitch
import com.anatdx.yukisu.ui.component.YukiIcon

enum class MoreSettingsItemPosition(val index: Int, val count: Int) {
    First(0, 3), Middle(1, 3), Last(2, 3), Only(0, 1)
}

@Composable
fun SettingsCard(title: String? = null, icon: ImageVector? = null, content: @Composable () -> Unit) {
    Column(Modifier.fillMaxWidth().padding(bottom = 24.dp)) {
        if (title != null) {
            Row(
                Modifier.fillMaxWidth().padding(horizontal = 8.dp, vertical = 12.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                if (icon != null) {
                    YukiIcon(icon, null, Modifier.size(20.dp), MaterialTheme.colorScheme.onSurfaceVariant)
                    Spacer(Modifier.width(12.dp))
                }
                Text(
                    title, modifier = Modifier.semantics { heading() },
                    style = MaterialTheme.typography.titleSmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
        }
        Surface(
            shape = MaterialTheme.shapes.medium,
            color = MaterialTheme.colorScheme.surfaceContainerLow.copy(alpha = CardConfig.cardAlpha),
        ) { Column { content() } }
    }
}

@Composable
fun SettingItem(
    icon: ImageVector,
    title: String,
    subtitle: String? = null,
    groupPosition: MoreSettingsItemPosition = MoreSettingsItemPosition.Middle,
    onClick: () -> Unit,
    iconTint: Color = MaterialTheme.colorScheme.onSurfaceVariant,
    trailingContent: @Composable (() -> Unit)? = {
        YukiIcon(Icons.AutoMirrored.Filled.NavigateNext, null, tint = MaterialTheme.colorScheme.onSurfaceVariant)
    },
) {
    Row(
        Modifier.fillMaxWidth().heightIn(min = 64.dp).clickable(onClick = onClick)
            .padding(horizontal = 16.dp, vertical = 12.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(16.dp),
    ) {
        YukiIcon(icon, null, Modifier.size(24.dp), iconTint)
        Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(4.dp)) {
            Text(title, style = MaterialTheme.typography.bodyLarge)
            if (subtitle != null) Text(
                subtitle, style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
        trailingContent?.invoke()
    }
    if (groupPosition != MoreSettingsItemPosition.Last && groupPosition != MoreSettingsItemPosition.Only) {
        HorizontalDivider(Modifier.padding(start = 56.dp), color = MaterialTheme.colorScheme.outlineVariant)
    }
}

@Composable
fun SwitchSettingItem(
    icon: ImageVector,
    title: String,
    summary: String? = null,
    checked: Boolean,
    enabled: Boolean = true,
    groupPosition: MoreSettingsItemPosition = MoreSettingsItemPosition.Middle,
    onChange: (Boolean) -> Unit,
) {
    Row(
        Modifier.fillMaxWidth().heightIn(min = 64.dp)
            .toggleable(value = checked, enabled = enabled, role = Role.Switch, onValueChange = onChange)
            .alpha(if (enabled) 1f else 0.5f)
            .padding(horizontal = 16.dp, vertical = 12.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(16.dp),
    ) {
        YukiIcon(icon, null, Modifier.size(24.dp), MaterialTheme.colorScheme.onSurfaceVariant)
        Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(4.dp)) {
            Text(title, style = MaterialTheme.typography.bodyLarge)
            if (summary != null) Text(
                summary, style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
        YukiSwitch(checked = checked, enabled = enabled, onCheckedChange = null)
    }
    if (groupPosition != MoreSettingsItemPosition.Last && groupPosition != MoreSettingsItemPosition.Only) {
        HorizontalDivider(Modifier.padding(start = 56.dp), color = MaterialTheme.colorScheme.outlineVariant)
    }
}

@Composable
fun SettingsControlGroup(
    groupPosition: MoreSettingsItemPosition = MoreSettingsItemPosition.Middle,
    content: @Composable ColumnScope.() -> Unit,
) {
    Column(Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 12.dp), content = content)
}

@Composable
fun SettingsDivider() {
    HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant)
}

@Composable
fun ColorCircle(color: Color, isSelected: Boolean, modifier: Modifier = Modifier) {
    Box(
        modifier.size(20.dp).clip(CircleShape).background(color).then(
            if (isSelected) Modifier.border(2.dp, MaterialTheme.colorScheme.primary, CircleShape)
            else Modifier
        )
    )
}
