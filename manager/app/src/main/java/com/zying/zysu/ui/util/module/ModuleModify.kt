package com.zying.zysu.ui.util.module

import android.app.Activity
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.util.Log
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.ActivityResultLauncher
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.res.stringResource
import com.zying.zysu.ui.util.SnackbarController
import com.zying.zysu.R
import com.zying.zysu.ui.component.YukiAlertDialog
import com.zying.zysu.ui.component.LoadingDialogHandle
import com.zying.zysu.ui.component.rememberLoadingDialog
import com.zying.zysu.ui.viewmodel.SuperUserViewModel
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.IOException
import kotlinx.coroutines.CancellationException
import java.text.SimpleDateFormat
import java.util.*

object ModuleModify {
    @Composable
    fun AllowlistRestoreConfirmationDialog(
        showDialog: Boolean,
        onConfirm: () -> Unit,
        onDismiss: () -> Unit
    ) {
        if (showDialog) {
            YukiAlertDialog(
                onDismissRequest = onDismiss,
                title = {
                    Text(
                        text = stringResource(R.string.allowlist_restore_confirm_title)
                    )
                },
                text = {
                    Text(
                        text = stringResource(R.string.allowlist_restore_confirm_message) + "\n\n" +
                            stringResource(R.string.allowlist_json_restore_confirm),
                        style = MaterialTheme.typography.bodyMedium
                    )
                },
                confirmButton = {
                    TextButton(onClick = onConfirm) {
                        Text(stringResource(R.string.confirm))
                    }
                },
                dismissButton = {
                    TextButton(onClick = onDismiss) {
                        Text(stringResource(R.string.cancel))
                    }
                }
            )
        }
    }

    suspend fun backupAllowlist(
        context: Context,
        snackBarHost: SnackbarController,
        viewModel: SuperUserViewModel,
        uri: Uri
    ) {
        withContext(Dispatchers.IO) {
            try {
                val json = viewModel.exportAllowlist()
                context.contentResolver.openOutputStream(uri, "wt")?.use { output ->
                    output.write(json.toByteArray(Charsets.UTF_8))
                } ?: throw IOException("Failed to open output uri")

                withContext(Dispatchers.Main) {
                    snackBarHost.showSnackbar(
                        context.getString(R.string.allowlist_backup_success),
                        duration = SnackbarDuration.Long
                    )
                }

            } catch (e: Exception) {
                if (e is CancellationException) throw e
                Log.e("AllowlistBackup", context.getString(R.string.allowlist_backup_failed, ""), e)
                withContext(Dispatchers.Main) {
                    snackBarHost.showSnackbar(
                        context.getString(R.string.allowlist_backup_failed, e.message),
                        duration = SnackbarDuration.Long
                    )
                }
            }
        }
    }

    suspend fun restoreAllowlist(
        context: Context,
        snackBarHost: SnackbarController,
        viewModel: SuperUserViewModel,
        uri: Uri,
        showConfirmDialog: (Boolean) -> Unit,
        confirmResult: CompletableDeferred<Boolean>,
        loadingDialog: LoadingDialogHandle
    ) {
        val document = try {
            loadingDialog.withLoading {
                Log.i("AllowlistRestore", "Reading backup document")
                val parsed = withContext(Dispatchers.IO) {
                    context.contentResolver.openInputStream(uri)?.use(AllowlistBackup::read)
                        ?: throw IOException("Failed to open input uri")
                }
                Log.i("AllowlistRestore", "Validating ${parsed.apps.size} backup profiles")
                viewModel.validateAllowlist(parsed)
                parsed
            }
        } catch (e: Exception) {
            if (e is CancellationException) throw e
            Log.e("AllowlistRestore", "Backup validation failed", e)
            withContext(Dispatchers.Main) {
                snackBarHost.showSnackbar(
                    context.getString(R.string.allowlist_restore_failed, restoreError(context, e)),
                    duration = SnackbarDuration.Long
                )
            }
            return
        }

        withContext(Dispatchers.Main) {
            Log.i("AllowlistRestore", "Backup validated; waiting for confirmation")
            showConfirmDialog(true)
        }

        val userConfirmed = confirmResult.await()
        if (!userConfirmed) return

        try {
            loadingDialog.withLoading { viewModel.restoreAllowlist(document) }

            snackBarHost.showSnackbar(
                context.getString(R.string.allowlist_restore_success),
                duration = SnackbarDuration.Long
            )

        } catch (e: Exception) {
            if (e is CancellationException) throw e
            Log.e(
                "AllowlistRestore",
                context.getString(R.string.allowlist_restore_failed, ""),
                e
            )
            snackBarHost.showSnackbar(
                context.getString(R.string.allowlist_restore_failed, restoreError(context, e)),
                duration = SnackbarDuration.Long
            )
        }
    }

    private fun restoreError(context: Context, error: Exception): String = when (error) {
        is AllowlistRestore.Failure -> context.getString(
            if (error.rollbackFailed) R.string.allowlist_rollback_failed else R.string.allowlist_rollback_success
        )
        else -> error.message ?: error.javaClass.simpleName
    }

    @Composable
    fun rememberAllowlistBackupLauncher(
        context: Context,
        snackBarHost: SnackbarController,
        viewModel: SuperUserViewModel,
        scope: CoroutineScope = rememberCoroutineScope()
    ): ActivityResultLauncher<Intent> {
        val loadingDialog = rememberLoadingDialog()
        return rememberLauncherForActivityResult(
            contract = ActivityResultContracts.StartActivityForResult()
        ) { result ->
            if (result.resultCode == Activity.RESULT_OK) {
                result.data?.data?.let { uri ->
                    scope.launch {
                        loadingDialog.withLoading { backupAllowlist(context, snackBarHost, viewModel, uri) }
                    }
                }
            }
        }
    }

    @Composable
    fun rememberAllowlistRestoreLauncher(
        context: Context,
        snackBarHost: SnackbarController,
        viewModel: SuperUserViewModel,
        scope: CoroutineScope = rememberCoroutineScope()
    ): ActivityResultLauncher<Intent> {
        val loadingDialog = rememberLoadingDialog()
        var showAllowlistRestoreDialog by remember { mutableStateOf(false) }
        var allowlistRestoreConfirmResult by remember {
            mutableStateOf<CompletableDeferred<Boolean>?>(
                null
            )
        }

        AllowlistRestoreConfirmationDialog(
            showDialog = showAllowlistRestoreDialog,
            onConfirm = {
                showAllowlistRestoreDialog = false
                allowlistRestoreConfirmResult?.complete(true)
            },
            onDismiss = {
                showAllowlistRestoreDialog = false
                allowlistRestoreConfirmResult?.complete(false)
            }
        )

        return rememberLauncherForActivityResult(
            contract = ActivityResultContracts.StartActivityForResult()
        ) { result ->
            if (result.resultCode == Activity.RESULT_OK) {
                result.data?.data?.let { uri ->
                    scope.launch {
                        val confirmResult = CompletableDeferred<Boolean>()
                        allowlistRestoreConfirmResult = confirmResult

                        restoreAllowlist(
                            context = context,
                            snackBarHost = snackBarHost,
                            viewModel = viewModel,
                            uri = uri,
                            showConfirmDialog = { show -> showAllowlistRestoreDialog = show },
                            confirmResult = confirmResult,
                            loadingDialog = loadingDialog
                        )
                    }
                }
            }
        }
    }

    fun createAllowlistBackupIntent(): Intent {
        return Intent(Intent.ACTION_CREATE_DOCUMENT).apply {
            addCategory(Intent.CATEGORY_OPENABLE)
            type = "application/json"
            val timestamp = SimpleDateFormat("yyyyMMdd_HHmmss", Locale.getDefault()).format(Date())
            putExtra(Intent.EXTRA_TITLE, "zysu_allowlist_backup_$timestamp.json")
        }
    }

    fun createAllowlistRestoreIntent(): Intent {
        return Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
            addCategory(Intent.CATEGORY_OPENABLE)
            type = "application/json"
        }
    }
}
