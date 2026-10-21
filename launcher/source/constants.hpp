#pragma once
#include <cstddef>
#include <switch.h>

// Outside the application range (0x0100000000010000-0x01FFFFFFFFFFFFFF).
// Loader checks the ACID signature of application ids, so at boot2 time,
// before sys-patch has patched loader, 0x0100000000057331 was refused and
// the module never started; fs.mitm also treated it as a game.
constexpr u64    kModuleProgramId = 0x4200000000057331;
constexpr char   kNspPath[]       = "sdmc:/atmosphere/contents/4200000000057331/exefs.nsp";
constexpr char   kBoot2FlagsDir[] = "sdmc:/atmosphere/contents/4200000000057331/flags";
constexpr char   kBoot2FlagPath[] = "sdmc:/atmosphere/contents/4200000000057331/flags/boot2.flag";
constexpr u16    kUploadPort      = 7332;
constexpr u16    kServerPort      = 57331;
constexpr size_t kUploadChunk     = 65536;
