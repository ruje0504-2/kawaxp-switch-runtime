/* GPL-2.0-or-later */
/* zh_CN translation table loader/lookup.  See zh.h. */
#include "zh.h"
#include "kawa.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ZH_MAX 65536
static char **zh_key, **zh_val;
static unsigned zh_n;

void zh_close(void)
{
	for (unsigned i = 0; i < zh_n; i++) { free(zh_key[i]); free(zh_val[i]); }
	free(zh_key); free(zh_val);
	zh_key = NULL; zh_val = NULL; zh_n = 0;
}

void zh_init(const char *data_dir)
{
	zh_close();
	char path[1200];
	snprintf(path, sizeof(path), "%s/zh_CN.txt", data_dir);
	FILE *f = fopen(path, "rb");
	if (!f)
		return;
	zh_key = calloc(ZH_MAX, sizeof(char *));
	zh_val = calloc(ZH_MAX, sizeof(char *));
	if (!zh_key || !zh_val) { zh_close(); fclose(f); return; }
	char line[4096];
	while (zh_n < ZH_MAX && fgets(line, sizeof(line), f)) {
		size_t n = strlen(line);
		if (n && line[n - 1] == '\n') line[--n] = 0;
		if (n && line[n - 1] == '\r') line[--n] = 0;
		if (!n || line[0] == '#') continue;   /* blank or comment */
		char *tab = strchr(line, '\t');
		if (!tab) continue;
		*tab = 0;
		if (!line[0] || !tab[1]) continue;
		zh_key[zh_n] = strdup(line);
		zh_val[zh_n] = strdup(tab + 1);
		zh_n++;
	}
	fclose(f);
}

const char *zh_tr(const char *jp)
{
	if (!zh_n || !jp || !jp[0])
		return jp ? jp : "";
	/* Linear scan is fine for a few thousand dialogue entries; a hash can
	 * come later if profiling ever calls for it. */
	for (unsigned i = 0; i < zh_n; i++)
		if (!strcmp(zh_key[i], jp))
			return zh_val[i];
	return jp;
}
