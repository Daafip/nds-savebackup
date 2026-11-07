# savebackup

A small Nintendo DS homebrew program that backs up the save files in a game folder on your
flashcart's memory card. Start it from the flashcart menu: it copies the saves into `/backups/`
on the same card, checks every copy, and keeps the newest 20 backups.

It was written for emulator saves (lameboy, GameYob and so on) on an old flashcart, where a
crash or switching off at the wrong moment can damage the card and take the saves with it.

## Using it

1. Get `savebackup.nds`: build it (see below), or download it from the latest **Build and
   test** run under *Actions* (needs a GitHub login).
2. Copy it into the folder with your saves, for example `/games-gb/`. Keep the name
   `savebackup.nds` (see [How it finds the folder](#how-it-finds-the-folder)).
3. Start it from the flashcart menu. Don't switch off while it shows *Don't switch off the DS!*
4. When it's done, press **A** and the DS switches off. A DS can't go back to the flashcart menu
   by itself.

To back up several folders, put a copy in each of them.

## What it copies

Every file in the folder, except:

- programs and ROMs: `.nds` `.dsi` `.srl` `.gb` `.gbc` `.sgb` `.gba`
- hidden files (names starting with `.`) and subfolders
- its own files (`savebackup.*`)

So `.sav` files, savestates, settings and anything else in the folder are copied.

## Where the backups go

The folder's path on the card is repeated under `/backups`, with one numbered folder per backup:

```
/games-gb/Pokemon Blue.sav     ->  /backups/games-gb/0001_2026-11-07_13-05/Pokemon Blue.sav
/Games/GB/Tetris.sav           ->  /backups/Games/GB/0001_2026-11-07_13-06/Tetris.sav
```

The card's root folder goes to `/backups/root`. The date and time come from the DS clock, and
the number keeps the order right even if that clock is wrong.

- Every copy is read back and compared with the original.
- If nothing changed since the newest backup, no new backup is made, so starting it often
  doesn't push out older versions.
- The newest 20 backups are kept. Older ones are only deleted after a backup in which every
  file was copied correctly.
- If a file can't be copied, that backup is renamed `…_INCOMPLETE` and no old backups are
  deleted.

## Restoring a save

Copy the files from the backup you want into the game folder, replacing the damaged ones. Do
this on a PC, or with a file manager on the DS, while the emulator isn't running.

## How it finds the folder

Some flashcarts tell a program which folder it was started from. If yours does, savebackup
uses that folder. The original R4 doesn't, so savebackup then searches the card (up to 6
folders deep, skipping `/backups`) for files named `savebackup.nds` and backs up every folder
that has one. That is why the name has to stay `savebackup.nds`.

## Limits

- Up to 64 files per folder, and up to 8 folders with a copy of savebackup.
- It uses the card driver (DLDI) that the flashcart adds when it starts a program, so other
  flashcarts should work too. It has been used on an original R4; other cards are untested.

## Building

With Docker, no install needed:

```sh
docker run --rm --user "$(id -u):$(id -g)" --volume "$PWD:/work" --workdir /work \
    skylyrac/blocksds:slim-v1.25.0@sha256:c5f19b95d9c2f312e6a68ea244d2ed5120203b4b82af9785937a349096f8e5e4 \
    make
```

Or install [BlocksDS](https://blocksds.skylyrac.net/) and run `make`. The result is
`savebackup.nds`.

## Tests

```sh
bash tests/run_tests.sh
```

This compiles `source/main.c` for the PC with small stand-ins for the DS libraries
(`tests/stub/`) and runs it against a fake memory card. It checks which files are copied,
finding the folder with and without help from the flashcart, skipping unchanged backups,
keeping 20 backups, and a copy that fails. It needs bash and a C compiler, not BlocksDS.
GitHub Actions runs the tests and builds `savebackup.nds` on every push.

## Licence

MIT, see [LICENSE](LICENSE). BlocksDS and its libraries have their own licences.
