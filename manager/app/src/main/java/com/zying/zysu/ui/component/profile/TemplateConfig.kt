package com.zying.zysu.ui.component.profile

import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.NavigateNext
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.KeyboardArrowDown
import androidx.compose.material.icons.filled.KeyboardArrowUp
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import com.zying.zysu.Natives
import com.zying.zysu.R
import com.zying.zysu.ui.component.YukiIcon
import com.zying.zysu.ui.component.clickHapticFeedback
import com.zying.zysu.ui.util.listAppProfileTemplates
import com.zying.zysu.ui.util.setSepolicy
import com.zying.zysu.ui.viewmodel.getTemplateInfoById

/**
 * @author weishu
 * @date 2023/10/21.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun TemplateConfig(
    profile: Natives.Profile,
    onViewTemplate: (id: String) -> Unit = {},
    onManageTemplate: () -> Unit = {},
    onProfileChange: (Natives.Profile) -> Unit
) {
    var expanded by remember { mutableStateOf(false) }
    var template by rememberSaveable {
        mutableStateOf(profile.rootTemplate ?: "")
    }
    val profileTemplates = listAppProfileTemplates()
    val noTemplates = profileTemplates.isEmpty()

    ListItem(content = {
        ExposedDropdownMenuBox(
            expanded = expanded,
            onExpandedChange = { expanded = it },
        ) {
            OutlinedTextField(
                modifier = Modifier
                    .menuAnchor(ExposedDropdownMenuAnchorType.PrimaryNotEditable)
                    .fillMaxWidth(),
                shape = MaterialTheme.shapes.large,
                colors = profileTextFieldColors(),
                readOnly = true,
                label = { Text(stringResource(R.string.profile_template)) },
                value = template.ifEmpty { "None" },
                onValueChange = {},
                trailingIcon = {
                    if (noTemplates) {
                        IconButton(
                            onClick = onManageTemplate
                        ) {
                            YukiIcon(Icons.Filled.Add, stringResource(R.string.app_profile_template_create))
                        }
                    } else if (expanded) YukiIcon(Icons.Filled.KeyboardArrowUp, null)
                    else YukiIcon(Icons.Filled.KeyboardArrowDown, null)
                },
            )
            if (profileTemplates.isEmpty()) {
                return@ExposedDropdownMenuBox
            }
            ExposedDropdownMenu(
                expanded = expanded,
                onDismissRequest = { expanded = false },
                modifier = Modifier.clickHapticFeedback(),
            ) {
                profileTemplates.forEach { tid ->
                    val templateInfo =
                        getTemplateInfoById(tid) ?: return@forEach
                    DropdownMenuItem(
                        text = { Text(tid) },
                        onClick = {
                            template = tid
                            if (setSepolicy(tid, templateInfo.rules.joinToString("\n"))) {
                                onProfileChange(
                                    profile.copy(
                                        rootTemplate = tid,
                                        rootUseDefault = false,
                                        uid = templateInfo.uid,
                                        gid = templateInfo.gid,
                                        groups = templateInfo.groups,
                                        capabilities = templateInfo.capabilities,
                                        context = templateInfo.context,
                                        namespace = templateInfo.namespace,
                                    )
                                )
                            }
                            expanded = false
                        },
                        trailingIcon = {
                            IconButton(onClick = {
                                onViewTemplate(tid)
                            }) {
                                YukiIcon(
                                    Icons.AutoMirrored.Filled.NavigateNext,
                                    stringResource(R.string.app_profile_template_view) + ": " + tid,
                                )
                            }
                        }
                    )
                }
            }
        }
    })
}
