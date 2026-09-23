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
    assert(!ready_after_release(&s, 1499));
    assert(!s.armed);
    release_chord(&s, 1499);
    assert(!ready_after_release(&s, 2000));  /* Short chord does nothing. */

    press_chord(&s, 2000);
    key_update(&s, 1, ENTER, 2, 2300);  /* Repeat doesn't reset hold time. */
    assert(!ready_after_release(&s, 3500));
    assert(s.armed);
    assert(!ready_after_release(&s, 9999));  /* Still held: never launches. */
    key_update(&s, 0, LEFT_SHIFT, 0, 10000);
    assert(!ready_after_release(&s, 10000)); /* One released: still no launch. */
    key_update(&s, 1, ENTER, 0, 10001);
    assert(ready_after_release(&s, 10001));
    assert(!ready_after_release(&s, 10002)); /* Exactly once. */

    /* A late wake-up immediately followed by release still detects the hold. */
    press_chord(&s, 11000);
    release_chord(&s, 12800);
    assert(ready_after_release(&s, 12800));
    assert(!ready_after_release(&s, 12800));

    /* Shift held alone for ages doesn't count until Enter is pressed. */
    key_update(&s, 0, LEFT_SHIFT, 1, 20000);
    key_update(&s, 1, ENTER, 1, 30000);
    release_chord(&s, 31000);
    assert(!ready_after_release(&s, 32000));

    /* Right Shift / keypad Enter are equivalent, including split devices. */
    key_update(&s, 1, RIGHT_SHIFT, 1, 40000);
    key_update(&s, 0, KP_ENTER, 1, 40000);
    assert(!ready_after_release(&s, 41500));
    key_update(&s, 1, RIGHT_SHIFT, 0, 41501);
    key_update(&s, 0, KP_ENTER, 0, 41502);
    assert(ready_after_release(&s, 41502));

    /* Both Shift keys must be released if both were held. */
    press_chord(&s, 50000);
    key_update(&s, 1, RIGHT_SHIFT, 1, 50500);
    assert(!ready_after_release(&s, 51500));
    release_chord(&s, 51501);
    assert(!ready_after_release(&s, 51501));
    key_update(&s, 1, RIGHT_SHIFT, 0, 51502);
    assert(ready_after_release(&s, 51502));

    /* A lock/disabled flag or lost input cancels even an already-armed chord. */
    press_chord(&s, 60000);
    assert(!ready_after_release(&s, 61500));
    assert(s.armed);
    inhibit(&s);
    release_chord(&s, 62000);
    assert(!ready_after_release(&s, 62000));
    press_chord(&s, 63000);
    release_chord(&s, 64500);
    assert(ready_after_release(&s, 64500));

    /* Resynchronized held keys don't trigger until released and pressed anew. */
    press_chord(&s, 70000);
    inhibit(&s);
    assert(!ready_after_release(&s, 99999));
    key_update(&s, 0, LEFT_SHIFT, 0, 100000);
    key_update(&s, 0, LEFT_SHIFT, 1, 100100);
    assert(!ready_after_release(&s, 102000));
    release_chord(&s, 103000);
    assert(!ready_after_release(&s, 103000));

    /* Repeated/stale events, unrelated keys and bad indices cannot form a chord. */
    key_update(&s, 0, LEFT_SHIFT, 2, 110000);
    key_update(&s, 1, ENTER, 2, 110000);
    key_update(&s, 0, -1, 1, 110000);
    key_update(&s, 2, ENTER, 1, 110000);
    assert(!ready_after_release(&s, 113000));
    assert(!any_key(&s));

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

    puts("hotkey tests: PASS (1500ms hold, release, repeat, mixed devices, inhibit, live/stale locks)");
    return 0;
}
