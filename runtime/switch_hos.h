/* GPL-2.0-or-later */
/* Switch HOS storage for the KAWAXP runtime.
 *
 * Full-NSP builds embed the read-only game data in the title RomFS and keep
 * save files in HOS-managed SaveData.  This module mounts both and points
 * the engine's data_dir/save_dir globals at them.  When neither is
 * available (homebrew NRO from hbmenu) it falls back to the SD-card
 * directory /switch/KAWAXP as before.
 *
 * Compiled only under __SWITCH__; no effect on host builds.
 */
#pragma once

/* Non-zero when the title RomFS was mounted ("romfs:/") and data_dir points
 * into it. */
extern int switch_romfs_active;

/* Non-zero when HOS SaveData was mounted as "save:/" and save_dir points
 * there.  When zero (homebrew NRO / mount failure) saves go to the SD card
 * as in the legacy layout. */
extern int switch_save_active;

/* Mount RomFS (if the title has one) and HOS SaveData, then set data_dir /
 * save_dir accordingly.  Call once before any game-data or save file is
 * opened. */
void switch_hos_init(char *data_dir, size_t data_dir_sz,
                     char *save_dir, size_t save_dir_sz);

/* Commit pending HOS SaveData writes (no-op when save:/ is not active).
 * Cheap and idempotent; call after closing every save file. */
void switch_hos_commit(void);
