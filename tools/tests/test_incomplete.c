/* Cut-short MIDI files are refused as incomplete; complete ones still load.
 * usage: test_incomplete file.mid (format 1, several tracks) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "smf.h"

static int fails;
static void check(const char *what, int got, int want)
{
    printf("  %-58s %s\n", what, got == want ? "ok" : "FAIL");
    fails += got != want;
}

static int load_bytes(const unsigned char *b, long n)
{
    FILE *f = fopen("/tmp/_cut.mid", "wb");
    fwrite(b, 1, (size_t)n, f);
    fclose(f);
    smf_t s;
    int e = smf_load(&s, "/tmp/_cut.mid");
    if (e == SMF_OK) {
        smf_free(&s);
    }
    return e;
}

int main(int argc, char **argv)
{
    FILE *f = fopen(argv[1], "rb");
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *b = malloc((size_t)n + 64);
    fread(b, 1, (size_t)n, f);
    fclose(f);

    check("complete file loads", load_bytes(b, n), SMF_OK);
    check("cut in half: incomplete", load_bytes(b, n / 2), SMF_ERR_INCOMPLETE);
    check("last byte missing: incomplete", load_bytes(b, n - 1), SMF_ERR_INCOMPLETE);
    check("cut just after the header (no tracks): incomplete", load_bytes(b, 14), SMF_ERR_INCOMPLETE);
    check("cut inside the header: not a MIDI file", load_bytes(b, 10), SMF_ERR_FORMAT);
    /* last track removed but every chunk whole: fewer tracks than the header says */
    long last = 14;
    for (long p = 14; p + 8 <= n;) {
        long len = (b[p + 4] << 24) | (b[p + 5] << 16) | (b[p + 6] << 8) | b[p + 7];
        last = p;
        p += 8 + len;
    }
    check("a whole track missing: incomplete", load_bytes(b, last), SMF_ERR_INCOMPLETE);
    memcpy(b + n, "JUNK", 4);   /* a few stray bytes after the end are harmless */
    check("stray bytes after the last track still load", load_bytes(b, n + 4), SMF_OK);
    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails != 0;
}
