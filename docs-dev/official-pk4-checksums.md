# Official Quake 4 PK4 Checksums (q4base)

This table captures the PK4 checksums loaded from the installed game directory:

- Install path: `C:\Program Files (x86)\Steam\steamapps\common\Quake 4\q4base`
- Source: `logs/openq4.log` startup lines (`Loaded pk4 ... with checksum ...`) under `fs_savepath\<gameDir>\`
- Checksum format: engine PK4 checksum (`MD4` of zip-entry CRC list, as computed in `src/framework/FileSystem.cpp`)

OpenQ4 ignores the retail game-binary PK4 archives (`game000.pk4` through `game300.pk4`, plus `gamex*.pk4` variants) because it ships its own game modules. They are not required and are not verified.

## Required official baseline

These media PK4s are required by OpenQ4 startup validation (`fs_validateOfficialPaks 1`, default):

| PK4 | Checksum |
|---|---|
| `pak001.pk4` | `0xf2cbc998` |
| `pak002.pk4` | `0x7f8d80d1` |
| `pak003.pk4` | `0x1b57b207` |
| `pak004.pk4` | `0x385aa578` |
| `pak005.pk4` | `0x60d50a1d` |
| `pak006.pk4` | `0x9099ed11` |
| `pak007.pk4` | `0xaf301fff` |
| `pak008.pk4` | `0x4ac6f6d9` |
| `pak009.pk4` | `0x36030c7d` |
| `pak010.pk4` | `0x4b80fbda` |
| `pak011.pk4` | `0x8acf4cfa` |
| `pak012.pk4` | `0xbe4120b0` |
| `pak013.pk4` | `0x6ad67f40` |
| `pak014.pk4` | `0xee51cd59` |
| `pak015.pk4` | `0xf5bf4e0c` |
| `pak016.pk4` | `0x2196f58c` |
| `pak017.pk4` | `0x91118a35` |
| `pak018.pk4` | `0x98a14f03` |
| `pak019.pk4` | `0xbc82ac79` |
| `pak020.pk4` | `0xce74cda5` |
| `pak021.pk4` | `0x2ba6e70c` |
| `pak022.pk4` | `0x4e390eec` |
| `pak023.pk4` | `0x7c1fd3a5` |
| `pak024.pk4` | `0x5546d551` |
| `pak025.pk4` | `0xcaeec1fd` |

## Additional official PK4s detected

| PK4 | Checksum |
|---|---|
| `q4cmp_pak001.pk4` | `0xd0813943` |
| `zpak_english.pk4` | `0x5868f530` |
| `zpak_english_01.pk4` | `0xd9f04b8b` |
| `zpak_english_02.pk4` | `0x9dbd91fd` |
| `zpak_english_03.pk4` | `0x02eb6ad8` |
| `zpak_english_04.pk4` | `0xd3fefaa1` |
| `zpak_english_05.pk4` | `0x8596af60` |
