package com.alexbatalov.fallout2ce;

import android.app.Activity;
import android.app.Application;
import android.app.PendingIntent;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageInfo;
import android.content.pm.PackageInstaller;
import android.content.pm.PackageManager;
import android.content.pm.Signature;
import android.net.Uri;
import android.os.Build;
import android.provider.Settings;
import android.util.Log;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.util.UUID;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

// One updater per game process. JNI only reads memory or queues work; all
// network, APK verification and disk copies run on this worker. No game
// files are read, and nothing is uploaded.
final class UpdateManager {
    // Kept in sync with AppUpdateState / AppUpdateError (app_update.h).
    static final int UNSUPPORTED = -1, IDLE = 0, CHECKING = 1, UP_TO_DATE = 2,
        AVAILABLE = 3, DOWNLOADING = 4, DOWNLOADED = 5, INSTALLING = 6, ERROR = 7, PERMISSION = 8, NO_ASSET = 9;
    static final int NO_ERROR = 0, CHECK_ERROR = 1, SERVICE_ERROR = 2,
        DOWNLOAD_ERROR = 3, INVALID_APK = 4, INSTALL_ERROR = 5;
    // In addition to the receiver's signature permission, a per-process
    // action rejects callbacks left over from a previous process.
    static final String INSTALL_ACTION = UpdateRelease.RELEASE_PACKAGE + ".UPDATE_INSTALL." + UUID.randomUUID();
    private static final String TAG = "FalloutUpdate";
    private static final ExecutorService worker = Executors.newSingleThreadExecutor();

    private static Application context;
    private static SharedPreferences preferences;
    private static String repository;
    private static String appVersion;
    private static long installedVersion;
    private static JSONObject release;
    private static File readyFile;
    private static int state = UNSUPPORTED;
    private static int error = NO_ERROR;
    private static int generation;
    private static boolean restoring;
    private static boolean pendingCheck, pendingManual;
    private static boolean automaticCheckAttempted;
    private static volatile boolean cancelled;
    private static volatile long downloaded, total;
    private static volatile HttpURLConnection activeConnection;
    private static boolean foreground, activeScreen;
    // A late task from a closed dialog cannot install for a newly opened one.
    private static long screenToken, sessionScreenToken;
    private static int sessionId = -1;
    private static PendingIntent sessionResult;

    private UpdateManager() {
    }

    static synchronized void initialize(Context app) {
        if (context != null) return;
        context = (Application) app.getApplicationContext();
        preferences = context.getSharedPreferences("updates", Context.MODE_PRIVATE);
        try {
            PackageInfo info = context.getPackageManager().getPackageInfo(context.getPackageName(), 0);
            installedVersion = code(info);
            appVersion = info.versionName;
        } catch (PackageManager.NameNotFoundException e) {
            Log.w(TAG, "Cannot read installed version", e);
        }
        configure("");
    }

    static synchronized void configure(String override) {
        if (context == null) return;
        String selected = UpdateRelease.repository(context.getPackageName(), BuildConfig.BUILD_TYPE, BuildConfig.DEBUG, override);
        if (selected == null ? repository == null : selected.equals(repository)) return;
        cancelled = true;
        HttpURLConnection connection = activeConnection;
        if (connection != null) connection.disconnect();
        final int previousSession = sessionId;
        finishSession();
        if (previousSession >= 0) worker.execute(() -> abandon(context.getPackageManager().getPackageInstaller(), previousSession));
        repository = selected;
        int run = ++generation;
        release = null;
        readyFile = null;
        state = selected == null ? UNSUPPORTED : IDLE;
        error = NO_ERROR;
        downloaded = total = 0;
        pendingCheck = pendingManual = false;
        restoring = false;
        automaticCheckAttempted = false;
        if (selected == null) return;
        restoring = true;
        worker.execute(() -> restore(run, selected));
    }

    static synchronized int state() { return state; }
    static synchronized int error() { return error; }
    static synchronized boolean ready() { return readyFile != null; }
    static synchronized String string(int field) {
        if (release == null) return "";
        return release.optString(field == 0 ? "tag" : field == 1 ? "name" : "notes", "");
    }
    static long[] progress() { return new long[] { downloaded, total }; }

    static synchronized void setActive(boolean value) {
        activeScreen = value;
        screenToken++;
    }

    static synchronized long activeToken() { return activeScreen ? screenToken : -1; }

    static synchronized void resumed(boolean value) {
        foreground = value;
        if (value && state == PERMISSION && canInstall()) state = DOWNLOADED;
    }

    static synchronized void check(boolean manual) {
        if (repository == null) return;
        if (restoring) {
            pendingCheck = true;
            pendingManual |= manual;
            return;
        }
        // Only the first menu entry after a cold start may check
        // automatically. Returning from the game or resuming the activity
        // does not create another request, even after the interval passes.
        if (!manual) {
            if (automaticCheckAttempted) return;
            automaticCheckAttempted = true;
        }
        if (state == CHECKING || state == DOWNLOADING || state == INSTALLING) return;
        long now = System.currentTimeMillis();
        long last = preferences.getLong("last_check." + repository, 0);
        // A clock moved backwards doesn't suppress checks indefinitely.
        if (!UpdateRelease.shouldCheck(now, last, manual)) return;
        preferences.edit().putLong("last_check." + repository, now).apply();
        state = CHECKING;
        error = NO_ERROR;
        final int run = generation;
        final String repo = repository;
        worker.execute(() -> {
            try {
                JSONObject response = new JSONObject(readResponse("https://api.github.com/repos/" + repo + "/releases/latest"));
                JSONObject candidate = candidate(response, repo);
                boolean same;
                synchronized (UpdateManager.class) {
                    if (run != generation) return;
                    same = candidate != null && release != null && release.optString("tag").equals(candidate.optString("tag"))
                        && release.optString("url").equals(candidate.optString("url")) && release.optLong("size") == candidate.optLong("size")
                        && release.optString("digest").equals(candidate.optString("digest"));
                }
                if (!same) clearCache();
                if (candidate != null) saveRelease(candidate);
                synchronized (UpdateManager.class) {
                    if (run != generation) return;
                    if (candidate == null) {
                        release = null;
                        readyFile = null;
                        downloaded = total = 0;
                        state = UpdateRelease.versionCode(response.optString("tag_name")) > installedVersion ? NO_ASSET : UP_TO_DATE;
                    } else {
                        if (!same) {
                            readyFile = null;
                            downloaded = 0;
                        }
                        release = candidate;
                        total = candidate.getLong("size");
                        state = readyFile == null ? AVAILABLE : DOWNLOADED;
                    }
                }
            } catch (Exception e) {
                fail(run, e instanceof ServiceException ? SERVICE_ERROR : CHECK_ERROR, "Check", e);
            }
        });
    }

    private static JSONObject candidate(JSONObject response, String repo) throws JSONException {
        if (response.optBoolean("draft") || response.optBoolean("prerelease")) return null;
        String tag = response.optString("tag_name");
        long version = UpdateRelease.versionCode(tag);
        if (version < 0) Log.i(TAG, "Ignoring an unrecognized release tag");
        if (version <= installedVersion) return null;
        JSONArray assets = response.optJSONArray("assets");
        if (assets == null) return null;
        String[] names = new String[assets.length()];
        for (int index = 0; index < names.length; index++) names[index] = assets.getJSONObject(index).optString("name");
        int index = UpdateRelease.assetIndex(names, Build.SUPPORTED_ABIS);
        if (index == -1) return null;
        JSONObject asset = assets.getJSONObject(index);
        String url = asset.optString("browser_download_url");
        long size = asset.optLong("size");
        String digest = asset.optString("digest");
        if (!UpdateRelease.validAsset(url, repo, tag, size, digest)) return null;
        String notes = response.isNull("body") ? "" : response.optString("body");
        if (notes.length() > UpdateRelease.MAX_NOTES_LENGTH) notes = notes.substring(0, UpdateRelease.MAX_NOTES_LENGTH) + "\n…";
        return new JSONObject().put("repo", repo).put("tag", tag).put("version", version)
            .put("notes", notes).put("url", url).put("size", size).put("digest", digest);
    }

    static synchronized void download() {
        if (release == null || restoring || state == CHECKING || state == DOWNLOADING || state == INSTALLING) return;
        final JSONObject selected = release;
        final int run = generation;
        cancelled = false;
        downloaded = 0;
        total = selected.optLong("size");
        state = DOWNLOADING;
        error = NO_ERROR;
        worker.execute(() -> {
            File part = new File(cache(), "download.part");
            HttpURLConnection connection = null;
            try {
                synchronized (UpdateManager.class) { if (run != generation || cancelled) return; }
                clearCache();
                saveRelease(selected);
                synchronized (UpdateManager.class) { if (run == generation) readyFile = null; }
                long size = selected.getLong("size");
                // Keep room for the APK and the system installer's staged copy.
                if (cache().getUsableSpace() < size * 2 + 8 * 1024 * 1024L) throw new IOException("Not enough space for update");
                connection = connect(selected.getString("url"));
                activeConnection = connection;
                long started = System.nanoTime();
                try (InputStream in = connection.getInputStream(); FileOutputStream out = new FileOutputStream(part)) {
                    byte[] buffer = new byte[65536];
                    int count;
                    while (!cancelled && (count = in.read(buffer)) != -1) {
                        if (System.nanoTime() - started > 10 * 60 * 1000000000L) throw new IOException("Update download timed out");
                        if (downloaded + count > size) throw new IOException("Wrong APK size");
                        out.write(buffer, 0, count);
                        downloaded += count;
                    }
                    out.getFD().sync();
                }
                if (cancelled) return;
                if (part.length() != size || !verify(part, selected)) throw new InvalidApkException();
                File complete = new File(cache(), selected.getString("tag") + ".apk");
                synchronized (UpdateManager.class) {
                    // Cancel and promotion to a ready APK are one decision,
                    // including a cancel during certificate verification.
                    if (run != generation || cancelled) return;
                    if (!part.renameTo(complete)) throw new IOException("Cannot finish APK");
                    readyFile = complete;
                    state = DOWNLOADED;
                }
            } catch (Exception e) {
                if (!cancelled) fail(run, e instanceof InvalidApkException ? INVALID_APK : DOWNLOAD_ERROR, "Download", e);
            } finally {
                activeConnection = null;
                if (connection != null) connection.disconnect();
                // An incomplete or invalid APK can never become installable.
                if (part.exists() && !part.delete()) Log.w(TAG, "Cannot remove incomplete APK");
                synchronized (UpdateManager.class) {
                    if (cancelled && run == generation) {
                        state = AVAILABLE;
                        error = NO_ERROR;
                        downloaded = 0;
                    }
                }
            }
        });
    }

    static synchronized void cancel() {
        if (state != DOWNLOADING) return;
        cancelled = true;
        HttpURLConnection connection = activeConnection;
        if (connection != null) connection.disconnect();
    }

    static synchronized void deleteDownload() {
        if (state == CHECKING || state == DOWNLOADING || state == INSTALLING || release == null) return;
        readyFile = null;
        downloaded = 0;
        state = AVAILABLE;
        error = NO_ERROR;
        final JSONObject selected = release;
        worker.execute(() -> {
            try {
                clearCache();
                saveRelease(selected);
            } catch (IOException e) { Log.w(TAG, "Cannot remove download", e); }
        });
    }

    // Called on the activity's UI thread. A dismissed update screen or a
    // backgrounded game never starts an installer, including a late result.
    static synchronized void install(Activity activity, long token) {
        if (!activeScreen || token != screenToken || !foreground || readyFile == null
            || (state != DOWNLOADED && state != PERMISSION && state != ERROR)) return;
        if (!canInstall()) {
            state = PERMISSION;
            try {
                activity.startActivity(new Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES, Uri.parse("package:" + context.getPackageName())));
            } catch (RuntimeException e) { fail(generation, INSTALL_ERROR, "Permission", e); }
            return;
        }
        final File apk = readyFile;
        final JSONObject selected = release;
        final int run = generation;
        state = INSTALLING;
        error = NO_ERROR;
        worker.execute(() -> {
            PackageInstaller installer = context.getPackageManager().getPackageInstaller();
            int id = -1;
            boolean committed = false;
            try {
                if (!verify(apk, selected)) throw new InvalidApkException();
                PackageInstaller.SessionParams params = new PackageInstaller.SessionParams(PackageInstaller.SessionParams.MODE_FULL_INSTALL);
                params.setAppPackageName(context.getPackageName());
                params.setSize(apk.length());
                if (Build.VERSION.SDK_INT >= 31) params.setRequireUserAction(PackageInstaller.SessionParams.USER_ACTION_REQUIRED);
                id = installer.createSession(params);
                try (PackageInstaller.Session session = installer.openSession(id)) {
                    try (InputStream in = new FileInputStream(apk); OutputStream out = session.openWrite("base.apk", 0, apk.length())) {
                        byte[] buffer = new byte[65536];
                        int count;
                        while ((count = in.read(buffer)) != -1) out.write(buffer, 0, count);
                        session.fsync(out);
                    }
                    synchronized (UpdateManager.class) {
                        if (!activeScreen || token != screenToken || !foreground || run != generation) {
                            if (run == generation) state = DOWNLOADED;
                            return;
                        }
                        sessionId = id;
                        sessionScreenToken = token;
                        Intent result = new Intent(INSTALL_ACTION).setPackage(context.getPackageName());
                        int flags = PendingIntent.FLAG_UPDATE_CURRENT;
                        if (Build.VERSION.SDK_INT >= 31) flags |= PendingIntent.FLAG_MUTABLE;
                        // Mutable is required for PackageInstaller's status extras;
                        // both pending-user-action and a terminal result are sent.
                        sessionResult = PendingIntent.getBroadcast(context, id, result, flags);
                        session.commit(sessionResult.getIntentSender());
                        committed = true;
                    }
                }
            } catch (Exception e) {
                if (e instanceof InvalidApkException) {
                    synchronized (UpdateManager.class) { readyFile = null; }
                    if (!apk.delete()) Log.w(TAG, "Cannot remove invalid APK");
                }
                fail(run, e instanceof InvalidApkException ? INVALID_APK : INSTALL_ERROR, "Install", e);
            } finally {
                if (!committed && id != -1) {
                    abandon(installer, id);
                    synchronized (UpdateManager.class) { if (sessionId == id) finishSession(); }
                }
            }
        });
    }

    static synchronized void installResult(Activity activity, Intent intent) {
        if (sessionId < 0 || !INSTALL_ACTION.equals(intent.getAction())
            || intent.getIntExtra(PackageInstaller.EXTRA_SESSION_ID, -1) != sessionId) return;
        int result = intent.getIntExtra(PackageInstaller.EXTRA_STATUS, PackageInstaller.STATUS_FAILURE);
        if (result == PackageInstaller.STATUS_PENDING_USER_ACTION) {
            Intent confirmation = intent.getParcelableExtra(Intent.EXTRA_INTENT);
            if (activeScreen && sessionScreenToken == screenToken && foreground && confirmation != null) {
                try { activity.startActivity(confirmation.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)); }
                catch (RuntimeException e) {
                    abandon(context.getPackageManager().getPackageInstaller(), sessionId);
                    finishSession();
                    fail(generation, INSTALL_ERROR, "Confirmation", e);
                }
            } else {
                abandon(context.getPackageManager().getPackageInstaller(), sessionId);
                finishSession();
                state = DOWNLOADED;
            }
        } else if (result == PackageInstaller.STATUS_SUCCESS) {
            finishSession();
            state = UP_TO_DATE;
            readyFile = null;
            release = null;
            worker.execute(UpdateManager::clearCache);
        } else if (result == PackageInstaller.STATUS_FAILURE_ABORTED) {
            finishSession();
            state = DOWNLOADED;
            error = NO_ERROR;
        } else {
            finishSession();
            Log.w(TAG, "Installer status " + result + ": " + intent.getStringExtra(PackageInstaller.EXTRA_STATUS_MESSAGE));
            state = ERROR;
            error = INSTALL_ERROR;
        }
    }

    private static void finishSession() {
        if (sessionResult != null) sessionResult.cancel();
        sessionResult = null;
        sessionId = -1;
    }

    private static boolean canInstall() {
        return Build.VERSION.SDK_INT < 26 || context.getPackageManager().canRequestPackageInstalls();
    }

    private static void abandon(PackageInstaller installer, int id) {
        try { installer.abandonSession(id); }
        catch (RuntimeException e) { Log.w(TAG, "Cannot abandon session", e); }
    }

    private static long code(PackageInfo info) {
        return Build.VERSION.SDK_INT >= 28 ? info.getLongVersionCode() : info.versionCode;
    }

    private static byte[][] signers(PackageInfo info) {
        Signature[] values = Build.VERSION.SDK_INT >= 28
            ? (info.signingInfo == null ? null : info.signingInfo.getApkContentsSigners()) : info.signatures;
        if (values == null) return null;
        byte[][] bytes = new byte[values.length][];
        for (int index = 0; index < values.length; index++) bytes[index] = values[index].toByteArray();
        return bytes;
    }

    private static boolean verify(File apk, JSONObject selected) throws PackageManager.NameNotFoundException, IOException {
        PackageManager pm = context.getPackageManager();
        int flags = Build.VERSION.SDK_INT >= 28 ? PackageManager.GET_SIGNING_CERTIFICATES : PackageManager.GET_SIGNATURES;
        PackageInfo installed = pm.getPackageInfo(context.getPackageName(), flags);
        return verifyApk(pm, context.getPackageName(), installed, apk, selected);
    }

    // Also exercised on a device with real signed, re-signed and corrupt APKs.
    static boolean verifyApk(PackageManager pm, String packageName, PackageInfo installed, File apk, JSONObject selected) throws IOException {
        long size = selected.optLong("size");
        if (size <= 0 || size > UpdateRelease.MAX_APK_SIZE || apk.length() != size
            || !UpdateRelease.matchesDigest(apk, selected.optString("digest"))) return false;
        int flags = Build.VERSION.SDK_INT >= 28 ? PackageManager.GET_SIGNING_CERTIFICATES : PackageManager.GET_SIGNATURES;
        PackageInfo candidate = pm.getPackageArchiveInfo(apk.getAbsolutePath(), flags);
        return candidate != null && packageName.equals(candidate.packageName)
            && (!UpdateRelease.RELEASE_PACKAGE.equals(packageName)
                || (candidate.applicationInfo != null && (candidate.applicationInfo.flags & ApplicationInfo.FLAG_DEBUGGABLE) == 0))
            && ("v" + candidate.versionName).equals(selected.optString("tag"))
            && code(candidate) == selected.optLong("version") && code(candidate) > code(installed)
            && UpdateRelease.sameSigners(signers(installed), signers(candidate));
    }

    private static File cache() {
        return new File(context.getCacheDir(), "update");
    }

    private static void clearCache() {
        File[] files = cache().listFiles();
        if (files != null) for (File file : files) if (file.isFile() && !file.delete()) Log.w(TAG, "Cannot remove cached update file");
    }

    private static void saveRelease(JSONObject value) throws IOException {
        File directory = cache();
        if (!directory.isDirectory() && !directory.mkdirs()) throw new IOException("Cannot create update cache");
        File temp = new File(directory, "release.tmp");
        try (FileOutputStream out = new FileOutputStream(temp)) {
            out.write(value.toString().getBytes(StandardCharsets.UTF_8));
            out.getFD().sync();
        }
        if (!temp.renameTo(new File(directory, "release.json"))) throw new IOException("Cannot keep release metadata");
    }

    private static void restore(int run, String repo) {
        JSONObject saved = null;
        File apk = null;
        boolean interrupted = false;
        try {
            synchronized (UpdateManager.class) { if (run != generation) return; }
            // A process restart revokes old installation intent too. Release
            // the system's staged copies left by an interrupted attempt.
            PackageInstaller installer = context.getPackageManager().getPackageInstaller();
            for (PackageInstaller.SessionInfo session : installer.getMySessions()) abandon(installer, session.getSessionId());
            File metadata = new File(cache(), "release.json");
            if (metadata.isFile()) {
                try (InputStream in = new FileInputStream(metadata)) { saved = new JSONObject(readLimited(in)); }
                if (!repo.equals(saved.optString("repo")) || UpdateRelease.versionCode(saved.optString("tag")) != saved.optLong("version")
                    || saved.optLong("version") <= installedVersion
                    || !UpdateRelease.validAsset(saved.optString("url"), repo, saved.optString("tag"), saved.optLong("size"), saved.optString("digest"))) {
                    saved = null;
                    clearCache();
                } else {
                    File complete = new File(cache(), saved.getString("tag") + ".apk");
                    if (complete.isFile() && verify(complete, saved)) apk = complete;
                    else if (complete.exists() && !complete.delete()) Log.w(TAG, "Cannot remove invalid cached APK");
                    File part = new File(cache(), "download.part");
                    interrupted = part.exists();
                    if (part.exists() && !part.delete()) Log.w(TAG, "Cannot remove interrupted download");
                }
            } else clearCache();
        } catch (Exception e) {
            saved = null;
            apk = null;
            clearCache();
            Log.w(TAG, "Cannot restore update", e);
        }
        synchronized (UpdateManager.class) {
            if (run != generation) return;
            release = saved;
            readyFile = apk;
            total = saved == null ? 0 : saved.optLong("size");
            downloaded = apk == null ? 0 : apk.length();
            state = apk != null ? DOWNLOADED : saved == null ? IDLE : interrupted ? ERROR : AVAILABLE;
            error = interrupted ? DOWNLOAD_ERROR : NO_ERROR;
            restoring = false;
            if (pendingCheck) {
                boolean manual = pendingManual;
                pendingCheck = pendingManual = false;
                check(manual);
            }
        }
    }

    private static boolean trustedUrl(String url) {
        return UpdateRelease.trustedUrl(url);
    }

    private static HttpURLConnection connect(String address) throws IOException {
        for (int redirects = 0; redirects <= 5; redirects++) {
            if (!trustedUrl(address)) throw new IOException("Untrusted update URL");
            HttpURLConnection connection = (HttpURLConnection) new URL(address).openConnection();
            connection.setConnectTimeout(10000);
            connection.setReadTimeout(10000);
            connection.setInstanceFollowRedirects(false);
            connection.setRequestProperty("User-Agent", "fallout2-ce-mobile/" + appVersion);
            connection.setRequestProperty("Accept", "application/vnd.github+json");
            int status;
            try { status = connection.getResponseCode(); }
            catch (IOException e) {
                connection.disconnect();
                throw e;
            }
            if (status == 200) return connection;
            String location = connection.getHeaderField("Location");
            connection.disconnect();
            if ((status == 301 || status == 302 || status == 303 || status == 307 || status == 308) && location != null) {
                address = new URL(new URL(address), location).toExternalForm();
                continue;
            }
            if (status == 403 || status == 429) throw new ServiceException();
            throw new IOException("HTTP " + status);
        }
        throw new IOException("Too many update redirects");
    }

    private static String readResponse(String address) throws IOException {
        HttpURLConnection connection = connect(address);
        try (InputStream in = connection.getInputStream()) { return readLimited(in); }
        finally { connection.disconnect(); }
    }

    private static String readLimited(InputStream in) throws IOException {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        byte[] buffer = new byte[8192];
        long started = System.nanoTime();
        int count;
        while ((count = in.read(buffer)) != -1) {
            if (System.nanoTime() - started > 30 * 1000000000L) throw new IOException("Release metadata timed out");
            if (out.size() + count > UpdateRelease.MAX_METADATA_SIZE) throw new IOException("Release metadata too large");
            out.write(buffer, 0, count);
        }
        return new String(out.toByteArray(), StandardCharsets.UTF_8);
    }

    private static synchronized void fail(int run, int reason, String operation, Exception e) {
        Log.w(TAG, operation + " failed", e);
        if (run == generation) {
            state = ERROR;
            error = reason;
        }
    }

    private static final class ServiceException extends IOException { }
    private static final class InvalidApkException extends IOException { }
}
