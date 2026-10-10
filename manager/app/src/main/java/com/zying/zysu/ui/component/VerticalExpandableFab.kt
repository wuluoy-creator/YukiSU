package com.zying.zysu.ui.component

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import com.zying.zysu.R

data class FabMenuItem(
    val icon: ImageVector,
    val labelRes: Int,
    val color: Color = Color.Unspecified,
    val onClick: () -> Unit
)

object FabAnimationConfig {
    val BUTTON_SIZE = 56.dp
}

@Composable
fun VerticalExpandableFab(
    menuItems: List<FabMenuItem>,
    modifier: Modifier = Modifier,
    buttonSize: Dp = FabAnimationConfig.BUTTON_SIZE,
    mainButtonIcon: ImageVector = Icons.Filled.Add,
    mainButtonExpandedIcon: ImageVector = Icons.Filled.Close,
    onMainButtonClick: (() -> Unit)? = null,
) {
    var isExpanded by remember { mutableStateOf(false) }
    Box(modifier = modifier.wrapContentSize(), contentAlignment = Alignment.BottomEnd) {
        FloatingActionButton(
            onClick = {
                onMainButtonClick?.invoke()
                isExpanded = !isExpanded
            },
            modifier = Modifier.size(buttonSize),
            shape = CircleShape,
            containerColor = MaterialTheme.colorScheme.primary,
            contentColor = MaterialTheme.colorScheme.onPrimary,
            elevation = FloatingActionButtonDefaults.elevation(defaultElevation = 2.dp, pressedElevation = 2.dp),
        ) {
            YukiIcon(
                if (isExpanded) mainButtonExpandedIcon else mainButtonIcon,
                stringResource(if (isExpanded) R.string.collapse_menu else R.string.expand_menu),
            )
        }
        // Native anchored menu keeps every action readable with large fonts and in landscape.
        DropdownMenu(
            expanded = isExpanded,
            onDismissRequest = { isExpanded = false },
            containerColor = MaterialTheme.colorScheme.surfaceContainerLow,
            shape = MaterialTheme.shapes.medium,
        ) {
            menuItems.forEach { item ->
                DropdownMenuItem(
                    text = { Text(stringResource(item.labelRes)) },
                    leadingIcon = { YukiIcon(item.icon, null) },
                    onClick = {
                        item.onClick()
                        isExpanded = false
                    },
                )
            }
        }
    }
}

object FabMenuPresets {
    fun getScrollMenuItems(
        onScrollToTop: () -> Unit,
        onScrollToBottom: () -> Unit
    ) = listOf(
        FabMenuItem(
            icon = Icons.Filled.KeyboardArrowDown,
            labelRes = R.string.scroll_to_bottom,
            onClick = onScrollToBottom
        ),
        FabMenuItem(
            icon = Icons.Filled.KeyboardArrowUp,
            labelRes = R.string.scroll_to_top,
            onClick = onScrollToTop
        )
    )

    @Composable
    fun getBatchActionMenuItems(
        onCancel: () -> Unit,
        onDeny: () -> Unit,
        onAllow: () -> Unit,
        onUnmountModules: () -> Unit,
        onDisableUnmount: () -> Unit
    ) = listOf(
        FabMenuItem(
            icon = Icons.Filled.Close,
            labelRes = R.string.cancel,
            color = Color.Gray,
            onClick = onCancel
        ),
        FabMenuItem(
            icon = Icons.Filled.Block,
            labelRes = R.string.deny_authorization,
            color = MaterialTheme.colorScheme.error,
            onClick = onDeny
        ),
        FabMenuItem(
            icon = Icons.Filled.Check,
            labelRes = R.string.grant_authorization,
            color = MaterialTheme.colorScheme.primary,
            onClick = onAllow
        ),
        FabMenuItem(
            icon = Icons.Filled.FolderOff,
            labelRes = R.string.unmount_modules,
            onClick = onUnmountModules
        ),
        FabMenuItem(
            icon = Icons.Filled.Folder,
            labelRes = R.string.disable_unmount,
            onClick = onDisableUnmount
        )
    )
}
