package com.alexbatalov.fallout2ce;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

public class HangWatchdogTest {
    private static long state(long frames, boolean loading) {
        return (frames << 1) | (loading ? 1 : 0);
    }

    @Test
    public void framesStoppedForTheLimitAreAHang() {
        HangWatchdog watchdog = new HangWatchdog();
        watchdog.reset(0);
        assertFalse(watchdog.stalled(0, state(10, false)));
        assertFalse(watchdog.stalled(HangWatchdog.LIMIT_MS - 1, state(10, false)));
        assertTrue(watchdog.stalled(HangWatchdog.LIMIT_MS, state(10, false)));
        // New frames: it came back.
        assertFalse(watchdog.stalled(HangWatchdog.LIMIT_MS + 1, state(11, false)));
    }

    @Test
    public void loadingWaitsLonger() {
        HangWatchdog watchdog = new HangWatchdog();
        watchdog.reset(0);
        assertFalse(watchdog.stalled(0, state(10, true)));
        assertFalse(watchdog.stalled(HangWatchdog.LIMIT_MS, state(10, true)));
        assertTrue(watchdog.stalled(HangWatchdog.LOADING_LIMIT_MS, state(10, true)));
    }

    @Test
    public void notBeforeTheFirstFrameNorRightAfterWaiting() {
        HangWatchdog watchdog = new HangWatchdog();
        watchdog.reset(0);
        assertFalse(watchdog.stalled(0, state(0, false)));
        assertFalse(watchdog.stalled(HangWatchdog.LOADING_LIMIT_MS * 2, state(0, false)));
        assertFalse(watchdog.stalled(100000, state(5, false)));
        assertTrue(watchdog.stalled(100000 + HangWatchdog.LIMIT_MS, state(5, false)));
        watchdog.waited(200000);
        assertFalse(watchdog.stalled(200000 + HangWatchdog.LIMIT_MS - 1, state(5, false)));
        assertTrue(watchdog.stalled(200000 + HangWatchdog.LIMIT_MS, state(5, false)));
    }
}
