/* SPDX-License-Identifier: GPL-3.0-only */
/* The FM-1 build on the host (tests/host.c) with a pipe to its backup over MIDI (backup.c), for tests/backup_test.py:
 * the script runs as with tests/run_host.sh (its saves are made), then, at its end, this answers on stdin / stdout:
 *   a line of hex: a decoded ask (00 59 CMD L0 L1 L2 BODY CHK) -> "@@ " and the decoded answer in hex ("@@ " alone:
 *                  no answer)
 *   "save":        the settings saved again, as SAVE would (gen moves on) -> "@@ saved"
 *   "yes", "home": SEL or HOME on REPLACE ALL SAVES? (a restore's question; then a pass of bk_poll) -> "@@ ui.ask"
 *   "tick N":      N ms of the main loop's restore part (bk_poll: the question's 60 s, the 10 s, the restart)
 *                  -> "@@ " the restore's state and the restarts so far
 * After each ask, 1 ms passes (a pass of bk_poll), as the main loop runs between messages on the FM-1. At the end of
 * stdin the flash goes to HOST_FLASH again (what a restore wrote: the next run, "after the restart", starts from it).
 * The host's own prints go to stdout too, without "@@". Built by tests/backup_test.py:
 *   cc -DDINGHY_HOST ... tests/backup_bridge.c   (it includes firmware/src/dinghy.c) */
#include "dinghy.c"

static void bridge(void)
{
    static uint8_t m[640], out[640];
    char line[2048];
    printf("@@ ready\n");
    fflush(stdout);
    while (fgets(line, sizeof line, stdin)) {
        uint32_t n = 0, r, i;
        unsigned v;
        int at = 0, k;
        if (!strncmp(line, "yes", 3) || !strncmp(line, "home", 4)) {
            k = ui.ask;
            if (ui.ask == A_RESTORE) { if (line[0] == 'y') ask_yes(); else ui.ask = 0; }
            fm1_ms++; bk_poll();
            printf("@@ %d\n", k);
            fflush(stdout);
            continue;
        }
        if (!strncmp(line, "tick", 4)) {
            for (n = (uint32_t)atoi(line + 4); n; n--) { fm1_ms++; bk_poll(); }
            printf("@@ %u %u\n", bk_st, host_restarts);
            fflush(stdout);
            continue;
        }
        if (!strncmp(line, "save", 4)) {
            settings_save(host_set + 4, (uint32_t)(host_set[1] | host_set[2] << 8 | host_set[3] << 16));
            printf("@@ saved\n");
            fflush(stdout);
            continue;
        }
        while (n < sizeof m && sscanf(line + at, "%2x%n", &v, &k) == 1) { m[n++] = (uint8_t)v; at += k; }
        r = n ? app_sysex(m, n, out, sizeof out) : 0;
        fm1_ms++; bk_poll();
        printf("@@ ");
        for (i = 0; i < r; i++) printf("%02x", out[i]);
        printf("\n");
        fflush(stdout);
    }
    host_flash(1);
}

__attribute__((constructor)) static void bridge_at_exit(void) { atexit(bridge); }
