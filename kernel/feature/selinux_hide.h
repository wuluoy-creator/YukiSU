#ifndef __KSU_H_SELINUX_HIDE
#define __KSU_H_SELINUX_HIDE

void ksu_selinux_hide_init(void);
void ksu_selinux_hide_exit(void);
void ksu_selinux_hide_drop_backup_if_unused(void);
void ksu_selinux_hide_handle_second_stage(void);
void ksu_selinux_hide_handle_post_fs_data(void);

#endif // #ifndef __KSU_H_SELINUX_HIDE
