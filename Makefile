# Build with the BlocksDS Docker image (no local install needed):
#
#   docker run --rm --user "$(id -u):$(id -g)" --volume "$PWD:/work" --workdir /work \
#       skylyrac/blocksds:slim-v1.25.0@sha256:c5f19b95d9c2f312e6a68ea244d2ed5120203b4b82af9785937a349096f8e5e4 \
#       make
#
# or run "make" with BlocksDS installed. Output: savebackup.nds
#
# The PC tests don't need BlocksDS: bash tests/run_tests.sh

NAME          := savebackup
GAME_TITLE    := Save Backup
GAME_SUBTITLE := Backs up game saves
GAME_AUTHOR   := to /backups on the card

include $(BLOCKSDS)/sys/default_makefiles/rom_arm9/Makefile
