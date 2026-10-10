package com.zying.zysu.ui.component.profile

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Edit
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.Alignment
import androidx.compose.ui.draw.clip
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.platform.LocalSoftwareKeyboardController
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp
import androidx.core.text.isDigitsOnly
import com.maxkeppeker.sheets.core.models.base.Header
import com.maxkeppeker.sheets.core.models.base.rememberUseCaseState
import com.maxkeppeler.sheets.input.InputDialog
import com.maxkeppeler.sheets.input.models.*
import com.maxkeppeler.sheets.list.ListDialog
import com.maxkeppeler.sheets.list.models.ListOption
import com.maxkeppeler.sheets.list.models.ListSelection
import com.zying.zysu.Natives
import com.zying.zysu.R
import com.zying.zysu.profile.Capabilities
import com.zying.zysu.profile.Groups
import com.zying.zysu.ui.component.rememberCustomDialog
import com.zying.zysu.ui.component.SwitchItem
import com.zying.zysu.ui.component.YukiIcon
import com.zying.zysu.ui.component.YukiPanel
import com.zying.zysu.ui.component.YukiDialogTheme
import com.zying.zysu.ui.component.performClickHapticFeedback
import com.zying.zysu.ui.util.isSepolicyValid
import com.zying.zysu.ui.theme.getCardBorder

@Composable
internal fun profileTextFieldColors() = OutlinedTextFieldDefaults.colors(
    unfocusedContainerColor = MaterialTheme.colorScheme.surfaceContainerHighest.copy(alpha = 0.35f),
    focusedContainerColor = MaterialTheme.colorScheme.surfaceContainerHigh,
    unfocusedBorderColor = MaterialTheme.colorScheme.outlineVariant,
)

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun RootProfileConfig(
    modifier: Modifier = Modifier,
    fixedName: Boolean,
    profile: Natives.Profile,
    onProfileChange: (Natives.Profile) -> Unit,
) {
    Column(modifier = modifier) {
        if (!fixedName) {
            OutlinedTextField(
                modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 8.dp),
                shape = MaterialTheme.shapes.large,
                colors = profileTextFieldColors(),
                label = { Text(stringResource(R.string.profile_name)) },
                value = profile.name,
                onValueChange = { onProfileChange(profile.copy(name = it)) }
            )
        }

        /* 
        var expanded by remember { mutableStateOf(false) }
        val currentNamespace = when (profile.namespace) {
            Natives.Profile.Namespace.INHERITED.ordinal -> stringResource(R.string.profile_namespace_inherited)
            Natives.Profile.Namespace.GLOBAL.ordinal -> stringResource(R.string.profile_namespace_global)
            Natives.Profile.Namespace.INDIVIDUAL.ordinal -> stringResource(R.string.profile_namespace_individual)
            else -> stringResource(R.string.profile_namespace_inherited)
        }
        ListItem(headlineContent = {
            ExposedDropdownMenuBox(
                expanded = expanded,
                onExpandedChange = { expanded = !expanded }
            ) {
                OutlinedTextField(
                    modifier = Modifier
                        .menuAnchor(MenuAnchorType.PrimaryNotEditable)
                        .fillMaxWidth(),
                    readOnly = true,
                    label = { Text(stringResource(R.string.profile_namespace)) },
                    value = currentNamespace,
                    onValueChange = {},
                    trailingIcon = {
                        if (expanded) Icon(Icons.Filled.ArrowDropUp, null)
                        else Icon(Icons.Filled.ArrowDropDown, null)
                    },
                )
                ExposedDropdownMenu(
                    expanded = expanded,
                    onDismissRequest = { expanded = false }
                ) {
                    DropdownMenuItem(
                        text = { Text(stringResource(R.string.profile_namespace_inherited)) },
                        onClick = {
                            onProfileChange(profile.copy(namespace = Natives.Profile.Namespace.INHERITED.ordinal))
                            expanded = false
                        },
                    )
                    DropdownMenuItem(
                        text = { Text(stringResource(R.string.profile_namespace_global)) },
                        onClick = {
                            onProfileChange(profile.copy(namespace = Natives.Profile.Namespace.GLOBAL.ordinal))
                            expanded = false
                        },
                    )
                    DropdownMenuItem(
                        text = { Text(stringResource(R.string.profile_namespace_individual)) },
                        onClick = {
                            onProfileChange(profile.copy(namespace = Natives.Profile.Namespace.INDIVIDUAL.ordinal))
                            expanded = false
                        },
                    )
                }
            }
        })
        */

        UidPanel(uid = profile.uid, label = "uid", onUidChange = {
            onProfileChange(
                profile.copy(
                    uid = it,
                    rootUseDefault = false
                )
            )
        })

        UidPanel(uid = profile.gid, label = "gid", onUidChange = {
            onProfileChange(
                profile.copy(
                    gid = it,
                    rootUseDefault = false
                )
            )
        })

        val selectedGroups = profile.groups.ifEmpty { listOf(0) }.let { e ->
            e.mapNotNull { g ->
                Groups.entries.find { it.gid == g }
            }
        }
        GroupsPanel(selectedGroups) {
            onProfileChange(
                profile.copy(
                    groups = it.map { group -> group.gid }.ifEmpty { listOf(0) },
                    rootUseDefault = false
                )
            )
        }

        val selectedCaps = profile.capabilities.mapNotNull { e ->
            Capabilities.entries.find { it.cap == e }
        }

        CapsPanel(selectedCaps) {
            onProfileChange(
                profile.copy(
                    capabilities = it.map { cap -> cap.cap },
                    rootUseDefault = false
                )
            )
        }

        SELinuxPanel(profile = profile, onSELinuxChange = { domain, rules ->
            onProfileChange(
                profile.copy(
                    context = domain,
                    rules = rules,
                    rootUseDefault = false
                )
            )
        })

        // When the profile still uses the default, seed the toggle from the
        // global "防逃逸" default; once customized, reflect the profile's flag.
        val noNewPrivs = if (profile.rootUseDefault) {
            Natives.isDefaultNoNewPrivsEnabled()
        } else {
            profile.flags and Natives.FLAG_KSU_NO_NEW_PRIVS != 0L
        }
        SwitchItem(
            title = stringResource(R.string.profile_no_new_privs),
            summary = stringResource(R.string.profile_no_new_privs_summary),
            checked = noNewPrivs,
            onCheckedChange = { checked ->
                val newFlags = if (checked) {
                    profile.flags or Natives.FLAG_KSU_NO_NEW_PRIVS
                } else {
                    profile.flags and Natives.FLAG_KSU_NO_NEW_PRIVS.inv()
                }
                onProfileChange(
                    profile.copy(flags = newFlags, rootUseDefault = false)
                )
            },
        )

    }
}

@OptIn(ExperimentalLayoutApi::class, ExperimentalMaterial3Api::class)
@Composable
fun GroupsPanel(selected: List<Groups>, closeSelection: (selection: Set<Groups>) -> Unit) {
    val selectGroupsDialog = rememberCustomDialog { dismiss: () -> Unit ->
        val feedbackView = LocalView.current
        val groups = Groups.entries.toTypedArray().sortedWith(
            compareBy<Groups> { if (selected.contains(it)) 0 else 1 }
                .then(compareBy {
                    when (it) {
                        Groups.ROOT -> 0
                        Groups.SYSTEM -> 1
                        Groups.SHELL -> 2
                        else -> Int.MAX_VALUE
                    }
                })
                .then(compareBy { it.name })

        )
        val options = groups.map { value ->
            ListOption(
                titleText = value.display,
                subtitleText = value.desc,
                selected = selected.contains(value),
            )
        }

        val selection = HashSet(selected)

        MaterialTheme(
            colorScheme = MaterialTheme.colorScheme.copy(
                surface = MaterialTheme.colorScheme.surfaceContainerHigh
            )
        ) {
            YukiDialogTheme {
                ListDialog(
                    state = rememberUseCaseState(visible = true, onFinishedRequest = {
                        feedbackView.performClickHapticFeedback()
                        closeSelection(selection)
                    }, onCloseRequest = {
                        dismiss()
                    }),
                    header = Header.Default(
                        title = stringResource(R.string.profile_groups),
                    ),
                    selection = ListSelection.Multiple(
                        showCheckBoxes = true,
                        options = options,
                        maxChoices = 32, // Kernel only supports 32 groups at most
                    ) { indecies, _ ->
                        feedbackView.performClickHapticFeedback()
                        // Handle selection
                        selection.clear()
                        indecies.forEach { index ->
                            val group = groups[index]
                            selection.add(group)
                        }
                    }
                )
            }
        }
    }

    ProfileSelectionPanel(
        title = stringResource(R.string.profile_groups),
        values = selected.map { it.display },
        onClick = { selectGroupsDialog.show() },
    )
}

@OptIn(ExperimentalLayoutApi::class, ExperimentalMaterial3Api::class)
@Composable
fun CapsPanel(
    selected: Collection<Capabilities>,
    closeSelection: (selection: Set<Capabilities>) -> Unit
) {
    val selectCapabilitiesDialog = rememberCustomDialog { dismiss ->
        val feedbackView = LocalView.current
        val caps = Capabilities.entries.toTypedArray().sortedWith(
            compareBy<Capabilities> { if (selected.contains(it)) 0 else 1 }
                .then(compareBy { it.name })
        )
        val options = caps.map { value ->
            ListOption(
                titleText = value.display,
                subtitleText = value.desc,
                selected = selected.contains(value),
            )
        }

        val selection = HashSet(selected)

        MaterialTheme(
            colorScheme = MaterialTheme.colorScheme.copy(
                surface = MaterialTheme.colorScheme.surfaceContainerHigh
            )
        ) {
            YukiDialogTheme {
                ListDialog(
                    state = rememberUseCaseState(visible = true, onFinishedRequest = {
                        feedbackView.performClickHapticFeedback()
                        closeSelection(selection)
                    }, onCloseRequest = {
                        dismiss()
                    }),
                    header = Header.Default(
                        title = stringResource(R.string.profile_capabilities),
                    ),
                    selection = ListSelection.Multiple(
                        showCheckBoxes = true,
                        options = options
                    ) { indecies, _ ->
                        feedbackView.performClickHapticFeedback()
                        // Handle selection
                        selection.clear()
                        indecies.forEach { index ->
                            val group = caps[index]
                            selection.add(group)
                        }
                    }
                )
            }
        }
    }

    ProfileSelectionPanel(
        title = stringResource(R.string.profile_capabilities),
        values = selected.map { it.display },
        onClick = { selectCapabilitiesDialog.show() },
    )
}

/** The panel is the action; its value pills never intercept taps or add empty actions. */
@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun ProfileSelectionPanel(title: String, values: List<String>, onClick: () -> Unit) {
    YukiPanel(
        modifier = Modifier.padding(horizontal = 16.dp, vertical = 8.dp)
            .clip(MaterialTheme.shapes.extraLarge)
            .clickable(role = Role.Button, onClickLabel = title, onClick = onClick),
    ) {
        Column(
            modifier = Modifier.padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(title, Modifier.weight(1f), style = MaterialTheme.typography.titleSmall)
                YukiIcon(Icons.Filled.Edit, null, tint = MaterialTheme.colorScheme.primary)
            }
            if (values.isNotEmpty()) {
                FlowRow(
                    horizontalArrangement = Arrangement.spacedBy(6.dp),
                    verticalArrangement = Arrangement.spacedBy(6.dp),
                ) {
                    values.forEach { value ->
                        Surface(
                            shape = RoundedCornerShape(percent = 50),
                            color = MaterialTheme.colorScheme.secondaryContainer.copy(alpha = 0.65f),
                            contentColor = MaterialTheme.colorScheme.onSecondaryContainer,
                        ) {
                            Text(
                                value,
                                modifier = Modifier.padding(horizontal = 12.dp, vertical = 6.dp),
                                style = MaterialTheme.typography.labelLarge,
                            )
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun UidPanel(uid: Int, label: String, onUidChange: (Int) -> Unit) {

    ListItem(content = {
        var isError by remember {
            mutableStateOf(false)
        }
        var lastValidUid by remember {
            mutableIntStateOf(uid)
        }
        val keyboardController = LocalSoftwareKeyboardController.current

        OutlinedTextField(
            modifier = Modifier.fillMaxWidth(),
            shape = MaterialTheme.shapes.large,
            colors = profileTextFieldColors(),
            label = { Text(label) },
            value = uid.toString(),
            isError = isError,
            keyboardOptions = KeyboardOptions(
                keyboardType = KeyboardType.Number,
                imeAction = ImeAction.Done
            ),
            keyboardActions = KeyboardActions(onDone = {
                keyboardController?.hide()
            }),
            onValueChange = {
                if (it.isEmpty()) {
                    onUidChange(0)
                    return@OutlinedTextField
                }
                val valid = isTextValidUid(it)

                val targetUid = if (valid) it.toInt() else lastValidUid
                if (valid) {
                    lastValidUid = it.toInt()
                }

                onUidChange(targetUid)

                isError = !valid
            }
        )
    })
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun SELinuxPanel(
    profile: Natives.Profile,
    onSELinuxChange: (domain: String, rules: String) -> Unit
) {
    val editSELinuxDialog = rememberCustomDialog { dismiss ->
        val feedbackView = LocalView.current
        var domain by remember { mutableStateOf(profile.context) }
        var rules by remember { mutableStateOf(profile.rules) }

        val inputOptions = listOf(
            InputTextField(
                text = domain,
                header = InputHeader(
                    title = stringResource(id = R.string.profile_selinux_domain),
                ),
                type = InputTextFieldType.OUTLINED,
                required = true,
                keyboardOptions = KeyboardOptions(
                    keyboardType = KeyboardType.Ascii,
                    imeAction = ImeAction.Next
                ),
                resultListener = {
                    domain = it ?: ""
                },
                validationListener = { value ->
                    // value can be a-zA-Z0-9_
                    val regex = Regex("^[a-z_]+:[a-z0-9_]+:[a-z0-9_]+(:[a-z0-9_]+)?$")
                    if (value?.matches(regex) == true) ValidationResult.Valid
                    else ValidationResult.Invalid("Domain must be in the format of \"user:role:type:level\"")
                }
            ),
            InputTextField(
                text = rules,
                header = InputHeader(
                    title = stringResource(id = R.string.profile_selinux_rules),
                ),
                type = InputTextFieldType.OUTLINED,
                keyboardOptions = KeyboardOptions(
                    keyboardType = KeyboardType.Ascii,
                ),
                singleLine = false,
                resultListener = {
                    rules = it ?: ""
                },
                validationListener = { value ->
                    if (isSepolicyValid(value)) ValidationResult.Valid
                    else ValidationResult.Invalid("SELinux rules is invalid!")
                }
            )
        )


        MaterialTheme(
            colorScheme = MaterialTheme.colorScheme.copy(
                surface = MaterialTheme.colorScheme.surfaceContainerHigh
            )
        ) {
            YukiDialogTheme {
                InputDialog(
                    state = rememberUseCaseState(
                        visible = true,
                        onFinishedRequest = {
                            feedbackView.performClickHapticFeedback()
                            onSELinuxChange(domain, rules)
                        },
                        onCloseRequest = {
                            dismiss()
                        }),
                    header = Header.Default(
                        title = stringResource(R.string.profile_selinux_context),
                    ),
                    selection = InputSelection(
                        input = inputOptions,
                        onPositiveClick = { _ ->
                            // Handled by the individual field result listeners.
                        },
                    )
                )
            }
        }
    }

    Surface(
        onClick = { editSELinuxDialog.show() },
        modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 8.dp),
        shape = MaterialTheme.shapes.large,
        color = MaterialTheme.colorScheme.surfaceContainerHighest.copy(alpha = 0.35f),
        border = getCardBorder(),
    ) {
        Row(
            modifier = Modifier.heightIn(min = 64.dp).padding(16.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(16.dp),
        ) {
            Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                Text(
                    stringResource(R.string.profile_selinux_context),
                    style = MaterialTheme.typography.labelMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
                Text(profile.context, style = MaterialTheme.typography.bodyLarge)
            }
            YukiIcon(Icons.Filled.Edit, null, tint = MaterialTheme.colorScheme.primary)
        }
    }
}

@Preview
@Composable
private fun RootProfileConfigPreview() {
    var profile by remember { mutableStateOf(Natives.Profile("")) }
    RootProfileConfig(fixedName = true, profile = profile) {
        profile = it
    }
}

private fun isTextValidUid(text: String): Boolean {
    return text.isNotEmpty() && text.isDigitsOnly() && text.toInt() >= 0
}
