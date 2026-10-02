#include <linux/binfmts.h>
#include <linux/capability.h>
#include <linux/compiler.h>
#include <linux/cred.h>
#include <linux/err.h>
#include <linux/fcntl.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/personality.h>
#include <linux/sched.h>
#include <linux/signal.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/version.h>

#include "feature/sucompat_exec.h"
#include "feature/sucompat_vfs.h"
#include "hook/lsm_hook.h"
#include "infra/symbol_resolver.h"
#include "klog.h" // IWYU pragma: keep
#include "ksu.h"
#include "objsec.h"
#include "policy/allowlist.h"
#include "policy/app_profile.h"
#include "runtime/ksud.h"
#include "sulog/event.h"
#include "supercall/supercall.h"

#define KSU_SU_EXEC_CTX_MAGIC 0x4b53555355455845ULL
#define KSU_SU_CREDS_HOOK_TARGET "selinux_bprm_creds_for_exec"
#define KSU_SU_CAPS_HOOK_TARGET "cap_bprm_creds_from_file"
#define KSU_SU_COMMIT_HOOK_TARGET "selinux_bprm_committing_creds"
#define KSU_SU_ARG0 "su"

enum ksu_su_exec_stage {
	KSU_SU_EXEC_OPEN,
	KSU_SU_EXEC_PROFILE_READY,
	KSU_SU_EXEC_CAPS_READY,
	KSU_SU_EXEC_COMMITTED,
};

struct ksu_su_exec_ctx {
	u64 magic;
	unsigned long su_ino;
	uid_t uid;
	enum ksu_su_exec_stage stage;
	struct ksu_root_profile_state profile;
	struct ksu_sulog_pending_event *sulog;
};

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
typedef const struct linux_binprm ksu_su_commit_bprm_t;
typedef const struct file ksu_su_caps_file_t;
#else
typedef struct linux_binprm ksu_su_commit_bprm_t;
typedef struct file ksu_su_caps_file_t;
#endif

typedef int (*ksu_bprm_creds_for_exec_fn)(struct linux_binprm *bprm);
typedef int (*ksu_bprm_creds_from_file_fn)(struct linux_binprm *bprm,
					   ksu_su_caps_file_t *file);
typedef void (*ksu_bprm_committing_creds_fn)(ksu_su_commit_bprm_t *bprm);

static int ksu_su_bprm_creds_for_exec(struct linux_binprm *bprm);
static int ksu_su_bprm_creds_from_file(struct linux_binprm *bprm,
				       ksu_su_caps_file_t *file);
static void ksu_su_bprm_committing_creds(ksu_su_commit_bprm_t *bprm);

static struct ksu_lsm_hook ksu_su_creds_hook =
    KSU_LSM_HOOK_INIT(bprm_creds_for_exec, KSU_SU_CREDS_HOOK_TARGET,
		      ksu_su_bprm_creds_for_exec, 0);
static struct ksu_lsm_hook ksu_su_caps_hook =
    KSU_LSM_HOOK_INIT(bprm_creds_from_file, KSU_SU_CAPS_HOOK_TARGET,
		      ksu_su_bprm_creds_from_file, 0);
static struct ksu_lsm_hook ksu_su_commit_hook =
    KSU_LSM_HOOK_INIT(bprm_committing_creds, KSU_SU_COMMIT_HOOK_TARGET,
		      ksu_su_bprm_committing_creds, 0);
static bool ksu_su_exec_hooks_ready;
static typeof(&ptracer_capable) ksu_su_ptracer_capable;

static noinline int ksu_su_exec_check_unsafe(const struct linux_binprm *bprm)
{
	if (uid_eq(current_euid(), GLOBAL_ROOT_UID))
		return 0;
	if (bprm->unsafe & LSM_UNSAFE_SHARE)
		return -EPERM;
	if ((bprm->unsafe & LSM_UNSAFE_PTRACE) &&
	    (!ksu_su_ptracer_capable ||
	     !ksu_su_ptracer_capable(current, bprm->cred->user_ns)))
		return -EPERM;
	/* Android apps inherit NNP; KSU's explicit escape-disable flag is
	 * checked by root-profile preparation instead.
	 */
	return 0;
}

static void ksu_su_allow_write_access(struct file *file)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 18, 0)
	exe_file_allow_write_access(file);
#else
	allow_write_access(file);
#endif
}

static void ksu_su_close_exec_file(struct file *file)
{
	if (!file)
		return;
	ksu_su_allow_write_access(file);
	fput(file);
}

static struct ksu_su_exec_ctx *ksu_su_exec_ctx(const struct file *file)
{
	struct ksu_su_exec_ctx *ctx;

	if (!ksu_sucompat_vfs_is_file(file))
		return NULL;
	ctx = file->private_data;
	if (!ctx || ctx->magic != KSU_SU_EXEC_CTX_MAGIC)
		return NULL;
	return ctx;
}

static struct ksu_su_exec_ctx *
ksu_su_exec_bprm_ctx(const struct linux_binprm *bprm)
{
	if (!bprm || !bprm->executable)
		return NULL;
	return ksu_su_exec_ctx(bprm->executable);
}

static void ksu_su_exec_emit_log(struct ksu_su_exec_ctx *ctx, int retval)
{
	struct ksu_sulog_pending_event *pending;

	if (!ctx)
		return;
	pending = ctx->sulog;
	ctx->sulog = NULL;
	ksu_sulog_emit_pending(pending, retval, GFP_ATOMIC);
}

static bool ksu_su_exec_grant_valid(const struct ksu_su_exec_ctx *ctx)
{
	if (!ctx || current_uid().val != ctx->uid ||
	    !ksu_sucompat_vfs_current_ino(ctx->su_ino))
		return false;
	return ksu_is_allow_uid_for_current(ctx->uid);
}

static int ksu_su_exec_authorize(struct ksu_su_exec_ctx *ctx)
{
	if (!ctx || !ksu_sucompat_vfs_current_ino(ctx->su_ino))
		return -EACCES;
	ctx->uid = current_uid().val;
	return ksu_su_exec_grant_valid(ctx) ? 0 : -EACCES;
}

static int ksu_su_exec_set_arg0(struct linux_binprm *bprm)
{
	int ret;

	ret = remove_arg_zero(bprm);
	if (ret)
		return ret;
	ret = copy_string_kernel(KSU_SU_ARG0, bprm);
	if (ret)
		return ret;
	bprm->argc++;
	return 0;
}

static int __nocfi ksu_su_bprm_creds_for_exec(struct linux_binprm *bprm)
{
	ksu_bprm_creds_for_exec_fn original =
	    (ksu_bprm_creds_for_exec_fn)READ_ONCE(ksu_su_creds_hook.original);
	struct ksu_su_exec_ctx *ctx;
	const struct cred *old_cred;
	struct cred *override_cred;
	struct file *marker;
	struct file *ksud_file;
	bool is_check = false;
	int ret;

	if (!bprm || !bprm->file || !ksu_sucompat_vfs_is_file(bprm->file))
		return original(bprm);
	if (!READ_ONCE(ksu_su_exec_hooks_ready) || bprm->executable ||
	    bprm->have_execfd || bprm->execfd_creds)
		return -EACCES;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 18, 0)
	is_check = bprm->is_check;
#endif
	marker = bprm->file;
	ctx = ksu_su_exec_ctx(marker);
	if (!ctx || ctx->stage != KSU_SU_EXEC_OPEN)
		return -EACCES;
	if (!is_check)
		ctx->sulog = ksu_sulog_capture_sucompat_bprm(bprm, GFP_KERNEL);
	ret = ksu_su_exec_check_unsafe(bprm);
	if (ret)
		goto out_log;

	ret = ksu_su_exec_authorize(ctx);
	if (ret)
		goto out_log;
	if (unlikely(!ksu_cred)) {
		ret = -EIO;
		goto out_log;
	}
	old_cred = override_creds(ksu_cred);
	ksud_file = open_exec(KSUD_PATH);
	revert_creds(old_cred);
	if (IS_ERR(ksud_file)) {
		ret = PTR_ERR(ksud_file);
		goto out_log;
	}

	override_cred = prepare_creds();
	if (!override_cred) {
		ksu_su_close_exec_file(ksud_file);
		ret = -ENOMEM;
		goto out_log;
	}
	ret = ksu_prepare_root_profile_cred_strict(override_cred, ctx->uid,
						   &ctx->profile);
	if (ret) {
		abort_creds(override_cred);
		ksu_su_close_exec_file(ksud_file);
		goto out_log;
	}
	if (!ksu_su_exec_grant_valid(ctx)) {
		abort_creds(override_cred);
		ksu_su_close_exec_file(ksud_file);
		ret = -EACCES;
		goto out_log;
	}

	bprm->executable = marker;
	bprm->file = ksud_file;
	ksu_su_allow_write_access(marker);

	old_cred = override_creds(override_cred);
	ret = original(bprm);
	revert_creds(old_cred);
	abort_creds(override_cred);
	if (ret)
		goto out_log;
	/* The temporary cred must not hide the real domain transition. */
	selinux_cred(bprm->cred)->osid = selinux_cred(current_cred())->sid;
	if (!ksu_su_exec_grant_valid(ctx)) {
		ret = -EACCES;
		goto out_log;
	}
	if (!is_check) {
		ret = ksu_su_exec_set_arg0(bprm);
		if (ret)
			goto out_log;
	}
	ctx->stage = KSU_SU_EXEC_PROFILE_READY;
	return 0;

out_log:
	ksu_su_exec_emit_log(ctx, ret);
	return ret;
}

static int __nocfi ksu_su_bprm_creds_from_file(struct linux_binprm *bprm,
					       ksu_su_caps_file_t *file)
{
	ksu_bprm_creds_from_file_fn original =
	    (ksu_bprm_creds_from_file_fn)READ_ONCE(ksu_su_caps_hook.original);
	struct ksu_su_exec_ctx *ctx = ksu_su_exec_bprm_ctx(bprm);
	int ret;

	if (!ctx)
		return original(bprm, file);
	if (ctx->stage != KSU_SU_EXEC_PROFILE_READY ||
	    !ksu_su_exec_grant_valid(ctx)) {
		ksu_su_exec_emit_log(ctx, -EACCES);
		return -EACCES;
	}
	ret = original(bprm, file);
	if (ret) {
		ksu_su_exec_emit_log(ctx, ret);
		return ret;
	}
	ret = ksu_apply_root_profile_state_cred_preserve_security(
	    bprm->cred, ctx->uid, &ctx->profile);
	if (ret) {
		ksu_su_exec_emit_log(ctx, ret);
		return ret;
	}
	if (ctx->profile.applied) {
		/* Preserve native secureexec decisions and the su command
		 * environment. */
		bprm->per_clear |= PER_CLEAR_ON_SETID;
	}
	if (!ksu_su_exec_grant_valid(ctx)) {
		ksu_su_exec_emit_log(ctx, -EACCES);
		return -EACCES;
	}
	ctx->stage = KSU_SU_EXEC_CAPS_READY;
	return 0;
}

static void __nocfi ksu_su_bprm_committing_creds(ksu_su_commit_bprm_t *bprm)
{
	ksu_bprm_committing_creds_fn original =
	    (ksu_bprm_committing_creds_fn)READ_ONCE(
		ksu_su_commit_hook.original);
	struct ksu_su_exec_ctx *ctx;
	int fd;

	original(bprm);
	ctx = ksu_su_exec_bprm_ctx(bprm);
	if (!ctx)
		return;
	if (ctx->stage != KSU_SU_EXEC_CAPS_READY ||
	    !ksu_su_exec_grant_valid(ctx)) {
		ksu_su_exec_emit_log(ctx, -EACCES);
		force_sig(SIGKILL);
		return;
	}

	ksu_restore_root_profile_caps(bprm->cred, &ctx->profile);
	if (ksu_finalize_root_profile(&ctx->profile)) {
		ksu_su_exec_emit_log(ctx, -ENOMEM);
		force_sig(SIGKILL);
		return;
	}
	fd = ksu_install_su_fd();
	if (fd < 0) {
		pr_err("kasumi: sucompat: install su session fd failed: %d\n",
		       fd);
		ksu_su_exec_emit_log(ctx, fd);
		force_sig(SIGKILL);
		return;
	}
	ctx->stage = KSU_SU_EXEC_COMMITTED;
	ksu_su_exec_emit_log(ctx, 0);
	pr_info("kasumi: sucompat: native exec uid=%u fd=%d\n", ctx->uid, fd);
}

bool ksu_sucompat_exec_ready(void)
{
	return READ_ONCE(ksu_su_exec_hooks_ready);
}

int ksu_sucompat_exec_file_open(struct file *file)
{
	struct ksu_su_exec_ctx *ctx;

	if (!file || !READ_ONCE(ksu_su_exec_hooks_ready) ||
	    !(file->f_flags & __FMODE_EXEC) ||
	    !ksu_sucompat_vfs_current_ino(file_inode(file)->i_ino))
		return -EACCES;
	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;
	ctx->magic = KSU_SU_EXEC_CTX_MAGIC;
	ctx->su_ino = file_inode(file)->i_ino;
	ctx->stage = KSU_SU_EXEC_OPEN;
	file->private_data = ctx;
	return 0;
}

int ksu_sucompat_exec_file_release(struct inode *inode, struct file *file)
{
	struct ksu_su_exec_ctx *ctx;

	(void)inode;
	if (!file)
		return 0;
	ctx = file->private_data;
	file->private_data = NULL;
	if (ctx && ctx->magic == KSU_SU_EXEC_CTX_MAGIC) {
		ksu_su_exec_emit_log(ctx, -ECANCELED);
		ctx->magic = 0;
		kfree(ctx);
	}
	return 0;
}

int ksu_sucompat_exec_init(void)
{
	int ret;

	ksu_su_ptracer_capable = ksu_lookup_symbol("ptracer_capable");
	ret = ksu_register_lsm_hook(&ksu_su_creds_hook);
	if (ret)
		return ret;
	ret = ksu_register_lsm_hook(&ksu_su_caps_hook);
	if (ret) {
		ksu_unregister_lsm_hook(&ksu_su_creds_hook);
		return ret;
	}
	ret = ksu_register_lsm_hook(&ksu_su_commit_hook);
	if (ret) {
		ksu_unregister_lsm_hook(&ksu_su_caps_hook);
		ksu_unregister_lsm_hook(&ksu_su_creds_hook);
		return ret;
	}
	WRITE_ONCE(ksu_su_exec_hooks_ready, true);
	pr_info("kasumi: sucompat: native exec LSM hooks ready\n");
	return 0;
}

void ksu_sucompat_exec_exit(void)
{
	WRITE_ONCE(ksu_su_exec_hooks_ready, false);
	ksu_unregister_lsm_hook(&ksu_su_commit_hook);
	ksu_unregister_lsm_hook(&ksu_su_caps_hook);
	ksu_unregister_lsm_hook(&ksu_su_creds_hook);
}
