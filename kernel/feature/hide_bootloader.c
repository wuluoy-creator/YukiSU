#include <linux/cache.h>
#include <linux/compiler.h>
#include <linux/printk.h>
#include <linux/types.h>

#include "policy/feature.h"
#include "feature/hide_bootloader.h"

static bool hide_bootloader_enabled __read_mostly;

static int hide_bootloader_feature_get(u64 *value)
{
	*value = READ_ONCE(hide_bootloader_enabled) ? 1 : 0;
	return 0;
}

static int hide_bootloader_feature_set(u64 value)
{
	WRITE_ONCE(hide_bootloader_enabled, value != 0);
	pr_info("hide_bootloader: set to %d\n",
		READ_ONCE(hide_bootloader_enabled));
	return 0;
}

static const struct ksu_feature_handler hide_bootloader_handler = {
    .feature_id = KSU_FEATURE_HIDE_BOOTLOADER,
    .name = "hide_bootloader",
    .get_handler = hide_bootloader_feature_get,
    .set_handler = hide_bootloader_feature_set,
};

void ksu_hide_bootloader_init(void)
{
	if (ksu_register_feature_handler(&hide_bootloader_handler))
		pr_err("hide_bootloader: failed to register feature handler\n");
}

void ksu_hide_bootloader_exit(void)
{
	ksu_unregister_feature_handler(KSU_FEATURE_HIDE_BOOTLOADER);
}
