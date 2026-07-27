#ifndef OMEGA_JOURNAL_DIAG_H
#define OMEGA_JOURNAL_DIAG_H

#include <gba_base.h>

typedef enum {
	JOURNAL_DIAG_NONE = 0,
	JOURNAL_DIAG_COMPLETE,
	JOURNAL_DIAG_PARTIAL,
	JOURNAL_DIAG_UNTOUCHED,
	JOURNAL_DIAG_INVALID
} JournalDiagResult;

typedef struct {
	JournalDiagResult result;
	u32 sequence;
	u32 game_code;
	u32 sector_count;
	u32 guard_sectors;
} JournalDiagReport;

void JournalDiag_Check(JournalDiagReport *report);
u32 JournalDiag_Prepare(u32 *fat_table, u32 save_size, const u8 game_code[4]);

#endif
