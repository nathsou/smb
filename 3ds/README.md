# SMB for the 3ds

## Build

Put a ROM of Super Mario Bros called `smb.nes` into the root folder, to extract the CHR ROM from.

```bash
cd 3ds && make
```

## Checkpoints

1. Optimize the PPU (cache tiles instead of per-pixel rendering) to run at 60fps on a regular 3ds
2. 3D mode (identify background and foreground tiles)
