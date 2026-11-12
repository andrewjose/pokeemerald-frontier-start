// Headless GBA runner on libmgba (0.10.x).
// Usage: gbarun ROM SCRIPT   (see tools/headless/README.md for the script commands)
// Exits 1 if any `expect32` fails.
#include <mgba/core/core.h>
#include <mgba/core/config.h>
#include <mgba/core/log.h>
#include <mgba/core/serialize.h>
#include <mgba-util/vfs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void nolog(struct mLogger *l, int c, enum mLogLevel lv, const char *f, va_list a) { (void)l;(void)c;(void)lv;(void)f;(void)a; }
static struct mLogger logger = { .log = nolog };
static struct mCore *core; static color_t *buf; static unsigned W, H; static long frame; static int failed;

static unsigned keymask(const char *s) {
    unsigned m = 0; char tmp[128]; strncpy(tmp, s, 127); tmp[127] = 0;
    for (char *t = strtok(tmp, "+"); t; t = strtok(NULL, "+")) {
        if (!strcmp(t,"A")) m|=1; else if (!strcmp(t,"B")) m|=2; else if (!strcmp(t,"SELECT")) m|=4;
        else if (!strcmp(t,"START")) m|=8; else if (!strcmp(t,"RIGHT")) m|=16; else if (!strcmp(t,"LEFT")) m|=32;
        else if (!strcmp(t,"UP")) m|=64; else if (!strcmp(t,"DOWN")) m|=128; else if (!strcmp(t,"R")) m|=256;
        else if (!strcmp(t,"L")) m|=512; else if (!strcmp(t,"NONE")) m|=0;
    }
    return m;
}
static void run(unsigned keys, int n) { core->setKeys(core, keys); for (int i = 0; i < n; i++) { core->runFrame(core); frame++; } }
static void shot(const char *f) {
    FILE *o = fopen(f, "wb"); fprintf(o, "P6\n%u %u\n255\n", W, H);
    for (unsigned i = 0; i < W*H; i++) { uint32_t c = buf[i]; unsigned char p[3] = { c & 0xFF, (c >> 8) & 0xFF, (c >> 16) & 0xFF }; fwrite(p, 1, 3, o); }
    fclose(o);
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: gbarun ROM SCRIPT\n"); return 1; }
    mLogSetDefaultLogger(&logger);
    core = mCoreFind(argv[1]); if (!core || !core->init(core)) { fprintf(stderr, "core init failed\n"); return 1; }
    mCoreInitConfig(core, NULL);
    core->desiredVideoDimensions(core, &W, &H);
    buf = calloc(W * H, sizeof(color_t)); core->setVideoBuffer(core, buf, W);
    if (!mCoreLoadFile(core, argv[1])) { fprintf(stderr, "load failed\n"); return 1; }
    FILE *s = fopen(argv[2], "r"); char line[512]; int reset = 0;
    while (fgets(line, sizeof line, s)) {
        char *h = strchr(line, '#'); if (h) *h = 0;
        char cmd[64] = {0}, a[256] = {0}, b[256] = {0}, c[256] = {0}, d[64] = {0};
        int n = sscanf(line, "%63s %255s %255s %255s %63s", cmd, a, b, c, d);
        if (n < 1) continue;
        if (!strcmp(cmd, "save")) { struct VFile *vf = VFileOpen(a, O_RDONLY); if (vf) core->loadSave(core, vf); continue; }
        if (!reset) { core->reset(core); reset = 1; }
        if (!strcmp(cmd, "wait")) run(0, atoi(a));
        else if (!strcmp(cmd, "hold")) run(keymask(a), atoi(b));
        else if (!strcmp(cmd, "tap")) { run(keymask(a), 4); run(0, 8); }
        else if (!strcmp(cmd, "taps")) { for (int i = 0; i < atoi(b); i++) { run(keymask(a), 4); run(0, 8); } }
        else if (!strcmp(cmd, "shot")) { shot(a); printf("[%ld] shot %s\n", frame, a); }
        else if (!strcmp(cmd, "state_save")) { struct VFile *vf = VFileOpen(a, O_CREAT|O_TRUNC|O_RDWR); mCoreSaveStateNamed(core, vf, SAVESTATE_SAVEDATA|SAVESTATE_RTC); vf->close(vf); printf("[%ld] state_save %s\n", frame, a); }
        else if (!strcmp(cmd, "state_load")) { struct VFile *vf = VFileOpen(a, O_RDONLY); int ok = mCoreLoadStateNamed(core, vf, SAVESTATE_SAVEDATA|SAVESTATE_RTC); vf->close(vf); printf("[%ld] state_load %s %s\n", frame, a, ok ? "ok" : "FAIL"); }
        else if (!strcmp(cmd, "peek8")) printf("[%ld] peek8 %s = 0x%02x\n", frame, a, core->busRead8(core, strtoul(a,0,0)));
        else if (!strcmp(cmd, "peek16")) printf("[%ld] peek16 %s = 0x%04x\n", frame, a, core->busRead16(core, strtoul(a,0,0)));
        else if (!strcmp(cmd, "peek32")) printf("[%ld] peek32 %s = 0x%08x\n", frame, a, core->busRead32(core, strtoul(a,0,0)));
        else if (!strcmp(cmd, "poke8")) core->rawWrite8(core, strtoul(a,0,0), -1, strtoul(b,0,0));
        else if (!strcmp(cmd, "poke16")) core->rawWrite16(core, strtoul(a,0,0), -1, strtoul(b,0,0));
        else if (!strcmp(cmd, "poke32")) core->rawWrite32(core, strtoul(a,0,0), -1, strtoul(b,0,0));
        else if (!strcmp(cmd, "dump")) { uint32_t ad = strtoul(a,0,0); int len = atoi(b); FILE *o = fopen(c, "wb"); for (int i = 0; i < len; i++) fputc(core->busRead8(core, ad+i), o); fclose(o); }
        else if (!strcmp(cmd, "waitfor32")) { uint32_t ad = strtoul(a,0,0), v = strtoul(b,0,0); int mx = atoi(c), i = 0; while (core->busRead32(core, ad) != v && i < mx) { run(0, 1); i++; } printf("[%ld] waitfor32 %s %s\n", frame, a, i < mx ? "hit" : "TIMEOUT"); }
        else if (!strcmp(cmd, "tapuntil32")) { uint32_t ad = strtoul(a,0,0), v = strtoul(b,0,0); unsigned k = keymask(c); int mx = atoi(d), i = 0; while (core->busRead32(core, ad) != v && i < mx) { run(k, 4); run(0, 20); i += 24; } printf("[%ld] tapuntil32 %s %s\n", frame, a, i < mx ? "hit" : "TIMEOUT"); }
        else if (!strcmp(cmd, "orbyte_ptr")) { uint32_t p = core->busRead32(core, strtoul(a,0,0)) + strtoul(b,0,0); core->rawWrite8(core, p, -1, core->busRead8(core, p) | strtoul(c,0,0)); }
        else if (!strcmp(cmd, "expect32")) { uint32_t got = core->busRead32(core, strtoul(a,0,0)), want = strtoul(b,0,0); printf("[%ld] expect32 %s = 0x%08x (want 0x%08x) %s\n", frame, a, got, want, got == want ? "OK" : "FAIL"); if (got != want) failed = 1; }
        else if (!strcmp(cmd, "sav_out")) { void *data; size_t sz = core->savedataClone(core, &data); FILE *o = fopen(a, "wb"); fwrite(data, 1, sz, o); fclose(o); free(data); }
        else fprintf(stderr, "unknown cmd %s\n", cmd);
        fflush(stdout);
    }
    core->deinit(core); return failed;
}
