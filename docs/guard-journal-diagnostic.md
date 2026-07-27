# Guard journal diagnostic firmware

This build tests whether the existing FPGA autosave engine rewrites every
mapped save sector. It does not modify the FPGA and it does not replay a
candidate save into FAT.

## Safety and activation

The diagnostic is opt-in. It activates only when the SD card has a valid MBR
partition entry whose type is `0xDA` and whose size is at least 384 sectors.
When that partition is absent or invalid, the official save mapping is left
unchanged. The firmware never guesses a raw LBA.

The current diagnostic layout is:

| Relative sectors | Purpose |
| --- | --- |
| 0 | Diagnostic header |
| 1–127 | Reserved metadata area |
| 128–383 | One 128 KiB candidate slot |

Use a dedicated test card and back it up before changing its partition table.
The partition must not overlap the FAT partition or any other partition.

## Test procedure

1. Create a type-`0xDA` partition of at least 384 sectors (2 MiB is
   recommended).
2. Boot this kernel and launch a GBA game with a non-zero save size.
3. The kernel fills the candidate slot with per-sector guards, verifies them,
   writes a `PREPARED` header, and redirects only the FPGA save map to the raw
   slot. The real `.sav` is still loaded into cartridge SRAM and is not used as
   the autosave write target.
4. Save in the game and wait at least 15 seconds.
5. Reboot normally. Before mounting FAT, the kernel checks all mapped sectors
   and displays one result:

   - `FULL WRITE`: no guard sector remains; the FPGA rewrote the whole save.
   - `PARTIAL`: some guard sectors remain; the FPGA performs dirty/partial
     writes, so guards cannot prove generic completeness.
   - `NOT WRITTEN`: every guard remains; no autosave reached the slot.
   - `INVALID HEADER`: the metadata is corrupt or inconsistent with the
     partition.

Press B to continue to the normal menu. Launching another game prepares a new
test and increments the diagnostic sequence.

## Reset ordering

At entry the kernel disables SD control, waits 120 VBlanks (about two seconds),
then performs the raw check. FatFs is mounted only after this quarantine and
the result screen. This keeps journal inspection ahead of normal filesystem
traffic.
