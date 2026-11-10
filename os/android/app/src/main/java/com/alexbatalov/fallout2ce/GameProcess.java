package com.alexbatalov.fallout2ce;

import android.app.ActivityManager;
import android.content.Context;
import android.content.Intent;

import java.util.List;

// The game runs in the app's main process (MainActivity); its list of games
// and its import screen in their own (":import"). They close the game before
// another game starts or the game's files change under it.
final class GameProcess {
    private GameProcess() {
    }

    // Asked of the game (MainActivity's receiver: it ends as on Exit, the
    // marks of the run removed).
    static String closeAction(Context context) {
        return context.getPackageName() + ".CLOSE_GAME";
    }

    // The permission both sides hold (the app's own, signature).
    static String permission(Context context) {
        return context.getPackageName() + ".UPDATE_INSTALL";
    }

    static boolean isRunning(Context context) {
        ActivityManager manager = (ActivityManager) context.getSystemService(Context.ACTIVITY_SERVICE);
        List<ActivityManager.RunningAppProcessInfo> processes = manager.getRunningAppProcesses();
        if (processes != null) {
            for (ActivityManager.RunningAppProcessInfo process : processes) {
                if (process.processName.equals(context.getPackageName())) {
                    return true;
                }
            }
        }
        return false;
    }

    // Asks the game to close and waits for its process to end (7 s at most;
    // not on the main thread). True: no game runs now.
    static boolean close(Context context) {
        if (!isRunning(context)) {
            return true;
        }
        context.sendBroadcast(new Intent(closeAction(context)).setPackage(context.getPackageName()), permission(context));
        if (waitForExit(context, 40)) {
            return true;
        }
        // Nobody to answer: the process stayed after sending the player here
        // (no game ran in it), or the game hangs. Its marks of the run stay
        // (a hang is told at the next start).
        int pid = pid(context);
        if (pid > 0) {
            android.os.Process.killProcess(pid);
        }
        return waitForExit(context, 30);
    }

    private static int pid(Context context) {
        ActivityManager manager = (ActivityManager) context.getSystemService(Context.ACTIVITY_SERVICE);
        List<ActivityManager.RunningAppProcessInfo> processes = manager.getRunningAppProcesses();
        if (processes != null) {
            for (ActivityManager.RunningAppProcessInfo process : processes) {
                if (process.processName.equals(context.getPackageName())) {
                    return process.pid;
                }
            }
        }
        return 0;
    }

    // The game closing itself ("Replace game" in it): waits for its process
    // to end.
    static boolean waitForExit(Context context) {
        return waitForExit(context, 150);
    }

    // [attempts] tenths of a second at most.
    private static boolean waitForExit(Context context, int attempts) {
        for (int attempt = 0; attempt < attempts; attempt++) {
            if (!isRunning(context)) {
                return true;
            }
            try {
                Thread.sleep(100);
            } catch (InterruptedException e) {
                return false;
            }
        }
        return false;
    }
}
