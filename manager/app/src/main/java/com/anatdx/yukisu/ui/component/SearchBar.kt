package com.anatdx.yukisu.ui.component

import androidx.activity.compose.BackHandler
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.outlined.ArrowBack
import androidx.compose.material.icons.outlined.Close
import androidx.compose.material.icons.outlined.Search
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.platform.LocalFocusManager
import androidx.compose.ui.platform.LocalSoftwareKeyboardController
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.unit.dp
import com.anatdx.yukisu.R

/** Search is frequent: replace the title immediately, without staged expansion. */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SearchAppBar(
    title: @Composable () -> Unit,
    searchText: String,
    onSearchTextChange: (String) -> Unit,
    onClearClick: () -> Unit,
    placeholder: @Composable (() -> Unit)? = null,
    onBackClick: (() -> Unit)? = null,
    onConfirm: (() -> Unit)? = null,
    dropdownContent: @Composable (() -> Unit)? = null,
    scrollBehavior: TopAppBarScrollBehavior? = null,
    navigationActions: @Composable (() -> Unit)? = null,
) {
    var searching by rememberSaveable { mutableStateOf(searchText.isNotEmpty()) }
    val keyboard = LocalSoftwareKeyboardController.current
    val focusManager = LocalFocusManager.current
    val focusRequester = remember { FocusRequester() }
    fun closeSearch() {
        searching = false
        focusManager.clearFocus()
        keyboard?.hide()
        onClearClick()
    }
    BackHandler(searching) { closeSearch() }
    LaunchedEffect(searching) {
        if (searching) focusRequester.requestFocus()
    }
    DisposableEffect(Unit) { onDispose { keyboard?.hide() } }

    YukiTopAppBar(
        title = {
            if (searching) {
                OutlinedTextField(
                    value = searchText,
                    onValueChange = onSearchTextChange,
                    modifier = Modifier.fillMaxWidth().padding(vertical = 4.dp, horizontal = 4.dp)
                        .focusRequester(focusRequester),
                    placeholder = placeholder,
                    shape = MaterialTheme.shapes.medium,
                    textStyle = MaterialTheme.typography.bodyLarge,
                    singleLine = true,
                    trailingIcon = {
                        IconButton(onClick = ::closeSearch) {
                            YukiIcon(Icons.Outlined.Close, stringResource(R.string.log_viewer_clear_search))
                        }
                    },
                    keyboardOptions = KeyboardOptions.Default.copy(imeAction = ImeAction.Search),
                    keyboardActions = KeyboardActions(onSearch = {
                        keyboard?.hide()
                        onConfirm?.invoke()
                    }),
                    colors = OutlinedTextFieldDefaults.colors(
                        unfocusedContainerColor = MaterialTheme.colorScheme.surfaceContainerLowest,
                        focusedContainerColor = MaterialTheme.colorScheme.surfaceContainerLowest,
                        unfocusedBorderColor = MaterialTheme.colorScheme.outlineVariant,
                    ),
                )
            } else title()
        },
        navigationIcon = {
            if (onBackClick != null) {
                IconButton(onClick = if (searching) ::closeSearch else onBackClick) {
                    YukiIcon(Icons.AutoMirrored.Outlined.ArrowBack, stringResource(R.string.back))
                }
            }
        },
        actions = {
            if (!searching) {
                IconButton(onClick = { searching = true }) {
                    YukiIcon(Icons.Outlined.Search, stringResource(R.string.log_viewer_search))
                }
                navigationActions?.invoke()
            }
            dropdownContent?.invoke()
        },
        scrollBehavior = if (searching) null else scrollBehavior,
    )
}
