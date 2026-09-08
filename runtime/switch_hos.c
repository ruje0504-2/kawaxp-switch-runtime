/* GPL-2.0-or-later */
/* Switch HOS storage for the KAWAXP runtime: RomFS game data + HOS SaveData
 * for the full-NSP layout, SD-card fallback for homebrew NRO. */
#include "kawa.h"
#include "switch_hos.h"

#ifdef __SWITCH__

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <switch.h>

int switch_romfs_active = 0;
int switch_save_active = 0;

void switch_hos_commit(void)
{
	if (!switch_save_active)
		return;
	Result rc = fsdevCommitDevice("save");
	if (R_FAILED(rc))
		note("fsdevCommitDevice(save): 0x%x", (unsigned)rc);
}

static bool dir_exists(const char *path)
{
	struct stat st;
	return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static Result mount_savedata_device(void)
{
	FsFileSystem fs;
	Result rc;

	/* Preselected user; all-zero uid = common save data when no account. */
	AccountUid uid = {0};
	rc = accountInitialize(AccountServiceType_Application);
	if (R_SUCCEEDED(rc)) {
		rc = accountGetPreselectedUser(&uid);
		accountExit();
	}
	if (R_FAILED(rc))
		uid = (AccountUid){0};

	rc = fsOpen_SaveData(&fs, FS_SAVEDATA_CURRENT_APPLICATIONID, uid);
	if (R_FAILED(rc))
		return rc;
	if (fsdevMountDevice("save", fs) == -1)
		return MAKERESULT(Module_Libnx, LibnxError_OutOfMemory);
	return 0;
}

void switch_hos_init(char *data_dir, size_t data_dir_sz,
                     char *save_dir, size_t save_dir_sz)
{
	switch_romfs_active = 0;
	switch_save_active = 0;

	/* SD access (needed by the legacy layout, harmless otherwise). */
	fsdevMountSdmc();

	/* Full NSP: game data lives in the title RomFS.  Only when the RomFS is
	 * actually mounted (i.e. we really are the installed application) do we
	 * take over the save location with HOS SaveData.  Under hbmenu (NRO)
	 * there is no RomFS, so this leaves the SD-card layout untouched and the
	 * engine keeps reading/writing sdmc:/switch/KAWAXP/kawaxp-saves/. */
	Result rc = romfsMountSelf("romfs");
	if (R_SUCCEEDED(rc) && chdir("romfs:/") == 0) {
		/* Engine appends "/name": store the device prefix WITHOUT a
		 * trailing slash so paths become romfs:/mes.ARC, save:/FLAG0100
		 * (a trailing slash here would yield romfs://mes.ARC which the
		 * fsdev devoptab rejects). */
		snprintf(data_dir, data_dir_sz, "romfs:");
		switch_romfs_active = 1;
		if (R_SUCCEEDED(mount_savedata_device())) {
			snprintf(save_dir, save_dir_sz, "save:");
			switch_save_active = 1;
		}
	}
}
#endif /* __SWITCH__ */
