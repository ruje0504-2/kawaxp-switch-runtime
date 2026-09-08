/* GPL-2.0-or-later */
/* zh_CN text translation layer.
 *
 * The game scripts stay in the original Japanese; on display, dialog and
 * branch-choice strings are looked up in an external UTF-8 table
 * (data_dir/zh_CN.txt) and replaced with Simplified Chinese when a match
 * exists.  Entries are  "原文<TAB>译文" per line, UTF-8.  Editing the table
 * is the whole "polish" workflow: no rebuild, no length limits.
 */
#ifndef KAWA_ZH_H
#define KAWA_ZH_H

/* Load data_dir/zh_CN.txt (no-op if absent).  Call after data_dir is set. */
void zh_init(const char *data_dir);

/* Return the Simplified-Chinese translation of a UTF-8 Japanese string, or
 * the original string when there is no entry.  Never returns NULL. */
const char *zh_tr(const char *utf8_jp);

/* Unload table (optional). */
void zh_close(void);

#endif
