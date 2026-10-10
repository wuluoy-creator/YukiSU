package com.zying.zysu.ui.component

import androidx.annotation.DrawableRes
import androidx.compose.material3.Icon
import androidx.compose.material3.LocalContentColor
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.res.painterResource
import com.zying.zysu.R
import com.zying.zysu.ui.theme.isExpressiveUi

/** One rounded symbol family across navigation, settings, tools and status messages. */
@Composable
fun YukiIcon(
    imageVector: ImageVector,
    contentDescription: String?,
    modifier: Modifier = Modifier,
    tint: Color = LocalContentColor.current,
    filled: Boolean = false,
) {
    val expressiveDrawable = if (isExpressiveUi) {
        (if (filled) imageVector.toFilledNavigationDrawable() else null)
            ?: imageVector.toExpressiveDrawable()
    } else {
        null
    }

    if (expressiveDrawable != null) {
        Icon(
            painter = painterResource(expressiveDrawable),
            contentDescription = contentDescription,
            modifier = modifier,
            tint = tint
        )
    } else {
        Icon(
            imageVector = imageVector,
            contentDescription = contentDescription,
            modifier = modifier,
            tint = tint
        )
    }
}

@DrawableRes
private fun ImageVector.toFilledNavigationDrawable(): Int? = when (name.substringAfterLast('.')) {
    "Home" -> R.drawable.ms_home_filled
    "AdminPanelSettings" -> R.drawable.ms_admin_panel_settings_filled
    "Extension" -> R.drawable.ms_extension_filled
    "Settings" -> R.drawable.ms_settings_filled
    else -> null
}

@DrawableRes
private fun ImageVector.toExpressiveDrawable(): Int? = when (name.substringAfterLast('.')) {
    "Adb" -> R.drawable.ms_adb
    "Add" -> R.drawable.ms_add
    "AddCircle" -> R.drawable.ms_add_circle
    "AdminPanelSettings" -> R.drawable.ms_admin_panel_settings
    "Android" -> R.drawable.ms_android
    "Archive" -> R.drawable.ms_archive
    "AutoAwesome" -> R.drawable.ms_auto_awesome
    "ArrowBack" -> R.drawable.ms_arrow_back
    "Block" -> R.drawable.ms_block
    "BugReport" -> R.drawable.ms_bug_report
    "Build" -> R.drawable.ms_build
    "Brush" -> R.drawable.ms_brush
    "Check" -> R.drawable.ms_check
    "Close" -> R.drawable.ms_close
    "ColorLens" -> R.drawable.ms_color_lens
    "DarkMode" -> R.drawable.ms_dark_mode
    "Delete" -> R.drawable.ms_delete
    "DeleteForever" -> R.drawable.ms_delete_forever
    "DeveloperMode" -> R.drawable.ms_developer_mode
    "Download" -> R.drawable.ms_download
    "ElectricalServices" -> R.drawable.ms_electrical_services
    "Engineering" -> R.drawable.ms_engineering
    "EnhancedEncryption" -> R.drawable.ms_enhanced_encryption
    "Extension" -> R.drawable.ms_extension
    "Fence" -> R.drawable.ms_fence
    "FormatListNumbered" -> R.drawable.ms_format_list_numbered
    "FormatSize" -> R.drawable.ms_format_size
    "Folder" -> R.drawable.ms_folder
    "FolderDelete" -> R.drawable.ms_folder_delete
    "FolderOff" -> R.drawable.ms_folder_off
    "FrontHand" -> R.drawable.ms_front_hand
    "GridView" -> R.drawable.ms_grid_view
    "Home" -> R.drawable.ms_home
    "Info" -> R.drawable.ms_info
    "Key" -> R.drawable.ms_key
    "KeyboardArrowDown" -> R.drawable.ms_keyboard_arrow_down
    "KeyboardArrowUp" -> R.drawable.ms_keyboard_arrow_up
    "Link" -> R.drawable.ms_link
    "LightMode" -> R.drawable.ms_light_mode
    "LocalPolice" -> R.drawable.ms_local_police
    "Lock" -> R.drawable.ms_lock
    "Memory" -> R.drawable.ms_memory
    "MoreVert" -> R.drawable.ms_more_vert
    "NavigateNext" -> R.drawable.ms_navigate_next
    "Opacity" -> R.drawable.ms_opacity
    "Palette" -> R.drawable.ms_palette
    "PhoneAndroid" -> R.drawable.ms_phone_android
    "PlayArrow" -> R.drawable.ms_play_arrow
    "PowerSettingsNew" -> R.drawable.ms_power_settings_new
    "RadioButtonChecked" -> R.drawable.ms_radio_button_checked
    "RadioButtonUnchecked" -> R.drawable.ms_radio_button_unchecked
    "Refresh" -> R.drawable.ms_refresh
    "RemoveCircle" -> R.drawable.ms_remove_circle
    "RemoveModerator" -> R.drawable.ms_remove_moderator
    "RestoreFromTrash" -> R.drawable.ms_restore_from_trash
    "Save" -> R.drawable.ms_save
    "Search" -> R.drawable.ms_search
    "SearchOff" -> R.drawable.ms_search_off
    "Security" -> R.drawable.ms_security
    "Settings" -> R.drawable.ms_settings
    "SettingsSuggest" -> R.drawable.ms_settings_suggest
    "Share" -> R.drawable.ms_share
    "Sync" -> R.drawable.ms_sync
    "TaskAlt" -> R.drawable.ms_task_alt
    "Terminal" -> R.drawable.ms_terminal
    "Translate" -> R.drawable.ms_translate
    "Undo" -> R.drawable.ms_undo
    "Update" -> R.drawable.ms_update
    "Visibility" -> R.drawable.ms_visibility
    "VisibilityOff" -> R.drawable.ms_visibility_off
    "Warning" -> R.drawable.ms_warning
    "Wallpaper" -> R.drawable.ms_wallpaper
    "WebAsset" -> R.drawable.ms_web_asset
    "Wysiwyg" -> R.drawable.ms_wysiwyg
    "ArrowDownward" -> R.drawable.ms_arrow_downward
    "ArrowDropDown" -> R.drawable.ms_arrow_drop_down
    "ArrowDropUp" -> R.drawable.ms_arrow_drop_up
    "ArrowForward" -> R.drawable.ms_arrow_forward
    "ArrowUpward" -> R.drawable.ms_arrow_upward
    "Article" -> R.drawable.ms_article
    "AutoFixHigh" -> R.drawable.ms_auto_fix_high
    "Bolt" -> R.drawable.ms_bolt
    "Cancel" -> R.drawable.ms_cancel
    "CheckCircle" -> R.drawable.ms_check_circle
    "Checklist" -> R.drawable.ms_checklist
    "Code" -> R.drawable.ms_code
    "ContentCopy" -> R.drawable.ms_content_copy
    "DeleteSweep" -> R.drawable.ms_delete_sweep
    "Description" -> R.drawable.ms_description
    "Done" -> R.drawable.ms_check
    "DoneAll" -> R.drawable.ms_done_all
    "Edit" -> R.drawable.ms_edit
    "ErrorOutline" -> R.drawable.ms_error
    "ExpandLess" -> R.drawable.ms_keyboard_arrow_up
    "ExpandMore" -> R.drawable.ms_keyboard_arrow_down
    "FavoriteBorder" -> R.drawable.ms_favorite
    "FilterList" -> R.drawable.ms_filter_list
    "GetApp" -> R.drawable.ms_download
    "Help" -> R.drawable.ms_help
    "HelpOutline" -> R.drawable.ms_help
    "History" -> R.drawable.ms_history
    "Image" -> R.drawable.ms_image
    "Inventory2" -> R.drawable.ms_inventory_2
    "Language" -> R.drawable.ms_language
    "Layers" -> R.drawable.ms_layers
    "LockOpen" -> R.drawable.ms_lock_open
    "OpenInNew" -> R.drawable.ms_open_in_new
    "Shield" -> R.drawable.ms_shield
    "Source" -> R.drawable.ms_source
    "Storage" -> R.drawable.ms_storage
    "Subject" -> R.drawable.ms_subject
    "SwapHoriz" -> R.drawable.ms_swap_horiz
    "SystemUpdate" -> R.drawable.ms_system_update
    "Tune" -> R.drawable.ms_tune
    "Upload" -> R.drawable.ms_upload
    "ViewList" -> R.drawable.ms_view_list
    else -> null
}
