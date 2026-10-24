package com.alexbatalov.fallout2ce;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

import java.io.File;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;

public class UpdateReleaseTest {
    @Test
    public void versionsFollowTheApkVersionCode() {
        assertEquals(1030006, UpdateRelease.versionCode("v1.3.0-mobile.6"));
        assertEquals(1030007, UpdateRelease.versionCode("v1.3.0-mobile.7"));
        assertEquals(1040001, UpdateRelease.versionCode("v1.4.0-mobile.1"));
        assertEquals(1030101, UpdateRelease.versionCode("v1.3.1-mobile.1"));
        assertTrue(UpdateRelease.versionCode("v1.3.0-mobile.100") > UpdateRelease.versionCode("v1.3.0-mobile.99"));
    }

    @Test
    public void unknownTagsAndOverflowAreNotUpdates() {
        for (String tag : new String[] { null, "", "1.3.0-mobile.7", "v1.3.0", "v1.3.0-mobile.7-beta", "../v1.3.0-mobile.7", "v-1.3.0-mobile.7", "v999999999999999999999.0.0-mobile.1", "v2147483647.0.0-mobile.1", "v0.0.0-mobile.0" }) {
            assertEquals(String.valueOf(tag), -1, UpdateRelease.versionCode(tag));
        }
    }

    @Test
    public void assetsFollowDeviceAbiPreferenceAndExactSuffix() {
        String[] assets = { "release-arm64.apk.sha256", "release-x86_64.apk", "release-arm64.apk", "release-armv7.apk" };
        assertEquals(2, UpdateRelease.assetIndex(assets, new String[] { "arm64-v8a", "armeabi-v7a" }));
        assertEquals(3, UpdateRelease.assetIndex(assets, new String[] { "armeabi-v7a" }));
        assertEquals(1, UpdateRelease.assetIndex(assets, new String[] { "x86_64", "x86" }));
        assertEquals(-1, UpdateRelease.assetIndex(assets, new String[] { "x86", "unknown" }));
        assertEquals(-1, UpdateRelease.assetIndex(new String[0], new String[] { "arm64-v8a" }));
    }

    @Test
    public void debugPackagesRequireAnExplicitSeparateRepository() {
        assertEquals(UpdateRelease.RELEASE_REPOSITORY, UpdateRelease.repository(UpdateRelease.RELEASE_PACKAGE, "release", false, "someone/test"));
        assertNull(UpdateRelease.repository(UpdateRelease.RELEASE_PACKAGE + ".debug", "debug", true, ""));
        assertNull(UpdateRelease.repository(UpdateRelease.RELEASE_PACKAGE + ".debug", "debug", true, UpdateRelease.RELEASE_REPOSITORY));
        assertNull(UpdateRelease.repository(UpdateRelease.RELEASE_PACKAGE + ".debug", "debug", true, UpdateRelease.RELEASE_REPOSITORY.toUpperCase(java.util.Locale.ROOT)));
        assertNull(UpdateRelease.repository(UpdateRelease.RELEASE_PACKAGE + ".other", "release", false, "someone/test"));
        assertEquals("someone/update-test", UpdateRelease.repository(UpdateRelease.RELEASE_PACKAGE + ".debug", "debug", true, "someone/update-test"));
        for (String repo : new String[] { "https://example.com", "../test", "owner/repo/other", "owner/repo?token=x" }) {
            assertNull(UpdateRelease.repository("test.debug", "debug", true, repo));
        }
    }

    @Test
    public void optimizedPhoneBuildUsesTheTestRepositoryWithoutDebuggableFlag() {
        assertEquals("owner/test", UpdateRelease.repository(UpdateRelease.RELEASE_PACKAGE + ".debug", "phone", false, "owner/test"));
        assertNull(UpdateRelease.repository(UpdateRelease.RELEASE_PACKAGE + ".debug", "phone", false, ""));
        assertNull(UpdateRelease.repository(UpdateRelease.RELEASE_PACKAGE + ".debug", "phone", false, UpdateRelease.RELEASE_REPOSITORY));
    }

    @Test
    public void automaticChecksWaitAnHourButManualChecksAndClockRollbackWork() {
        long hour = 60 * 60 * 1000L;
        assertFalse(UpdateRelease.shouldCheck(hour + 1000, hour, false));
        assertFalse(UpdateRelease.shouldCheck(hour * 2 - 1, hour, false));
        assertTrue(UpdateRelease.shouldCheck(hour * 2, hour, false));
        assertTrue(UpdateRelease.shouldCheck(hour + 1000, hour, true));
        assertTrue(UpdateRelease.shouldCheck(hour - 1000, hour, false));
    }

    @Test
    public void redirectsCannotLeaveHttpsAndGitHubHosts() {
        for (String url : new String[] { "https://api.github.com/repos/a/b/releases/latest", "https://github.com/a/b/releases/download/v1/update.apk", "https://release-assets.githubusercontent.com/file?token=example", "https://objects.githubusercontent.com/file" }) {
            assertTrue(url, UpdateRelease.trustedUrl(url));
        }
        for (String url : new String[] { null, "http://github.com/file", "https://github.com.evil.test/file", "https://evilgithubusercontent.com/file", "https://evil.test/file", "https://user:password@github.com/file", "https://github.com:8443/file", "file:///tmp/update.apk", "garbage", "https://attacker.githubusercontent.com/file", "https://github.com/file#fragment", "https://github.com\\@evil.test/file", "https://github.com/file\r\nInjected: yes", "https://github.com/%zz", "https://github.com@evil.test/file", "https://github.com./file" }) {
            assertFalse(url, UpdateRelease.trustedUrl(url));
        }
    }

    @Test
    public void assetsMustBelongToTheSelectedRepositoryAndTag() {
        String repo = "owner/game";
        String tag = "v1.3.0-mobile.7";
        String url = "https://github.com/" + repo + "/releases/download/" + tag + "/game-arm64.apk";
        String digest = "sha256:" + new String(new char[64]).replace('\0', 'a');
        assertTrue(UpdateRelease.validAsset(url, repo, tag, 1, digest));
        assertTrue(UpdateRelease.validAsset(url, repo, tag, UpdateRelease.MAX_APK_SIZE, digest));
        for (String wrong : new String[] { url.replace("owner/game", "other/game"), url.replace(tag, "v1.3.0-mobile.8"), url + "?download=1", url + "#file", url + ".sha256", url.replace("game-arm64.apk", "../game-arm64.apk"), url.replace("game-arm64.apk", "%2e%2e/game-arm64.apk"), "https://objects.githubusercontent.com/game-arm64.apk", url.replace("github.com", "api.github.com") }) {
            assertFalse(wrong, UpdateRelease.validAsset(wrong, repo, tag, 10, digest));
        }
        for (long size : new long[] { -1, 0, UpdateRelease.MAX_APK_SIZE + 1, Long.MAX_VALUE }) {
            assertFalse(UpdateRelease.validAsset(url, repo, tag, size, digest));
        }
        for (String wrong : new String[] { null, "", "sha256:abc", digest.replace("sha256:", "sha1:"), digest.replace('a', 'z') }) {
            assertFalse(UpdateRelease.validAsset(url, repo, tag, 10, wrong));
        }
    }

    @Test
    public void digestChecksEveryByteIncludingSameLengthCorruption() throws Exception {
        File file = File.createTempFile("update-digest", ".apk");
        try {
            try (FileOutputStream out = new FileOutputStream(file)) { out.write("abc".getBytes(StandardCharsets.UTF_8)); }
            String digest = "sha256:ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
            assertTrue(UpdateRelease.matchesDigest(file, digest));
            assertTrue(UpdateRelease.matchesDigest(file, "sha256:" + digest.substring(7).toUpperCase(java.util.Locale.ROOT)));
            assertFalse(UpdateRelease.matchesDigest(file, null));
            assertFalse(UpdateRelease.matchesDigest(file, "sha256:bad"));
            try (FileOutputStream out = new FileOutputStream(file)) { out.write("abd".getBytes(StandardCharsets.UTF_8)); }
            assertFalse(UpdateRelease.matchesDigest(file, digest));
        } finally {
            assertTrue(file.delete());
        }
    }

    @Test
    public void everySignerMustMatchExactly() {
        byte[][] a = { { 1, 2, 3 }, { 4, 5, 6 } };
        assertTrue(UpdateRelease.sameSigners(a, new byte[][] { { 4, 5, 6 }, { 1, 2, 3 } }));
        assertFalse(UpdateRelease.sameSigners(a, new byte[][] { { 1, 2, 3 }, { 1, 2, 3 } }));
        assertFalse(UpdateRelease.sameSigners(a, new byte[][] { { 1, 2, 3 } }));
        assertFalse(UpdateRelease.sameSigners(a, new byte[][] { { 1, 2, 3 }, { 4, 5, 7 } }));
        assertFalse(UpdateRelease.sameSigners(null, a));
        assertFalse(UpdateRelease.sameSigners(new byte[0][], new byte[0][]));
        assertFalse(UpdateRelease.sameSigners(new byte[][] { {} }, new byte[][] { {} }));
    }
}
