#include "usb_mass_storage.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/usb/usb_device.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/usb/class/usbd_msc.h>
#include <zephyr/fs/fs.h>
#include <stdio.h>
#include <ff.h>

LOG_MODULE_REGISTER(mass_storage, CONFIG_APP_LOG_LEVEL);

static struct fs_mount_t fs_mnt;

static int setup_flash(struct fs_mount_t *mnt)
{
	int rc = 0;
	unsigned int id;
	const struct flash_area *pfa;

	mnt->storage_dev = (void *)STORAGE_PARTITION_ID;
	id = STORAGE_PARTITION_ID;

	rc = flash_area_open(id, &pfa);
	LOG_DBG("Area %u at 0x%x on %s for %u bytes",
	       id, (unsigned int)pfa->fa_off, pfa->fa_dev->name,
	       (unsigned int)pfa->fa_size);

	if (rc < 0 && IS_ENABLED(CONFIG_APP_WIPE_STORAGE)) {
		LOG_INF("Erasing flash area ... ");
		rc = flash_area_erase(pfa, 0, pfa->fa_size);
		if (rc != 0) {
			LOG_ERR("Failed to erase falsh area (%i)", rc);
		}
	}

	if (rc < 0) {
		flash_area_close(pfa);
	}
	return rc;
}

static int mount_app_fs(struct fs_mount_t *mnt)
{
	int rc;

	static FATFS fat_fs;

	mnt->type = FS_FATFS;
	mnt->fs_data = &fat_fs;
	mnt->mnt_point = "/NAND:";

	rc = fs_mount(mnt);

	return rc;
}

static void setup_disk(void)
{
	struct fs_mount_t *mp = &fs_mnt;
	struct fs_statvfs sbuf;
	int rc;

	rc = setup_flash(mp);
	if (rc < 0) {
		LOG_ERR("Failed to setup flash area");
		return;
	}

	if (!IS_ENABLED(CONFIG_FAT_FILESYSTEM_ELM)) {
		LOG_INF("No compatible file system selected");
		return;
	}

	rc = mount_app_fs(mp);
	if (rc < 0) {
		LOG_ERR("Failed to mount filesystem");
		return;
	}

	/* Allow log messages to flush to avoid interleaved output */
	k_sleep(K_MSEC(50));

	LOG_DBG("Mount %s: %d", fs_mnt.mnt_point, rc);

	rc = fs_statvfs(mp->mnt_point, &sbuf);
	if (rc < 0) {
		LOG_ERR("FAIL: statvfs: %d", rc);
		return;
	}

	LOG_DBG("%s: bsize = %lu ; frsize = %lu ;"
	       " blocks = %lu ; bfree = %lu",
	       mp->mnt_point,
	       sbuf.f_bsize, sbuf.f_frsize,
	       sbuf.f_blocks, sbuf.f_bfree);

	return;
}

/* List dir entry by path
 *
 * @param path Absolute path to list
 *
 * @return Negative errno code on error, number of listed entries on
 *         success.
 */
int usb_mass_storage_lsdir(const char *path)
{
	struct fs_mount_t *mp = &fs_mnt;
	int rc;
	struct fs_dir_t dir;
	int count = 0;

	const char** tmp_path;
	if (path == NULL) {
		tmp_path = &mp->mnt_point;
	} else {
		if (memcmp(path, mp->mnt_point, strlen(mp->mnt_point))) {
			LOG_ERR("Path %s is not in mount point %s", path, mp->mnt_point);
			return -EINVAL;
		}
		tmp_path = &path;
	}

	fs_dir_t_init(&dir);

	/* Verify fs_opendir() */
	rc = fs_opendir(&dir, *tmp_path);
	if (rc) {
		LOG_ERR("Error opening dir %s [%d]", *tmp_path, rc);
		return rc;
	}

	LOG_DBG("Listing dir %s ...", *tmp_path);
	while (rc >= 0) {
		struct fs_dirent ent = { 0 };

		rc = fs_readdir(&dir, &ent);
		if (rc < 0) {
			LOG_ERR("Failed to read directory entries");
			break;
		}
		if (ent.name[0] == 0) {
			LOG_DBG("End of files");
			break;
		}
		LOG_DBG("  %c %u %s",
		       (ent.type == FS_DIR_ENTRY_FILE) ? 'F' : 'D',
		       ent.size,
		       ent.name);
	}

	/* Verify fs_closedir() */
	fs_closedir(&dir);
	if (rc == 0) {
		rc = count;
	}

	return rc;
}

int usb_mass_storage_create_file(const char *path, const char *filename){
	char file_path[128];
	uint8_t base = 0;
	struct fs_mount_t *mp = &fs_mnt;
	struct fs_file_t file;

	if (path == NULL) {
		memcpy(file_path, mp->mnt_point, strlen(mp->mnt_point));
		base = strlen(mp->mnt_point);
	} else {
		memcpy(file_path, path, strlen(path));
		if (path[strlen(path) - 1] == '/') {
			file_path[strlen(path) - 1] = 0;
		}
		base = strlen(file_path);
	}

	file_path[base++] = '/';
	file_path[base] = 0;

	strcat(file_path, filename);
	fs_file_t_init(&file);

	int rc = fs_open(&file, file_path, FS_O_CREATE | FS_O_RDWR);
	if (rc != 0) {
		LOG_ERR("Failed to create file %s (%i)", file_path, rc);
		return rc;
	}

	rc = fs_close(&file);
	if (rc != 0) {
		LOG_ERR("Failed to close file %s (%i)", file_path, rc);
		return rc;
	}

	return 0;
}

int usb_mass_storage_init() {

	setup_disk();

#if CONFIG_USB_DEVICE_INITIALIZE_AT_BOOT == 0
	int ret = usb_enable(NULL);

	if (ret != 0) {
		LOG_ERR("Failed to enable USB (%i)", ret);
		return -EIO;
	}
#endif

	return 0;
}
