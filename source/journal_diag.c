#include <gba_base.h>
#include <string.h>

#include "Ezcard_OP.h"
#include "journal_diag.h"

#define FAT_TABLE_SAV_OFFSET         0x200
#define JOURNAL_PARTITION_TYPE       0xDA
#define JOURNAL_MBR_PARTITION_OFFSET 0x1BE
#define JOURNAL_METADATA_SECTORS     128
#define JOURNAL_SLOT_SECTORS         256
#define JOURNAL_MAGIC                0x4A44474F
#define JOURNAL_VERSION              1
#define JOURNAL_STATE_PREPARED       1
#define JOURNAL_HEADER_WORDS         10

typedef struct {
	u32 magic;
	u32 version;
	u32 state;
	u32 sequence;
	u32 slot_lba;
	u32 save_size;
	u32 guard_seed;
	u32 game_code;
	u32 partition_sectors;
	u32 header_checksum;
	u8 reserved[512 - JOURNAL_HEADER_WORDS * sizeof(u32)];
} JournalDiagHeader;

static u8 sector_buffer[512] EWRAM_BSS __attribute__((aligned(4)));
static u8 compare_buffer[512] EWRAM_BSS __attribute__((aligned(4)));
static JournalDiagHeader current_header EWRAM_BSS;

static u32 read_le32(const u8 *p)
{
	return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) |
	       ((u32)p[3] << 24);
}

static u32 checksum_words(const u32 *words, u32 count)
{
	u32 hash = 2166136261UL;
	u32 i;

	for (i = 0; i < count; i++) {
		hash ^= words[i];
		hash *= 16777619UL;
	}
	return hash;
}

static u32 find_journal_partition(u32 *start_lba, u32 *sector_count)
{
	u32 i;

	Read_SD_sectors(0, 1, sector_buffer);
	if (sector_buffer[510] != 0x55 || sector_buffer[511] != 0xAA)
		return 0;

	for (i = 0; i < 4; i++) {
		const u8 *entry = sector_buffer + JOURNAL_MBR_PARTITION_OFFSET + i * 16;
		u32 start;
		u32 count;

		if (entry[4] != JOURNAL_PARTITION_TYPE)
			continue;
		start = read_le32(entry + 8);
		count = read_le32(entry + 12);
		if (start == 0 || count < JOURNAL_METADATA_SECTORS +
		    JOURNAL_SLOT_SECTORS)
			return 0;
		*start_lba = start;
		*sector_count = count;
		return 1;
	}
	return 0;
}

static u32 header_valid(const JournalDiagHeader *header, u32 partition_start,
			u32 partition_sectors)
{
	u32 checksum;
	u32 save_sectors;

	if (header->magic != JOURNAL_MAGIC ||
	    header->version != JOURNAL_VERSION ||
	    header->state != JOURNAL_STATE_PREPARED)
		return 0;
	checksum = checksum_words((const u32 *)header, JOURNAL_HEADER_WORDS - 1);
	if (checksum != header->header_checksum)
		return 0;
	save_sectors = (header->save_size + 511) / 512;
	if (save_sectors == 0 || save_sectors > JOURNAL_SLOT_SECTORS)
		return 0;
	if (header->slot_lba != partition_start + JOURNAL_METADATA_SECTORS ||
	    header->partition_sectors != partition_sectors)
		return 0;
	return 1;
}

static u32 guard_next(u32 value)
{
	value ^= value << 13;
	value ^= value >> 17;
	value ^= value << 5;
	return value;
}

static void make_guard(u8 *buffer, u32 seed, u32 sector)
{
	u32 *words = (u32 *)buffer;
	u32 value = seed ^ (0x9E3779B9UL * (sector + 1));
	u32 i;

	for (i = 0; i < 512 / sizeof(u32); i++) {
		value = guard_next(value);
		words[i] = value ^ sector ^ i;
	}
}

void JournalDiag_Check(JournalDiagReport *report)
{
	u32 partition_start;
	u32 partition_sectors;
	u32 sectors;
	u32 i;

	memset(report, 0, sizeof(*report));
	if (!find_journal_partition(&partition_start, &partition_sectors))
		return;

	Read_SD_sectors(partition_start, 1, (u8 *)&current_header);
	if (current_header.magic == 0 || current_header.magic == 0xFFFFFFFF)
		return;
	if (!header_valid(&current_header, partition_start, partition_sectors)) {
		report->result = JOURNAL_DIAG_INVALID;
		return;
	}

	sectors = (current_header.save_size + 511) / 512;
	report->sequence = current_header.sequence;
	report->game_code = current_header.game_code;
	report->sector_count = sectors;
	for (i = 0; i < sectors; i++) {
		make_guard(compare_buffer, current_header.guard_seed, i);
		Read_SD_sectors(current_header.slot_lba + i, 1, sector_buffer);
		if (memcmp(compare_buffer, sector_buffer, sizeof(compare_buffer)) == 0)
			report->guard_sectors++;
	}

	if (report->guard_sectors == 0)
		report->result = JOURNAL_DIAG_COMPLETE;
	else if (report->guard_sectors == sectors)
		report->result = JOURNAL_DIAG_UNTOUCHED;
	else
		report->result = JOURNAL_DIAG_PARTIAL;
}

u32 JournalDiag_Prepare(u32 *fat_table, u32 save_size, const u8 game_code[4])
{
	u32 partition_start;
	u32 partition_sectors;
	u32 slot_lba;
	u32 sectors;
	u32 sequence = 1;
	u32 guard_seed;
	u32 i;
	u32 *save_map;

	sectors = (save_size + 511) / 512;
	if (sectors == 0 || sectors > JOURNAL_SLOT_SECTORS)
		return 0;
	if (!find_journal_partition(&partition_start, &partition_sectors))
		return 0;
	slot_lba = partition_start + JOURNAL_METADATA_SECTORS;

	Read_SD_sectors(partition_start, 1, (u8 *)&current_header);
	if (header_valid(&current_header, partition_start, partition_sectors))
		sequence = current_header.sequence + 1;
	guard_seed = sequence ^ slot_lba ^ 0xA5C39E17UL;
	if (guard_seed == 0)
		guard_seed = 1;

	for (i = 0; i < sectors; i++) {
		make_guard(sector_buffer, guard_seed, i);
		Write_SD_sectors(slot_lba + i, 1, sector_buffer);
		Read_SD_sectors(slot_lba + i, 1, compare_buffer);
		if (memcmp(sector_buffer, compare_buffer, sizeof(compare_buffer)) != 0)
			return 0;
	}

	memset(&current_header, 0, sizeof(current_header));
	current_header.magic = JOURNAL_MAGIC;
	current_header.version = JOURNAL_VERSION;
	current_header.state = JOURNAL_STATE_PREPARED;
	current_header.sequence = sequence;
	current_header.slot_lba = slot_lba;
	current_header.save_size = save_size;
	current_header.guard_seed = guard_seed;
	memcpy(&current_header.game_code, game_code,
	       sizeof(current_header.game_code));
	current_header.partition_sectors = partition_sectors;
	current_header.header_checksum =
		checksum_words((const u32 *)&current_header,
			       JOURNAL_HEADER_WORDS - 1);
	Write_SD_sectors(partition_start, 1, (u8 *)&current_header);
	Read_SD_sectors(partition_start, 1, compare_buffer);
	if (memcmp(&current_header, compare_buffer, sizeof(current_header)) != 0)
		return 0;

	/* One contiguous extent: offset 0 -> slot LBA, followed by terminator. */
	save_map = fat_table + FAT_TABLE_SAV_OFFSET / sizeof(u32);
	save_map[0] = 0;
	save_map[1] = slot_lba;
	save_map[2] = 0xFFFFFFFF;
	save_map[3] = 0;
	return 1;
}
