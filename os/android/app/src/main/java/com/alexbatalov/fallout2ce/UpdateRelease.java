package com.alexbatalov.fallout2ce;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.net.URI;
import java.net.URISyntaxException;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.Arrays;
import java.util.Locale;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

// The release policy, independent of Android and the network.
final class UpdateRelease {
    static final String RELEASE_PACKAGE = "io.github.imbaner.fallout2ce";
    static final String RELEASE_REPOSITORY = "imbaner/fallout2-ce-mobile";
    static final long MAX_APK_SIZE = 128 * 1024 * 1024L;
    static final int MAX_METADATA_SIZE = 1024 * 1024;
    static final int MAX_NOTES_LENGTH = 32768;
    private static final Pattern TAG = Pattern.compile("v([0-9]+)\\.([0-9]+)\\.([0-9]+)-mobile\\.([0-9]+)");

    private UpdateRelease() {
    }

    // The same formula as app/build.gradle. Unknown or overflowing tags
    // aren't an update, rather than an error shown to the player.
    static long versionCode(String tag) {
        if (tag == null || tag.length() > 64) return -1;
        Matcher match = TAG.matcher(tag);
        if (!match.matches()) {
            return -1;
        }
        try {
            long major = Long.parseLong(match.group(1));
            long minor = Long.parseLong(match.group(2));
            long patch = Long.parseLong(match.group(3));
            long mobile = Long.parseLong(match.group(4));
            long code = Math.addExact(Math.multiplyExact(major, 1000000), Math.multiplyExact(minor, 10000));
            code = Math.addExact(code, Math.multiplyExact(patch, 100));
            code = Math.addExact(code, mobile);
            return code > 0 && code <= Integer.MAX_VALUE ? code : -1;
        } catch (NumberFormatException | ArithmeticException e) {
            return -1;
        }
    }

    static String assetSuffix(String abi) {
        if ("arm64-v8a".equals(abi)) return "-arm64.apk";
        if ("armeabi-v7a".equals(abi)) return "-armv7.apk";
        if ("x86".equals(abi)) return "-x86.apk";
        if ("x86_64".equals(abi)) return "-x86_64.apk";
        return null;
    }

    static int assetIndex(String[] names, String[] abis) {
        for (String abi : abis) {
            String suffix = assetSuffix(abi);
            if (suffix == null) continue;
            for (int index = 0; index < names.length; index++) {
                if (names[index] != null && names[index].endsWith(suffix)) return index;
            }
        }
        return -1;
    }

    static String repository(String packageName, String buildType, boolean debuggable, String override) {
        if (RELEASE_PACKAGE.equals(packageName)) return RELEASE_REPOSITORY;
        // A debug APK must never offer the public APK with another package
        // and signing key. Only an explicitly chosen test repository works.
        // `phone` uses optimized release code and DEBUG=false, but the test
        // package and debug signing key (app/build.gradle).
        boolean testBuild = debuggable || "phone".equals(buildType);
        if (testBuild && override != null && !RELEASE_REPOSITORY.equalsIgnoreCase(override)
            && override.matches("[A-Za-z0-9_-][A-Za-z0-9_.-]*/[A-Za-z0-9_-][A-Za-z0-9_.-]*")) {
            return override;
        }
        return null;
    }

    static boolean shouldCheck(long now, long last, boolean manual) {
        return manual || now < last || now - last >= 60 * 60 * 1000L;
    }

    static boolean trustedUrl(String url) {
        try {
            if (url == null || url.length() > 4096) return false;
            URI parsed = new URI(url);
            String host = parsed.getHost();
            if (host == null) return false;
            host = host.toLowerCase(Locale.ROOT);
            return "https".equals(parsed.getScheme()) && parsed.getRawUserInfo() == null && parsed.getRawFragment() == null
                && (parsed.getPort() == -1 || parsed.getPort() == 443)
                && ("api.github.com".equals(host) || "github.com".equals(host)
                    || "release-assets.githubusercontent.com".equals(host) || "objects.githubusercontent.com".equals(host));
        } catch (URISyntaxException e) { return false; }
    }

    static boolean validAsset(String url, String repository, String tag, long size, String digest) {
        if (versionCode(tag) < 0 || size <= 0 || size > MAX_APK_SIZE || !validDigest(digest) || !trustedUrl(url)) return false;
        URI parsed = URI.create(url);
        String prefix = "/" + repository + "/releases/download/" + tag + "/";
        String path = parsed.getRawPath();
        // Only an asset of this repository and release may start a download.
        // CDN URLs are accepted only as HTTPS redirects of that request.
        return "github.com".equalsIgnoreCase(parsed.getHost()) && parsed.getRawQuery() == null
            && path != null && path.startsWith(prefix) && path.substring(prefix.length()).matches("[A-Za-z0-9_.-]+\\.apk");
    }

    private static boolean validDigest(String digest) {
        return digest != null && digest.matches("sha256:[0-9a-fA-F]{64}");
    }

    static boolean matchesDigest(File file, String expected) throws IOException {
        if (!validDigest(expected)) return false;
        MessageDigest hash;
        try { hash = MessageDigest.getInstance("SHA-256"); }
        catch (NoSuchAlgorithmException e) { throw new IllegalStateException(e); }
        try (InputStream in = new FileInputStream(file)) {
            byte[] buffer = new byte[65536];
            int count;
            while ((count = in.read(buffer)) != -1) hash.update(buffer, 0, count);
        }
        byte[] bytes = new byte[32];
        for (int index = 0; index < bytes.length; index++) {
            bytes[index] = (byte) Integer.parseInt(expected.substring(7 + index * 2, 9 + index * 2), 16);
        }
        return MessageDigest.isEqual(bytes, hash.digest());
    }

    static boolean sameSigners(byte[][] installed, byte[][] downloaded) {
        if (installed == null || downloaded == null || installed.length == 0 || installed.length != downloaded.length) return false;
        boolean[] used = new boolean[downloaded.length];
        for (byte[] signer : installed) {
            if (signer == null || signer.length == 0) return false;
            boolean found = false;
            for (int index = 0; index < downloaded.length; index++) {
                if (!used[index] && Arrays.equals(signer, downloaded[index])) {
                    used[index] = true;
                    found = true;
                    break;
                }
            }
            if (!found) return false;
        }
        return true;
    }
}
