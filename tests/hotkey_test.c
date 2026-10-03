/* Native, device-free tests for the actual shortcut state machine. */
#define C1_HOTKEY_UNIT_TEST
#include "../launcher/src/hotkey.c"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

static void press_chord(struct hotkey_state *s, uint64_t now) {
    key_update(s, 0, LEFT_SHIFT, 1, now);
    key_update(s, 1, ENTER, 1, now);
}
static void release_chord(struct hotkey_state *s, uint64_t now) {
    key_update(s, 0, LEFT_SHIFT, 0, now);
    key_update(s, 1, ENTER, 0, now);
}
int main(void) {
    struct hotkey_state s = {0};
    press_chord(&s, 0);
    assert(!chord_ready(&s, 1499));
    release_chord(&s, 1499);               /* Short chord does nothing. */
    assert(!chord_ready(&s, 5000));
    assert(!s.armed);

    press_chord(&s, 2000);
    key_update(&s, 1, ENTER, 2, 2300);  /* Repeat doesn't reset hold time. */
    assert(!chord_ready(&s, 3499));
    assert(chord_ready(&s, 3500));      /* Fires at the threshold while held. */
    assert(s.armed);
    assert(!chord_ready(&s, 9999));     /* Once per press: stays consumed. */
    release_chord(&s, 10000);           /* Release re-arms... */
    assert(!s.armed && !s.timing);
    press_chord(&s, 11000);             /* ...and a fresh chord fires again. */
    assert(!chord_ready(&s, 12499));
    assert(chord_ready(&s, 12500));
    release_chord(&s, 12600);

    /* A delayed poll past the threshold still fires while held. */
    press_chord(&s, 13000);
    assert(chord_ready(&s, 16000));
    release_chord(&s, 16001);

    /* Shift held alone for ages doesn't count until Enter is pressed. */
    key_update(&s, 0, LEFT_SHIFT, 1, 20000);
    key_update(&s, 1, ENTER, 1, 30000);
    assert(!chord_ready(&s, 31499));
    assert(chord_ready(&s, 31500));
    release_chord(&s, 31600);

    /* Right Shift / keypad Enter are equivalent, including split devices. */
    key_update(&s, 1, RIGHT_SHIFT, 1, 40000);
    key_update(&s, 0, KP_ENTER, 1, 40000);
    assert(!chord_ready(&s, 41499));
    assert(chord_ready(&s, 41500));
    key_update(&s, 1, RIGHT_SHIFT, 0, 41501);
    key_update(&s, 0, KP_ENTER, 0, 41502);
    assert(!s.armed);

    /* Holding an extra Shift doesn't restart the timer. */
    press_chord(&s, 50000);
    key_update(&s, 1, RIGHT_SHIFT, 1, 50500);
    assert(chord_ready(&s, 51500));
    release_chord(&s, 51501);
    key_update(&s, 1, RIGHT_SHIFT, 0, 51502);
    assert(!s.armed);

    /* A lock/disabled flag or lost input cancels a pending chord. */
    press_chord(&s, 60000);
    inhibit(&s);
    assert(!chord_ready(&s, 62000));
    release_chord(&s, 62001);
    assert(!s.inhibited);
    press_chord(&s, 63000);
    assert(chord_ready(&s, 64500));
    release_chord(&s, 64501);

    /* Resynchronized held keys don't trigger until released and pressed anew. */
    press_chord(&s, 70000);
    inhibit(&s);
    assert(!chord_ready(&s, 99999));
    key_update(&s, 0, LEFT_SHIFT, 0, 100000);
    key_update(&s, 0, LEFT_SHIFT, 1, 100100);
    release_chord(&s, 103000);
    assert(!s.inhibited);
    press_chord(&s, 104000);
    assert(chord_ready(&s, 105500));
    release_chord(&s, 105501);

    /* Repeated/stale events, unrelated keys and bad indices cannot form a chord. */
    key_update(&s, 0, LEFT_SHIFT, 2, 110000);
    key_update(&s, 1, ENTER, 2, 110000);
    key_update(&s, 0, -1, 1, 110000);
    key_update(&s, 2, ENTER, 1, 110000);
    assert(!chord_ready(&s, 113000));
    assert(!any_key(&s));

    /* Back long-press: fires at the 2s threshold while still held, once. */
    key_update(&s, 1, BACK, 1, 120000);
    assert(!back_ready(&s, 121999));
    assert(back_ready(&s, 122000));
    s.back_fired = 1;                    /* Main loop consumes exactly once. */
    assert(!back_ready(&s, 130000));
    key_update(&s, 1, BACK, 0, 130001);  /* Release; no second fire. */
    assert(!back_ready(&s, 131000));
    key_update(&s, 1, BACK, 1, 140000);  /* Fresh press re-arms. */
    assert(!back_ready(&s, 141999));
    assert(back_ready(&s, 142000));
    key_update(&s, 1, BACK, 2, 143000);  /* Auto-repeat keeps the hold alive. */
    s.back_fired = 0;
    assert(back_ready(&s, 143000));
    key_update(&s, 1, BACK, 0, 143100);

    /* Back on the keypad device (event0) never triggers. */
    key_update(&s, 0, BACK, 1, 150000);
    assert(!back_ready(&s, 160000));
    key_update(&s, 0, BACK, 0, 160001);

    /* Inhibit cancels a pending back-hold until a fresh press. */
    key_update(&s, 1, BACK, 1, 170000);
    inhibit(&s);
    assert(!back_ready(&s, 180000));
    key_update(&s, 1, BACK, 0, 180001);  /* Release clears inhibition. */
    assert(!s.inhibited);
    key_update(&s, 1, BACK, 1, 190000);
    assert(!back_ready(&s, 191999));
    assert(back_ready(&s, 192000));
    key_update(&s, 1, BACK, 0, 192001);

    /* Stale inhibition with no keys held clears at the loop level, so the
     * first gesture after a foreground->stock switch is not swallowed. */
    inhibit(&s);
    maybe_uninhibit(&s);
    assert(!s.inhibited);
    key_update(&s, 1, BACK, 1, 200000);  /* First back-hold fires right away. */
    assert(!back_ready(&s, 201999));
    assert(back_ready(&s, 202000));
    key_update(&s, 1, BACK, 0, 202001);

    /* Inhibition with a key physically held survives until the release. */
    key_update(&s, 1, BACK, 1, 210000);
    inhibit(&s);
    maybe_uninhibit(&s);
    assert(s.inhibited);
    key_update(&s, 1, BACK, 0, 210100);
    assert(!s.inhibited);

    /* A stale run.lock must not disable the global entry after a reboot. */
    char directory[] = "/tmp/c1max-hotkey-test.XXXXXX";
    assert(mkdtemp(directory));
    char foreground[256], stale[256], bad[256], link[256];
    snprintf(foreground, sizeof foreground, "%s/foreground.lock", directory);
    snprintf(stale, sizeof stale, "%s/run.lock", directory);
    snprintf(bad, sizeof bad, "%s/missing-parent/lock", directory);
    snprintf(link, sizeof link, "%s/symlink.lock", directory);
    assert(mkdir(stale, 0700) == 0);
    assert(!foreground_busy(foreground));
    int owner = open(foreground, O_RDWR);
    assert(owner >= 0);
    assert(flock(owner, LOCK_EX | LOCK_NB) == 0);
    assert(foreground_busy(foreground));
    assert(flock(owner, LOCK_UN) == 0);
    assert(!foreground_busy(foreground));
    assert(foreground_busy(bad));  /* Unknown lock state fails closed. */
    assert(symlink(foreground, link) == 0);
    assert(foreground_busy(link)); /* Never follow a replacement lock inode. */
    close(owner);
    assert(unlink(link) == 0);
    assert(unlink(foreground) == 0);
    assert(rmdir(stale) == 0);
    assert(rmdir(directory) == 0);

    puts("hotkey tests: PASS (threshold fire, once-per-press, re-arm, repeat, mixed devices, inhibit, live/stale locks)");
    return 0;
}
