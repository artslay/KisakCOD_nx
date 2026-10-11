# Switch fastfile zone loader

This directory vendors the portable IW3/PC zone-stream runtime and generated asset loaders used by the 64-bit KisakCOD Switch port.

**Source:** [DevZer0D4Y/COD4iOS-COD4NS](https://github.com/DevZer0D4Y/COD4iOS-COD4NS/tree/main/ports/ios/zoneload), upstream commit `500462d40a4804d9c7ae78e62f92091089557c73`.

The imported loader is based on OpenAssetTools and distributed under GPLv3, consistent with this project. The Switch engine adapter is in `src/database/db_zoneload_switch.cpp`; it binds the portable reader to this port's stream, asset registry, texture/audio upload paths and engine fixups. Apple-specific platform code is not used.

`ARCH_x64` is deliberately enabled only for these translation units: it selects native 64-bit host structure handling while decoding the fastfile's 32-bit pointer/offset representation. This does not imply that Switch uses the x86-64 instruction set.
