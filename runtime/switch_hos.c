/* GPL-2.0-or-later */
/* Switch HOS storage for the KAWAXP runtime: RomFS game data + HOS SaveData
 * for the full-NSP layout, SD-card fallback for homebrew NRO. */
#include "kawa.h"
#include "switch_hos.h"

#ifdef __SWITCH__

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <switch.h>

int switch_romfs_active = 0;
int switch_save_active = 0;

void switch_hos_commit(void)
{
	if (!switch_save_active)
		return;
	fsdevCommitDevice("save");
}

/* ---- native HOS SaveData file IO ------------------------------------ */
static FsFileSystem *save_fs(void){ return switch_save_active?fsdevGetDeviceFileSystem("save"):NULL; }
static int save_path(const char *rel, char *path, size_t sz){
 char full[FS_MAX_PATH]; snprintf(full,sizeof(full),"save:/%s",rel);
 FsFileSystem *fs=NULL;
 if(fsdevTranslatePath(full,&fs,path)==-1||!fs) return -1;
 return 0;
}
int switch_hos_save_write(const char *rel, const void *data, size_t len){
 FsFileSystem *fs=save_fs(); if(!fs) return -1;
 char path[FS_MAX_PATH]; if(save_path(rel,path,sizeof(path))!=0) return -1;
 Result rc;
 fsFsDeleteFile(fs,path); /* ignore not-found */
 rc=fsFsCreateFile(fs,path,(s64)len,0);
 if(R_FAILED(rc)) return (int)rc;
 FsFile f;
 rc=fsFsOpenFile(fs,path,FsOpenMode_Write,&f);
 if(R_FAILED(rc)) return (int)rc;
 const size_t CH=64*1024;
 size_t off=0;
 while(off<len){
  size_t n=len-off; if(n>CH)n=CH;
  rc=fsFileWrite(&f,(s64)off,(const char*)data+off,n,0);
  if(R_FAILED(rc)){ fsFileClose(&f); return (int)rc; }
  off+=n;
 }
 fsFileClose(&f);
 rc=fsFsCommit(fs);
 if(R_FAILED(rc)) return (int)rc;
 return (int)rc;
}
int switch_hos_save_read(const char *rel, void *buf, size_t cap, size_t *out_len){
 FsFileSystem *fs=save_fs(); if(!fs) return -1;
 char path[FS_MAX_PATH]; if(save_path(rel,path,sizeof(path))!=0) return -1;
 FsFile f; Result rc=fsFsOpenFile(fs,path,FsOpenMode_Read,&f);
 if(R_FAILED(rc)) return (int)rc;
 s64 sz=0; rc=fsFileGetSize(&f,&sz);
 if(R_FAILED(rc)){ fsFileClose(&f); return (int)rc; }
 size_t rd=0;
 while((size_t)sz>rd&&rd<cap){
  u64 got=0; u64 want=(u64)(sz-rd); if(want>cap-rd) want=cap-rd;
  rc=fsFileRead(&f,(s64)rd,(char*)buf+rd,want,0,&got);
  if(R_FAILED(rc)){ fsFileClose(&f); return (int)rc; }
  rd+=(size_t)got; if(!got) break;
 }
 fsFileClose(&f); if(out_len)*out_len=rd; return 0;
}

int switch_hos_save_remove(const char *rel){
 FsFileSystem *fs=save_fs(); if(!fs) return -1;
 char path[FS_MAX_PATH]; if(save_path(rel,path,sizeof(path))!=0) return -1;
 return (int)fsFsDeleteFile(fs,path);
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
