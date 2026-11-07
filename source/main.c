/*
 * savebackup - back up the save files in a game folder.
 *
 * Copy savebackup.nds into the folder with your games and saves (for example /games-gb/) and
 * start it from the flashcart menu. It copies every file in that folder except programs and
 * ROMs (see ROM_EXTS) and its own savebackup.* files into
 *
 *     /backups/<folder path>/<NNNN>_<YYYY-MM-DD>_<HH-MM>/
 *
 * (for example /backups/games-gb/0007_2026-11-07_13-05/),
 * reads every copy back to check it, and keeps the newest KEEP_SETS backups. If nothing
 * changed since the newest backup, no new backup is made, so starting it several times doesn't
 * push out older versions. Old backups are only deleted after a backup in which every file
 * succeeded. The date comes from the DS clock; the running number NNNN keeps the order right
 * even if that clock is wrong.
 *
 * Not every flashcart tells a program which folder it was started from (the original R4
 * doesn't). Then it searches the card for savebackup.nds and backs up the folder where it is
 * (every such folder, if there are several copies). So keep that file name.
 *
 * It shows the result and then waits for A to switch the DS off (a DS can't go back to the
 * flashcart menu by itself).
 *
 * Built with BlocksDS (see Makefile and README.md). MIT licence, see LICENSE.
 */
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <fat.h>
#include <nds.h>

#define VERSION        "1.0"
#define KEEP_SETS      20
#define MAX_FILES      64
#define MAX_SETS       1000
#define PATH_LEN       320
#define CHUNK          16384
#define SELF_NAME      "savebackup.nds"
#define MAX_FOUND      8      // folders with a copy of this program
#define MAX_DEPTH      6      // how deep the card is searched for it

// Programs and ROMs, which are not backed up.
static const char *const ROM_EXTS[] = { ".nds", ".dsi", ".srl", ".gb", ".gbc", ".sgb", ".gba" };

typedef struct { char name[256]; off_t size; } Entry;
typedef struct { int num; char name[64]; } Set;

static char src_dir[PATH_LEN];    // folder to back up, no trailing '/' (except a drive root)
static char dest_root[PATH_LEN];  // <drive>/backups/<folder path>
static Entry files[MAX_FILES];
static int nfiles;
static Set sets[MAX_SETS];
static int nsets;
static char victims[MAX_FILES][256];
static char found[MAX_FOUND][PATH_LEN];   // folders to back up
static int nfound;
static u8 buf_a[CHUNK], buf_b[CHUNK];

// ------------------------------------------------------------------ small helpers

static void wait_for_any_key(void)
{
    while (1) {
        swiWaitForVBlank();
        scanKeys();
        if (keysDown())
            return;
    }
}

static void fail(const char *msg)
{
    printf("\nERROR: %s\n\nPress any button to switch off.\n", msg);
    wait_for_any_key();
    systemShutDown();
    while (1)
        swiWaitForVBlank();
}

// Returns false (and an empty path) if the result would not fit, so a cut-off path is never used.
static bool join(char *out, const char *dir, const char *name)
{
    size_t n = strlen(dir);
    int len;
    if (n > 0 && dir[n - 1] == '/')
        len = snprintf(out, PATH_LEN, "%s%s", dir, name);
    else
        len = snprintf(out, PATH_LEN, "%s/%s", dir, name);
    if (len < 0 || len >= PATH_LEN) {
        out[0] = '\0';
        return false;
    }
    return true;
}

// "fat:/games-gb/" -> "fat:/games-gb", but "fat:/" stays "fat:/"
static void strip_slash(char *p)
{
    size_t n = strlen(p);
    while (n > 1 && p[n - 1] == '/' && p[n - 2] != ':')
        p[--n] = '\0';
}

static bool is_file(const char *path, off_t *size)
{
    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode))
        return false;
    if (size)
        *size = st.st_size;
    return true;
}

static bool is_dir(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static bool make_dir(const char *path)
{
    return mkdir(path, 0777) == 0 || errno == EEXIST || is_dir(path);
}

// Creates `path` and any missing folders above it ("fat:/backups/Games/GB").
static bool make_dirs(const char *path)
{
    char p[PATH_LEN];
    size_t n = strlen(path);
    if (n == 0 || n >= sizeof(p))
        return false;
    memcpy(p, path, n + 1);
    const char *colon = strchr(p, ':');
    size_t start = colon ? (size_t)(colon - p) + 1 : 0;   // skip the drive name
    while (p[start] == '/')
        start++;
    for (size_t i = start; i < n; i++) {
        if (p[i] != '/')
            continue;
        p[i] = '\0';
        bool ok = make_dir(p);
        p[i] = '/';
        if (!ok)
            return false;
    }
    return make_dir(p);
}

static bool has_ext(const char *name, const char *ext)
{
    size_t n = strlen(name), e = strlen(ext);
    return n > e && strcasecmp(name + n - e, ext) == 0;
}

// Long names don't fit on the 32-column screen: "Pokemon - Bl..nced).sav"
static const char *short_name(const char *name)
{
    static char out[32];
    size_t n = strlen(name);
    if (n <= 21)
        return name;
    snprintf(out, sizeof(out), "%.11s..%s", name, name + n - 8);
    return out;
}

// ------------------------------------------------------------------ finding the files

static void add_found(const char *dir)
{
    if (nfound < MAX_FOUND)
        snprintf(found[nfound++], PATH_LEN, "%s", dir);
}

// "fat:/" or "sd:/"
static bool is_drive_root(const char *p)
{
    size_t n = strlen(p);
    return n >= 2 && p[n - 1] == '/' && p[n - 2] == ':';
}

// Adds `dir` and every folder below it that holds a copy of this program. The names of the
// subfolders are collected and the folder is closed before going deeper, so only one folder
// is open at a time.
static void search(const char *dir, int depth)
{
    DIR *d = opendir(dir);
    if (!d)
        return;
    char *subdirs = NULL;   // names, each ending in '\0'
    size_t used = 0, cap = 0;
    bool here = false;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        const char *n = e->d_name;
        if (n[0] == '.')
            continue;
        char p[PATH_LEN];
        bool sub = e->d_type == DT_DIR ||
                   (e->d_type == DT_UNKNOWN && join(p, dir, n) && is_dir(p));
        if (!sub) {
            if (strcasecmp(n, SELF_NAME) == 0)
                here = true;
            continue;
        }
        if (depth >= MAX_DEPTH || (depth == 0 && strcasecmp(n, "backups") == 0))
            continue;
        size_t len = strlen(n) + 1;
        if (used + len > cap) {
            size_t ncap = cap ? cap * 2 : 1024;
            while (ncap < used + len)
                ncap *= 2;
            char *grown = realloc(subdirs, ncap);
            if (!grown)
                break;
            subdirs = grown;
            cap = ncap;
        }
        memcpy(subdirs + used, n, len);
        used += len;
    }
    closedir(d);
    if (here)
        add_found(dir);
    for (size_t i = 0; i < used; i += strlen(subdirs + i) + 1) {
        char p[PATH_LEN];
        if (join(p, dir, subdirs + i))
            search(p, depth + 1);
    }
    free(subdirs);
}

// Fills found[] with the folder(s) to back up: the folder this program was started from.
static void find_sources(int argc, char **argv)
{
    // Known directly if the flashcart passes argv. Without argv (or with a path it can't
    // read) fatGetDefaultCwd() returns the drive root, so a root is only trusted if this
    // program is really there.
    bool have_argv = argc > 0 && argv && argv[0] && argv[0][0];
    char *cwd = fatGetDefaultCwd();
    if (cwd) {
        char c[PATH_LEN], self[PATH_LEN];
        snprintf(c, sizeof(c), "%s", cwd);
        free(cwd);
        strip_slash(c);
        if (have_argv && is_dir(c) &&
            (!is_drive_root(c) || (join(self, c, SELF_NAME) && is_file(self, NULL)))) {
            add_found(c);
            return;
        }
    }
    // Otherwise look for this program on the card.
    printf("Looking for %s...\n", SELF_NAME);
    search(fatGetDefaultDrive(), 0);
    if (nfound > 1)
        printf("Found it in %d folders,\nbacking up each of them.\n", nfound);
    if (nfound > 0)
        printf("\n");
}

static bool wanted(const char *name)
{
    if (name[0] == '.')                                   // ".", ".." and hidden files
        return false;
    for (size_t i = 0; i < sizeof(ROM_EXTS) / sizeof(ROM_EXTS[0]); i++)
        if (has_ext(name, ROM_EXTS[i]))
            return false;
    if (strncasecmp(name, "savebackup.", 11) == 0)        // this program's own files
        return false;
    return true;
}

static int cmp_entries(const void *a, const void *b)
{
    return strcasecmp(((const Entry *)a)->name, ((const Entry *)b)->name);
}

static void list_files(void)
{
    DIR *d = opendir(src_dir);
    if (!d)
        fail("Can't open the game folder.");
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (!wanted(e->d_name))
            continue;
        char p[PATH_LEN];
        off_t size;
        if (!join(p, src_dir, e->d_name) || !is_file(p, &size))
            continue;
        if (nfiles == MAX_FILES) {
            printf("Too many files, only the first %d are copied.\n", MAX_FILES);
            break;
        }
        snprintf(files[nfiles].name, sizeof(files[nfiles].name), "%s", e->d_name);
        files[nfiles].size = size;
        nfiles++;
    }
    closedir(d);
    qsort(files, nfiles, sizeof(Entry), cmp_entries);
}

// ------------------------------------------------------------------ backup sets

static int cmp_sets(const void *a, const void *b)
{
    return ((const Set *)a)->num - ((const Set *)b)->num;
}

static void scan_sets(void)
{
    nsets = 0;
    DIR *d = opendir(dest_root);
    if (!d)
        return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL && nsets < MAX_SETS) {
        const char *n = e->d_name;
        if (strlen(n) < 5 || strlen(n) >= sizeof(sets[0].name) || n[4] != '_')
            continue;
        if (!isdigit((unsigned char)n[0]) || !isdigit((unsigned char)n[1]) ||
            !isdigit((unsigned char)n[2]) || !isdigit((unsigned char)n[3]))
            continue;
        char p[PATH_LEN];
        if (!join(p, dest_root, n) || !is_dir(p))
            continue;
        sets[nsets].num = atoi(n);
        memcpy(sets[nsets].name, n, strlen(n) + 1);   // length checked above
        nsets++;
    }
    closedir(d);
    qsort(sets, nsets, sizeof(Set), cmp_sets);
}

// Deletes the files in one backup folder, then the folder itself. Nothing else.
static void delete_set(const char *name)
{
    char dir[PATH_LEN], p[PATH_LEN];
    if (!join(dir, dest_root, name))
        return;
    DIR *d = opendir(dir);
    if (!d)
        return;
    int nv = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL && nv < MAX_FILES) {
        if (strcmp(e->d_name, ".") != 0 && strcmp(e->d_name, "..") != 0)
            snprintf(victims[nv++], sizeof(victims[0]), "%s", e->d_name);
    }
    closedir(d);
    for (int i = 0; i < nv; i++) {
        if (join(p, dir, victims[i]) && is_file(p, NULL))
            unlink(p);
    }
    rmdir(dir);
}

static int prune(void)
{
    scan_sets();
    int removed = 0;
    for (int i = 0; i < nsets - KEEP_SETS; i++) {
        delete_set(sets[i].name);
        removed++;
    }
    return removed;
}

// ------------------------------------------------------------------ copying and comparing

static bool copy_file(const char *src, const char *dst, off_t expected)
{
    FILE *in = fopen(src, "rb");
    if (!in)
        return false;
    FILE *out = fopen(dst, "wb");
    if (!out) {
        fclose(in);
        return false;
    }
    bool ok = true;
    off_t total = 0;
    size_t n;
    while ((n = fread(buf_a, 1, CHUNK, in)) > 0) {
        if (fwrite(buf_a, 1, n, out) != n) {
            ok = false;
            break;
        }
        total += n;
    }
    if (ferror(in))
        ok = false;
    fclose(in);
    if (fclose(out) != 0)
        ok = false;
    return ok && total == expected;
}

static bool same_contents(const char *a, const char *b)
{
    FILE *fa = fopen(a, "rb");
    FILE *fb = fopen(b, "rb");
    bool ok = fa && fb;
    while (ok) {
        size_t na = fread(buf_a, 1, CHUNK, fa);
        size_t nb = fread(buf_b, 1, CHUNK, fb);
        if (na != nb || memcmp(buf_a, buf_b, na) != 0)
            ok = false;
        else if (na == 0)
            break;
    }
    if (fa && ferror(fa))
        ok = false;
    if (fb && ferror(fb))
        ok = false;
    if (fa)
        fclose(fa);
    if (fb)
        fclose(fb);
    return ok;
}

// True if the newest complete backup holds exactly the current files with the same contents.
static bool same_as_newest(int *num)
{
    scan_sets();
    for (int i = nsets - 1; i >= 0; i--) {
        if (strstr(sets[i].name, "_INCOMPLETE"))
            continue;
        char dir[PATH_LEN], src[PATH_LEN], dst[PATH_LEN];
        if (!join(dir, dest_root, sets[i].name))
            return false;
        *num = sets[i].num;

        int count = 0;
        DIR *d = opendir(dir);
        if (!d)
            return false;
        struct dirent *e;
        while ((e = readdir(d)) != NULL)
            if (join(dst, dir, e->d_name) && is_file(dst, NULL))
                count++;
        closedir(d);
        if (count != nfiles)
            return false;

        for (int j = 0; j < nfiles; j++) {
            off_t size;
            if (!join(src, src_dir, files[j].name) || !join(dst, dir, files[j].name))
                return false;
            if (!is_file(dst, &size) || size != files[j].size || !same_contents(src, dst))
                return false;
        }
        return true;
    }
    return false;
}

// Makes a new backup set and reports whether every file was copied and checked.
static void backup(void)
{
    if (!make_dirs(dest_root))
        fail("Can't create the backups\nfolder. Card full or locked?");
    scan_sets();
    int next = nsets ? sets[nsets - 1].num + 1 : 1;
    if (next > 9999)
        fail("Backup number 9999 reached.\nRename the backups folder.");

    char set_name[80], set_dir[PATH_LEN];
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    if (tm)
        snprintf(set_name, sizeof(set_name), "%04d_%04d-%02d-%02d_%02d-%02d", next,
                 tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min);
    else
        snprintf(set_name, sizeof(set_name), "%04d_no-date", next);
    if (!join(set_dir, dest_root, set_name) || !make_dir(set_dir))
        fail("Can't create the backup\nfolder. Card full or locked?");

    printf("Backing up to:\n %s\n", set_name);
    printf("Don't switch off the DS!\n\n");

    int bad = 0;
    for (int i = 0; i < nfiles; i++) {
        char src[PATH_LEN], dst[PATH_LEN];
        bool ok = join(src, src_dir, files[i].name) && join(dst, set_dir, files[i].name)
                  && copy_file(src, dst, files[i].size) && same_contents(src, dst);
        printf(" %-21s %s\n", short_name(files[i].name), ok ? "OK" : "FAILED");
        if (!ok)
            bad++;
    }

    if (bad == 0) {
        int removed = prune();
        printf("\nDone: %d file(s) backed up.\n", nfiles);
        if (removed)
            printf("Removed %d old backup(s).\n", removed);
        return;
    }
    char failed_dir[PATH_LEN];
    int len = snprintf(failed_dir, sizeof(failed_dir), "%s_INCOMPLETE", set_dir);
    if (len > 0 && len < (int)sizeof(failed_dir))
        rename(set_dir, failed_dir);
    printf("\n%d file(s) FAILED!\nThis backup is marked\nINCOMPLETE. Old backups\nwere kept.\n", bad);
}

// ------------------------------------------------------------------ switching off

static void switch_off(void)
{
    printf("Switching off...\n");
    for (int i = 0; i < 30; i++)       // let the screen show it; all files are closed
        swiWaitForVBlank();
    systemShutDown();
    while (1)
        swiWaitForVBlank();
}

static void wait_for_a_then_switch_off(void)
{
    printf("\nPress A to switch off.\n");
    while (1) {
        swiWaitForVBlank();
        scanKeys();
        if (keysDown() & KEY_A)
            switch_off();
    }
}

// ------------------------------------------------------------------ main

static void back_up_folder(const char *dir)
{
    snprintf(src_dir, sizeof(src_dir), "%s", dir);
    nfiles = 0;
    list_files();

    // <drive>/backups/<path of the folder on the card>, e.g. fat:/backups/Games/GB for
    // fat:/Games/GB, so folders with the same name in different places stay apart.
    // The root of the card is backed up to <drive>/backups/root.
    char drive[PATH_LEN], tmp[PATH_LEN];
    const char *colon = strchr(src_dir, ':');
    const char *rel = colon ? colon + 1 : src_dir;
    if (colon)
        snprintf(drive, sizeof(drive), "%.*s/", (int)(colon - src_dir + 1), src_dir);
    else
        snprintf(drive, sizeof(drive), "/");
    while (*rel == '/')
        rel++;
    if (!join(tmp, drive, "backups") || !join(dest_root, tmp, *rel ? rel : "root"))
        fail("The folder path is too long.");
    printf("Folder: %s\n\n", src_dir);

    int newest = 0;
    if (nfiles == 0)
        printf("Nothing to back up yet.\n");
    else if (same_as_newest(&newest))
        printf("No changes since backup %04d,\nnothing to do.\n", newest);
    else
        backup();
}

int main(int argc, char **argv)
{
    consoleDemoInit();
    printf("Save backup " VERSION "\n");
    printf("===============\n\n");

    if (!fatInitDefault())
        fail("Can't access the SD card.");
    find_sources(argc, argv);
    if (nfound == 0)
        fail("Can't find " SELF_NAME "\non the card. Keep that file\nname, so it can tell which\nfolder to back up.");
    for (int i = 0; i < nfound; i++) {
        if (i > 0)
            printf("\n");
        back_up_folder(found[i]);
    }

    wait_for_a_then_switch_off();
    return 0;
}
