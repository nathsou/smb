// Cartridges: an iNES file, or a raw dump of the cartridge (PRG then CHR, no
// header) of 24 or 40 KiB (NROM). Mapper 0 (NROM, 16 or 32 KiB of PRG ROM),
// mapper 1 (MMC1, 32 to 256 KiB, CHR RAM or up to 128 KiB of CHR ROM) and mapper 4
// (MMC3, 32 to 512 KiB of PRG ROM, up to 256 KiB of CHR ROM) are supported. The recompiler applies the same rules (src/image/rom.mbt) and
// identifies the dump by the SHA-256 of PRG and CHR.

export function splitDump(rom) {
    const headered = rom.length >= 4 && rom[0] === 0x4e && rom[1] === 0x45 && rom[2] === 0x53 && rom[3] === 0x1a;
    if (headered) {
        if (rom.length < 16) throw new Error('Truncated iNES file');
        // old dumping tools left text in bytes 12-15, where byte 7 is not to be trusted either
        const archaic = (rom[7] & 0x0c) !== 8 && (rom[12] | rom[13] | rom[14] | rom[15]) !== 0;
        const mapper = (rom[6] >> 4) | (archaic ? 0 : rom[7] & 0xf0);
        const prgSize = rom[4] * 0x4000;
        const chrSize = rom[5] * 0x2000;
        if (mapper === 0) {
            if (rom[4] < 1 || rom[4] > 2) throw new Error('NROM needs 16 or 32 KiB of PRG ROM');
        } else if (mapper === 1) {
            const banks = rom[4];
            if (banks < 2 || banks > 16 || (banks & (banks - 1)) !== 0) throw new Error('MMC1 needs 32 to 256 KiB of PRG ROM in 16 KiB banks');
            if (chrSize > 0x20000) throw new Error('MMC1 has CHR RAM or up to 128 KiB of CHR ROM');
        } else if (mapper === 4) {
            const banks = rom[4] * 2;
            if (banks < 4 || banks > 64 || (banks & (banks - 1)) !== 0) throw new Error('MMC3 needs 32 to 512 KiB of PRG ROM in 8 KiB banks');
            if (chrSize > 0x40000) throw new Error('MMC3 has up to 256 KiB of CHR ROM');
        } else {
            throw new Error(`Only mappers 0 (NROM), 1 (MMC1) and 4 (MMC3) are supported, not ${mapper}`);
        }
        const start = 16 + (rom[6] & 4 ? 512 : 0);
        if (rom.length < start + prgSize + chrSize) throw new Error('Truncated iNES file');
        return {
            headered,
            mapper,
            prg: rom.subarray(start, start + prgSize),
            chr: rom.subarray(start + prgSize, start + prgSize + chrSize),
        };
    }
    if (rom.length !== 0x6000 && rom.length !== 0xa000) {
        throw new Error(`Not an iNES file, nor a raw NROM dump (24 or 40 KiB): ${rom.length} bytes`);
    }
    const prgSize = rom.length - 0x2000;
    return { headered, mapper: 0, prg: rom.subarray(0, prgSize), chr: rom.subarray(prgSize) };
}
