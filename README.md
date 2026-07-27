# EZ-FLASH OMEGA Kernel

## How to build 

1. We use devkitARM_r47. You can use the current version or newer.
2. Set the following environment variables in system, or modify the value in `build.bat` (if you use Windows), based on your installation path.

```
PATH,DEVKITARM,DEVKITPRO,LIBGBA
```

3. Double click on `build.bat` under Windows (or run make if you use Linux). If it goes well, you will get `ezkernel.gba` and, `ezkernel.bin`, which is the omega kernel upgrade file.

## Guard journal diagnostic

This branch includes an opt-in raw-journal diagnostic for determining whether
the FPGA autosave engine rewrites every mapped save sector. See
[docs/guard-journal-diagnostic.md](docs/guard-journal-diagnostic.md) for the SD
layout, safety constraints, and test procedure.
