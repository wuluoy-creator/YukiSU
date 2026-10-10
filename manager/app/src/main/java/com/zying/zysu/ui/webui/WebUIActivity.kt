package com.zying.zysu.ui.webui

import com.zying.zysu.ui.theme.ThemeManager
import android.os.Bundle
import android.util.Log
import android.view.WindowManager
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.SideEffect
import androidx.compose.runtime.remember
import com.zying.zysu.R
import com.zying.zysu.ui.theme.KernelSUTheme
import kotlinx.coroutines.CancellationException

class WebUIActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        enableEdgeToEdge()
        window.isNavigationBarContrastEnforced = false
        window.setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_ADJUST_NOTHING)
        super.onCreate(savedInstanceState)

        val moduleId = intent.getStringExtra("id") ?: run {
            finishAndRemoveTask()
            return
        }

        ThemeManager.loadThemeMode(this)
        ThemeManager.loadThemeColors(this)
        ThemeManager.loadDynamicColorState(this)

        setContent {
            KernelSUTheme {
                val state = remember { WebUIState() }
                val colorScheme = MaterialTheme.colorScheme
                val colorsCss = remember(colorScheme) { MonetColorsProvider.getColorsCss(colorScheme) }
                SideEffect { state.colorsCss = colorsCss }

                LaunchedEffect(moduleId) {
                    try {
                        prepareWebView(this@WebUIActivity, moduleId, state)
                    } catch (e: CancellationException) {
                        throw e
                    } catch (e: Exception) {
                        Log.e("WebUIActivity", "Failed to prepare module WebUI", e)
                        state.uiEvent = WebUIEvent.Error(getString(R.string.operation_failed))
                    }
                }

                DisposableEffect(state) {
                    onDispose { state.dispose(this@WebUIActivity) }
                }

                when (val event = state.uiEvent) {
                    is WebUIEvent.Loading -> WebUILoadingScreen(onBack = ::finishAndRemoveTask)
                    is WebUIEvent.Error -> LaunchedEffect(event) {
                        Toast.makeText(this@WebUIActivity, event.message, Toast.LENGTH_SHORT).show()
                        finishAndRemoveTask()
                    }
                    is WebUIEvent.Close -> LaunchedEffect(event) { finishAndRemoveTask() }
                    else -> WebUIScreen(state)
                }
            }
        }
    }
}
