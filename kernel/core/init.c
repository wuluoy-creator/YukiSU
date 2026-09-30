/*
 * KernelSU Main Entry Point (LKM only)
 *
 * YukiSU supports only loadable kernel module (CONFIG_KSU=m).
 */

#include <linux/export.h>
#include <linux/fs.h>
#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/rcupdate.h>
#include <linux/sched.h>
#include <linux/version.h>

#include "policy/allowlist.h"
#include "policy/feature.h"
#include "core/imgpatch_config.h"
#include "feature/adb_root.h"
#include "feature/hide_bootloader.h"
#include "feature/selinux_hide.h"
#ifdef CONFIG_KSU_YUKIZYGISK
#include "feature/yukizygisk/api.h"
#endif // #ifdef CONFIG_KSU_YUKIZYGISK
#include "kasumi_bootstrap.h"
#include "hook/lsm_hook.h"
#include "infra/symbol_resolver.h"
#include "klog.h" // IWYU pragma: keep
#include "ksu.h"
#include "runtime/ksud_boot.h"
#include "runtime/ksud.h"
#include "manager/manager_observer.h"
#include "selinux/selinux.h"
#include "supercall/supercall.h"
#ifdef CONFIG_KSU_SUPERKEY
#include "manager/superkey.h"
#endif // #ifdef CONFIG_KSU_SUPERKEY
#ifndef CONFIG_KSU_DISABLE_MANAGER
#include "manager/dynamic_manager.h"
#endif // #ifndef CONFIG_KSU_DISABLE_MANAGER
#include "manager/throne_tracker.h"
#include "feature/sulog.h"

struct cred *ksu_cred;
bool ksu_late_loaded;
bool ksu_imgpatch_loaded;
u32 ksu_boot_load_mode __read_mostly = KSU_LOAD_MODE_RAMDISK;
module_param_named(imgpatch, ksu_imgpatch_loaded, bool, 0);

#ifdef CONFIG_KSU_DEBUG
bool allow_shell = true;
#else
bool allow_shell = false;
#endif // #ifdef CONFIG_KSU_DEBUG
module_param(allow_shell, bool, 0);

bool ksu_no_custom_rc = false;
module_param_named(norc, ksu_no_custom_rc, bool, 0);

bool ksu_bundled;
module_param_named(bundled, ksu_bundled, bool, 0);

static void yukisu_custom_config_init(void)
{
}

static void yukisu_custom_config_exit(void)
{
#if __SULOG_GATE
	ksu_sulog_exit();
#endif // #if __SULOG_GATE
}

#include "hook/syscall_hook.h"
#include "hook/syscall_hook_manager.h"

static bool ksu_hooks_started;

static void ksu_hook_init(void)
{
	int ret = ksu_syscall_hook_init();

	ksu_hooks_started = false;
	if (ret) {
		pr_err("ksu: syscall_hook_init failed: %d\n", ret);
		return;
	}
	ksu_syscall_hook_manager_init();
	ksu_hooks_started = true;
}

static void ksu_hook_exit(void)
{
	if (!ksu_hooks_started)
		return;

	ksu_syscall_hook_manager_exit();
	ksu_hooks_started = false;
}

static int __init kernelsu_init(void)
{
	int ret;

	pr_info("KernelSU LKM initializing, version: %u\n", KSU_VERSION);
	ksu_late_loaded = (current->pid != 1);
	ksu_imgpatch_loaded = ksu_imgpatch_loaded ||
			      ksu_boot_load_mode == KSU_LOAD_MODE_IMAGE_PATCH;
	/*
	 * The persistent load-mode marker is written together with the embedded
	 * config. A ramdisk module parameter named imgpatch must not make an
	 * otherwise normal LKM override the ramdisk configuration.
	 */
	if (ksu_boot_load_mode == KSU_LOAD_MODE_IMAGE_PATCH &&
	    !ksu_late_loaded) {
		ret = ksu_imgpatch_config_apply();
		if (ret)
			return ret;
	}
#ifdef CONFIG_KSU_DEBUG
	pr_alert(
	    "*************************************************************");
	pr_alert("**\t NOTICE NOTICE NOTICE NOTICE NOTICE NOTICE NOTICE\t**");
	pr_alert("**\t\t\t\t\t\t\t"
		 "\t\t\t **");
	pr_alert("**\t\t You are running KernelSU in DEBUG mode\t\t  **");
	pr_alert("**\t\t\t\t\t\t\t"
		 "\t\t\t **");
	pr_alert("**\t NOTICE NOTICE NOTICE NOTICE NOTICE NOTICE NOTICE\t**");
	pr_alert(
	    "*************************************************************");
#endif // #ifdef CONFIG_KSU_DEBUG

	if (allow_shell) {
		pr_alert("shell is allowed at init!");
	}

	ksu_init_symbol_resolver();

	ksu_cred = prepare_creds();
	if (!ksu_cred) {
		pr_err("prepare cred failed!\n");
		return -ENOMEM;
	}

	ksu_feature_init();
	ksu_hide_bootloader_init();
	ksu_lsm_hook_init();
	ksu_adb_root_init();
	ksu_selinux_hide_init();
#ifdef CONFIG_KSU_YUKIZYGISK
	ksu_yukizygisk_init();
#endif // #ifdef CONFIG_KSU_YUKIZYGISK

	ksu_supercalls_init();
	ksu_app_profile_init();
#ifndef CONFIG_KSU_DISABLE_MANAGER
	ksu_dynamic_manager_init();
#endif // #ifndef CONFIG_KSU_DISABLE_MANAGER

#ifdef CONFIG_KSU_SUPERKEY
	superkey_init();
#endif // #ifdef CONFIG_KSU_SUPERKEY

	yukisu_custom_config_init();

#if __SULOG_GATE
	ksu_sulog_init();
#endif // #if __SULOG_GATE

	if (ksu_late_loaded) {
		pr_info("late load mode, skipping kprobe hooks\n");

		apply_kernelsu_rules();
		cache_sid();
		setup_ksu_cred();

		/*
		 * Late-load ksud must keep root and the YukiSU SELinux domain
		 * before SELinux is switched back to enforcing.
		 */
		escape_to_root_for_init();

		if (!getenforce()) {
			pr_info("Permissive SELinux, enforcing\n");
			setenforce(true);
		}

		ksu_allowlist_init();
		ksu_load_allow_list();

	} else {
		ksu_allowlist_init();
	}

	ksu_kasumi_init();
	ksu_hook_init();

	if (ksu_late_loaded) {
		ksu_throne_tracker_init();
		ksu_observer_init();

		ksu_boot_completed = true;
		track_throne(false);
	} else {
		ksu_throne_tracker_init();
		ksu_ksud_init();
	}

#ifndef CONFIG_KSU_DEBUG
	kobject_del(&THIS_MODULE->mkobj.kobj);
#endif // #ifndef CONFIG_KSU_DEBUG

	pr_info("KernelSU LKM initialized\n");
	return 0;
}

static void kernelsu_exit(void)
{
	// Phase 1: Stop hooks first to prevent new callbacks
	ksu_hook_exit();
	ksu_kasumi_exit();
	ksu_supercalls_exit();

	if (!ksu_late_loaded)
		ksu_ksud_exit();

	// Wait for any in-flight RCU readers (e.g. handlers traversing
	// allow_list) before releasing the data structures they access.
	synchronize_rcu();

	// Phase 2: Now safe to release data structures
	ksu_observer_exit();
	ksu_throne_tracker_exit();
#ifndef CONFIG_KSU_DISABLE_MANAGER
	ksu_dynamic_manager_exit();
#endif // #ifndef CONFIG_KSU_DISABLE_MANAGER
	ksu_allowlist_exit();

	yukisu_custom_config_exit();
	ksu_selinux_hide_exit();
	ksu_hide_bootloader_exit();
#ifdef CONFIG_KSU_YUKIZYGISK
	ksu_yukizygisk_exit();
#endif // #ifdef CONFIG_KSU_YUKIZYGISK
	ksu_adb_root_exit();
	ksu_lsm_hook_exit();
	ksu_feature_exit();

	if (ksu_cred) {
		put_cred(ksu_cred);
		ksu_cred = NULL;
	}
}

module_init(kernelsu_init);
module_exit(kernelsu_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("weishu");
MODULE_DESCRIPTION("Android KernelSU");
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 13, 0)
MODULE_IMPORT_NS("VFS_internal_I_am_really_a_filesystem_and_am_NOT_a_driver");
#else
MODULE_IMPORT_NS(VFS_internal_I_am_really_a_filesystem_and_am_NOT_a_driver);
#endif // #if LINUX_VERSION_CODE >= KERNEL_VERSIO...
