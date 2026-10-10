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
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import com.zying.zysu.ui.component.YukiSwitch
import com.zying.zysu.ui.component.YukiIcon
import com.zying.zysu.ui.component.YukiIconBadge
import com.zying.zysu.ui.component.YukiPanel

enum class MoreSettingsItemPosition(val index: Int, val count: Int) {
    First(0, 3), Middle(1, 3), Last(2, 3), Only(0, 1)
}

@Composable
fun SettingsCard(title: String? = null, icon: ImageVector? = null, content: @Composable () -> Unit) {
    Column(Modifier.fillMaxWidth().padding(bottom = 20.dp)) {
        if (title != null) {
            Row(
                Modifier.fillMaxWidth().padding(horizontal = 12.dp, vertical = 12.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                if (icon != null) {
                    YukiIcon(icon, null, Modifier.size(18.dp), MaterialTheme.colorScheme.primary)
                    Spacer(Modifier.width(8.dp))
                }
                Text(
                    title, modifier = Modifier.semantics { heading() },
                    style = MaterialTheme.typography.titleSmall,
                    fontWeight = FontWeight.SemiBold,
                    color = MaterialTheme.colorScheme.primary,
                )
            }
        }
        YukiPanel { Column(Modifier.padding(vertical = 4.dp)) { content() } }
    }
}

/** The same quiet, rounded icon treatment anchors every preference row. */
@Composable
fun SettingsIconBadge(
    icon: ImageVector,
    tint: Color = MaterialTheme.colorScheme.primary,
    selected: Boolean = false,
) {
    YukiIconBadge(
        icon = icon,
        tint = tint,
        containerColor = if (selected) MaterialTheme.colorScheme.primaryContainer
            else MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.55f),
    )
}

@Composable
fun SettingItem(
    icon: ImageVector,
    title: String,
    subtitle: String? = null,
    groupPosition: MoreSettingsItemPosition = MoreSettingsItemPosition.Middle,
    onClick: () -> Unit,
    iconTint: Color = MaterialTheme.colorScheme.primary,
    trailingContent: @Composable (() -> Unit)? = {
        YukiIcon(Icons.AutoMirrored.Filled.NavigateNext, null, tint = MaterialTheme.colorScheme.onSurfaceVariant)
    },
) {
    Row(
        Modifier.fillMaxWidth().heightIn(min = 76.dp).clickable(role = Role.Button, onClick = onClick)
            .padding(horizontal = 16.dp, vertical = 12.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(14.dp),
    ) {
        SettingsIconBadge(icon, tint = iconTint)
        Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(4.dp)) {
            Text(title, style = MaterialTheme.typography.bodyLarge, fontWeight = FontWeight.Medium)
            if (subtitle != null) Text(
                subtitle, style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
        trailingContent?.invoke()
    }
    if (groupPosition != MoreSettingsItemPosition.Last && groupPosition != MoreSettingsItemPosition.Only) {
        HorizontalDivider(Modifier.padding(start = 74.dp, end = 16.dp), color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.45f))
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
        Modifier.fillMaxWidth().heightIn(min = 76.dp)
            .toggleable(value = checked, enabled = enabled, role = Role.Switch, onValueChange = onChange)
            .alpha(if (enabled) 1f else 0.5f)
            .padding(horizontal = 16.dp, vertical = 12.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(14.dp),
    ) {
        SettingsIconBadge(icon, selected = checked)
        Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(4.dp)) {
            Text(title, style = MaterialTheme.typography.bodyLarge, fontWeight = FontWeight.Medium)
            if (summary != null) Text(
                summary, style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
        YukiSwitch(checked = checked, enabled = enabled, onCheckedChange = null)
    }
    if (groupPosition != MoreSettingsItemPosition.Last && groupPosition != MoreSettingsItemPosition.Only) {
        HorizontalDivider(Modifier.padding(start = 74.dp, end = 16.dp), color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.45f))
    }
}

@Composable
fun SettingsControlGroup(
    groupPosition: MoreSettingsItemPosition = MoreSettingsItemPosition.Middle,
    content: @Composable ColumnScope.() -> Unit,
) {
    Column(Modifier.fillMaxWidth().padding(horizontal = 20.dp, vertical = 16.dp), content = content)
}

@Composable
fun SettingsDivider() {
    HorizontalDivider(Modifier.padding(horizontal = 20.dp), color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.45f))
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
