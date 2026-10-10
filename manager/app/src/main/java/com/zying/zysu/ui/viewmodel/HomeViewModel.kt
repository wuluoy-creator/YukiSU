package com.zying.zysu.ui.viewmodel

import android.content.Context
import android.os.Build
import android.system.Os
import android.util.Log
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.zying.zysu.BuildConfig
import com.zying.zysu.KernelVersion
import com.zying.zysu.Natives
import com.zying.zysu.getKernelVersion
import com.zying.zysu.ksuApp
import com.zying.zysu.integrity.KsudIntegrity
import com.zying.zysu.integrity.KsudIntegrityStatus
import com.zying.zysu.ui.util.*
import com.zying.zysu.ui.util.module.LatestVersionInfo
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import java.io.File

class HomeViewModel : ViewModel() {

    data class KsudCardState(
        val bundledVersion: String? = null,
        val installedVersion: String? = null,
        val hasBundledDaemon: Boolean = false,
        val loading: Boolean = true,
        val syncing: Boolean = false,
        val syncResult: Boolean? = null,
    )

    var ksudCardState by mutableStateOf(KsudCardState())
        private set
    private var ksudRefreshJob: Job? = null

    private suspend fun readKsudCard(): KsudCardState = withContext(Dispatchers.IO) {
        val (bundled, installed) = KsuCli.getKsudVersionsForUi()
        KsudCardState(
            bundledVersion = bundled,
            installedVersion = installed,
            hasBundledDaemon = File(ksuApp.applicationInfo.nativeLibraryDir, "libksud.so").isFile,
            loading = false,
        )
    }

    fun refreshKsudCard() {
        if (ksudCardState.syncing) return
        ksudRefreshJob?.cancel()
        ksudRefreshJob = viewModelScope.launch {
            try {
                val refreshed = readKsudCard()
                ksudCardState = refreshed
            } catch (cancelled: CancellationException) {
                throw cancelled
            } catch (error: Exception) {
                Log.e("HomeViewModel", "Unable to read ksud card", error)
                ksudCardState = ksudCardState.copy(loading = false, installedVersion = null, syncResult = null)
            }
        }
    }

    fun syncKsudCard() {
        val status = systemStatus
        if (ksudCardState.loading || ksudCardState.syncing || !ksudCardState.hasBundledDaemon ||
            !status.isManager || status.kernelUapiVersion <= 0 ||
            status.kernelUapiVersion != status.managerUapiVersion
        ) return
        ksudRefreshJob?.cancel()
        ksudCardState = ksudCardState.copy(syncing = true, syncResult = null)
        // Retain progress across navigation/rotation; finish the atomic install and required
        // shell refresh + integrity readback even if this destination's ViewModel is cleared.
        viewModelScope.launch(start = CoroutineStart.UNDISPATCHED) {
            withContext(NonCancellable) {
                var updated = false
                var verified = false
                try {
                    updated = KsuCli.updateKsudDaemonForUi()
                } catch (cancelled: CancellationException) {
                    throw cancelled
                } catch (error: Exception) {
                    Log.e("HomeViewModel", "Unable to sync ksud", error)
                } finally {
                    try {
                        val integrityMatches = KsudIntegrity.refresh(ksuApp, notifyOnMismatch = false) == KsudIntegrityStatus.MATCH
                        val refreshed = readKsudCard()
                        ksudCardState = refreshed.copy(syncing = true)
                        verified = integrityMatches && refreshed.installedVersion != null
                    } catch (cancelled: CancellationException) {
                        throw cancelled
                    } catch (error: Exception) {
                        Log.e("HomeViewModel", "Unable to verify ksud after sync", error)
                    } finally {
                        ksudCardState = ksudCardState.copy(loading = false, syncing = false, syncResult = updated && verified)
                    }
                }
            }
        }
    }

    data class SystemStatus(
        val isManager: Boolean = false,
        val ksuVersion: Int? = null,
        val ksuFullVersion : String? = null,
        val kernelVersion: KernelVersion = getKernelVersion(),
        val isRootAvailable: Boolean = false,
        val isLkmBundled: Boolean = false,
        val kernelUapiVersion: Int = 0,
        val managerUapiVersion: Int = 0,
        val managerVersionCode: Long = BuildConfig.VERSION_CODE.toLong(),
    ) {
        val requireNewKernel: Boolean
            get() = isManager && kernelUapiVersion < managerUapiVersion
        val requireNewManager: Boolean
            get() = isManager && kernelUapiVersion > managerUapiVersion
        val showLkmUpdate: Boolean
            get() = isManager && isLkmBundled && ksuVersion != null &&
                ksuVersion.toLong() != managerVersionCode &&
                kernelUapiVersion == managerUapiVersion
        val showCustomLkmBadge: Boolean
            get() = ksuVersion != null && !isLkmBundled
    }

    data class SystemInfo(
        val kernelRelease: String = "",
        val androidVersion: String = "",
        val deviceModel: String = "",
        val managerVersion: Pair<String, Long> = Pair("", 0L),
        val seLinuxStatus: String = "",
        val superuserCount: Int = 0,
        val moduleCount: Int = 0,
        val zygiskImplement: String = "",
        val metaModuleImplement: String = "",
        // Manager process seccomp mode: -1 unsupported, 0 disabled, 1 strict,
        // 2 filter (mirrors upstream's prctl(PR_GET_SECCOMP)).
        val seccompStatus: Int = 2
    )

    var systemStatus by mutableStateOf(SystemStatus())
        private set

    var systemInfo by mutableStateOf(SystemInfo())
        private set

    var latestVersionInfo by mutableStateOf(LatestVersionInfo())
        private set

    var isSimpleMode by mutableStateOf(false)
        private set
    var isKernelSimpleMode by mutableStateOf(false)
        private set
    var isHideVersion by mutableStateOf(false)
        private set
    var isHideOtherInfo by mutableStateOf(false)
        private set
    var isHideZygiskImplement by mutableStateOf(false)
        private set
    var isHideSeccompStatus by mutableStateOf(false)
        private set
    var isHideMetaModuleImplement by mutableStateOf(false)
        private set
    var isHideLinkCard by mutableStateOf(false)
        private set


    var isCoreDataLoaded by mutableStateOf(false)
        private set
    var isExtendedDataLoaded by mutableStateOf(false)
        private set
    var isRefreshing by mutableStateOf(false)
        private set

    private val _dataRefreshTrigger = MutableStateFlow(0L)
    val dataRefreshTrigger: StateFlow<Long> = _dataRefreshTrigger

    private var loadingJobs = mutableListOf<Job>()
    private var lastRefreshTime = 0L
    private val refreshCooldown = 2000L

    fun loadUserSettings(context: Context) {
        viewModelScope.launch(Dispatchers.IO) {
            val settingsPrefs = context.getSharedPreferences("settings", Context.MODE_PRIVATE)
            isSimpleMode = settingsPrefs.getBoolean("is_simple_mode", false)
            isKernelSimpleMode = settingsPrefs.getBoolean("is_kernel_simple_mode", false)
            isHideVersion = settingsPrefs.getBoolean("is_hide_version", false)
            isHideOtherInfo = settingsPrefs.getBoolean("is_hide_other_info", false)
            isHideLinkCard = settingsPrefs.getBoolean("is_hide_link_card", false)
            isHideZygiskImplement = settingsPrefs.getBoolean("is_hide_zygisk_Implement", false)
            isHideSeccompStatus = settingsPrefs.getBoolean("is_hide_seccomp_status", false)
            isHideMetaModuleImplement = settingsPrefs.getBoolean("is_hide_meta_module_Implement", false)
        }
    }

    fun loadCoreData() {
        if (isCoreDataLoaded) return

        val job = viewModelScope.launch(Dispatchers.IO) {
            try {
                val kernelVersion = getKernelVersion()
                val isManager = try {
                    Natives.isManager
                } catch (_: Exception) {
                    false
                }

                val ksuVersion = if (isManager) Natives.version else null

                val fullVersion = try {
                    Natives.getFullVersion()
                } catch (_: Exception) {
                    "Unknown"
                }

                val ksuFullVersion = if (isKernelSimpleMode) {
                    try {
                        val startIndex = fullVersion.indexOf('v')
                        if (startIndex >= 0) {
                            val endIndex = fullVersion.indexOf('-', startIndex)
                            val versionStr = if (endIndex > startIndex) {
                                fullVersion.substring(startIndex, endIndex)
                            } else {
                                fullVersion.substring(startIndex)
                            }
                            val numericVersion = "v" + (Regex("""\d+(\.\d+)*""").find(versionStr)?.value ?: versionStr)
                            numericVersion
                        } else {
                            fullVersion
                        }
                    } catch (_: Exception) {
                        fullVersion
                    }
                } else {
                    fullVersion
                }

                val isRootAvailable = try {
                    rootAvailable()
                } catch (_: Exception) {
                    false
                }

                val isLkmBundled = isManager && runCatching { Natives.isLkmBundled }.getOrDefault(false)

                val kernelUapiVersion = if (isManager) {
                    try { Natives.getUapiVersion() } catch (_: Exception) { 0 }
                } else 0

                val managerUapiVersion = try {
                    Natives.getManagerUapiVersion()
                } catch (_: Exception) { 0 }

                systemStatus = SystemStatus(
                    isManager = isManager,
                    ksuVersion = ksuVersion,
                    ksuFullVersion = ksuFullVersion,
                    kernelVersion = kernelVersion,
                    isRootAvailable = isRootAvailable,
                    isLkmBundled = isLkmBundled,
                    kernelUapiVersion = kernelUapiVersion,
                    managerUapiVersion = managerUapiVersion
                )

                isCoreDataLoaded = true
            } catch (_: Exception) {
            }
        }
        loadingJobs.add(job)
    }

    fun loadExtendedData(context: Context) {
        if (isExtendedDataLoaded) return

        val job = viewModelScope.launch(Dispatchers.IO) {
            try {
                delay(50)

                val basicInfo = loadBasicSystemInfo(context)
                systemInfo = systemInfo.copy(
                    kernelRelease = basicInfo.first,
                    androidVersion = basicInfo.second,
                    deviceModel = basicInfo.third,
                    managerVersion = basicInfo.fourth,
                    seLinuxStatus = basicInfo.fifth,
                    seccompStatus = readSeccompStatus()
                )

                delay(100)

                if (!isSimpleMode) {
                    val moduleInfo = loadModuleInfo()
                    systemInfo = systemInfo.copy(
                        superuserCount = moduleInfo.first,
                        moduleCount = moduleInfo.second,
                        zygiskImplement = moduleInfo.third,
                        metaModuleImplement = moduleInfo.fourth
                    )
                }

                delay(100)

                isExtendedDataLoaded = true
            } catch (_: Exception) {
            }
        }
        loadingJobs.add(job)
    }

    fun refreshData(context: Context, forceRefresh: Boolean = false) {
        val currentTime = System.currentTimeMillis()

        if (!forceRefresh && currentTime - lastRefreshTime < refreshCooldown) {
            return
        }

        lastRefreshTime = currentTime

        viewModelScope.launch {
            isRefreshing = true

            try {
                loadingJobs.forEach { it.cancel() }
                loadingJobs.clear()

                isCoreDataLoaded = false
                isExtendedDataLoaded = false

                _dataRefreshTrigger.value = currentTime

                loadUserSettings(context)

                loadCoreData()
                delay(100)

                loadExtendedData(context)

                val settingsPrefs = context.getSharedPreferences("settings", Context.MODE_PRIVATE)
                val checkUpdate = settingsPrefs.getBoolean("check_update", true)
                if (checkUpdate) {
                    try {
                        val newVersionInfo = withContext(Dispatchers.IO) {
                            checkNewVersion()
                        }
                        latestVersionInfo = newVersionInfo
                    } catch (_: Exception) {
                    }
                }
            } catch (_: Exception) {
            } finally {
                isRefreshing = false
            }
        }
    }

    fun onPullRefresh(context: Context) {
        refreshData(context, forceRefresh = true)
    }

    fun autoRefreshIfNeeded(context: Context) {
        viewModelScope.launch {
            val needsRefresh = checkIfDataNeedsRefresh()
            if (needsRefresh) {
                refreshData(context)
            }
        }
    }

    private suspend fun checkIfDataNeedsRefresh(): Boolean {
        return withContext(Dispatchers.IO) {
            try {
                val currentKsuVersion = try {
                    if (Natives.isManager) {
                        Natives.version
                    } else null
                } catch (_: Exception) {
                    null
                }

                if (currentKsuVersion != systemStatus.ksuVersion) {
                    return@withContext true
                }

                val currentModuleCount = try {
                    getModuleCount()
                } catch (_: Exception) {
                    systemInfo.moduleCount
                }

                if (currentModuleCount != systemInfo.moduleCount) {
                    return@withContext true
                }

                false
            } catch (_: Exception) {
                false
            }
        }
    }

    // Reads the manager process seccomp mode from /proc/self/status.
    // -1 = unsupported (no Seccomp line), 0 = disabled, 1 = strict, 2 = filter.
    private fun readSeccompStatus(): Int = runCatching {
        java.io.File("/proc/self/status").useLines { lines ->
            lines.firstOrNull { it.startsWith("Seccomp:") }
                ?.substringAfter(':')?.trim()?.toIntOrNull() ?: -1
        }
    }.getOrDefault(-1)

    private suspend fun loadBasicSystemInfo(
        context: Context
    ): Tuple5<String, String, String, Pair<String, Long>, String> {
        return withContext(Dispatchers.IO) {
            val uname = try {
                Os.uname()
            } catch (_: Exception) {
                null
            }

            val deviceModel = try {
                resolveDeviceName()
            } catch (_: Exception) {
                "Unknown"
            }

            val managerVersion = try {
                getManagerVersion(context)
            } catch (_: Exception) {
                Pair("Unknown", 0L)
            }

            val seLinuxStatus = try {
                getSELinuxStatus(ksuApp.applicationContext)
            } catch (_: Exception) {
                "Unknown"
            }

            Tuple5(
                uname?.release ?: "Unknown",
                Build.VERSION.RELEASE ?: "Unknown",
                deviceModel,
                managerVersion,
                seLinuxStatus
            )
        }
    }

    private suspend fun loadModuleInfo(): Tuple4<Int, Int, String, String> {
        return withContext(Dispatchers.IO) {
            val superuserCount = try {
                getSuperuserCount()
            } catch (_: Exception) {
                0
            }

            val moduleCount = try {
                getModuleCount()
            } catch (_: Exception) {
                0
            }

            val zygiskImplement = try {
                getZygiskImplement()
            } catch (_: Exception) {
                "None"
            }

            val metaModuleImplement = try {
                getMetaModuleImplement()
            } catch (_: Exception) {
                "None"
            }

            Tuple4(superuserCount, moduleCount, zygiskImplement, metaModuleImplement)
        }
    }

    private fun getManagerVersion(context: Context): Pair<String, Long> {
        return try {
            val packageInfo = context.packageManager.getPackageInfo(context.packageName, 0)
            val versionCode = androidx.core.content.pm.PackageInfoCompat.getLongVersionCode(packageInfo)
            val versionName = packageInfo.versionName ?: "Unknown"
            Pair(versionName, versionCode)
        } catch (_: Exception) {
            Pair("Unknown", 0L)
        }
    }

    data class Tuple5<T1, T2, T3, T4, T5>(
        val first: T1,
        val second: T2,
        val third: T3,
        val fourth: T4,
        val fifth: T5
    )

    data class Tuple4<T1, T2, T3, T4>(
        val first: T1,
        val second: T2,
        val third: T3,
        val fourth: T4
    )

    override fun onCleared() {
        loadingJobs.forEach { it.cancel() }
        loadingJobs.clear()
    }
}
