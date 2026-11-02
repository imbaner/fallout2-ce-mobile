package com.alexbatalov.fallout2ce;

// The game's hang watchdog (error_report.h): it stopped showing new frames
// for 20 s, 60 s while a map or a save loads. The game's state is its frame
// count << 1 | loading (MainActivity.nativeWatchState).
final class HangWatchdog {
    static final long LIMIT_MS = 20000;
    static final long LOADING_LIMIT_MS = 60000;

    private long frames = -1;
    private long changed;

    static boolean loading(long state) {
        return (state & 1) != 0;
    }

    // Counted from [now] (the game came to the front).
    void reset(long now) {
        frames = -1;
        changed = now;
    }

    // The player chose to wait: counted from [now] again.
    void waited(long now) {
        changed = now;
    }

    // True when the frames haven't changed for longer than the limit (not
    // before the first one).
    boolean stalled(long now, long state) {
        long count = state >>> 1;
        if (count != frames) {
            frames = count;
            changed = now;
            return false;
        }
        return count > 0 && now - changed >= (loading(state) ? LOADING_LIMIT_MS : LIMIT_MS);
    }
}
