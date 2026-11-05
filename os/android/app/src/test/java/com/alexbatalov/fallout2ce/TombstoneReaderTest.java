package com.alexbatalov.fallout2ce;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.Random;

public class TombstoneReaderTest {
    private static final String GAME = "/data/app/~~abc==/io.github.imbaner.fallout2ce-x==/lib/arm64/libfallout2-ce.so";
    private static final String LIBC = "/apex/com.android.runtime/lib64/bionic/libc.so";
    private static final String ART = "/apex/com.android.art/lib64/libart.so";
    private static final String BUILD_ID = "a1b2c3d4e5f60718";

    // Protocol buffers' wire format, for the records of these tests.
    private static final class Message {
        final ByteArrayOutputStream bytes = new ByteArrayOutputStream();

        private void varint(long value) {
            while ((value & ~0x7FL) != 0) {
                bytes.write((int) (value & 0x7F) | 0x80);
                value >>>= 7;
            }
            bytes.write((int) value);
        }

        Message number(int field, long value) {
            varint((long) field << 3);
            varint(value);
            return this;
        }

        Message data(int field, byte[] value) {
            varint((long) field << 3 | 2);
            varint(value.length);
            bytes.write(value, 0, value.length);
            return this;
        }

        Message text(int field, String value) {
            return data(field, value.getBytes(StandardCharsets.UTF_8));
        }

        Message message(int field, Message value) {
            return data(field, value.bytes.toByteArray());
        }

        // A field of another type, as a record of a newer system may have.
        Message fixed(int field, int type, int length) {
            varint((long) field << 3 | type);
            bytes.write(new byte[length], 0, length);
            return this;
        }

        byte[] toBytes() {
            return bytes.toByteArray();
        }
    }

    private static Message frame(long offset, String file, String buildId) {
        return new Message().number(1, offset).number(2, offset + 0x7000000000L).text(6, file).text(8, buildId);
    }

    private static Message thread(long tid, Message... frames) {
        Message thread = new Message().number(1, tid).text(2, "SDLThread");
        // Registers and memory: never in a report.
        thread.message(3, new Message().text(1, "x0").number(2, 0xDEADBEEFL));
        for (Message frame : frames) {
            thread.message(4, frame);
        }
        thread.message(5, new Message().text(1, "x0").data(4, new byte[] { 1, 2, 3 }));
        return new Message().number(1, tid).message(2, thread);
    }

    private static Message signal(String name, String code, boolean hasAddress, long address) {
        Message signal = new Message().number(1, 11).text(2, name).number(3, 1).text(4, code);
        if (hasAddress) {
            signal.number(8, 1).number(9, address);
        }
        return signal;
    }

    private static Message crash() {
        return new Message()
            .number(1, 3)
            .text(2, "google/sdk_gphone64_arm64")
            .number(5, 100)
            .number(6, 101)
            .text(9, "io.github.imbaner.fallout2ce")
            .message(10, signal("SIGSEGV", "SEGV_MAPERR", true, 0x10))
            .text(14, "Scudo ERROR: corrupted chunk header")
            .message(15, new Message().text(1, "null pointer dereference"))
            .message(15, new Message().text(1, "second cause"))
            .message(16, thread(100, frame(0x111, GAME, BUILD_ID)))
            .message(16, thread(101, frame(0x5e0f0, LIBC, "ffff"), frame(0x1a2b3c, GAME, BUILD_ID), frame(0x1a0f00, GAME, BUILD_ID), frame(0x362774, ART, "eeee"), frame(0x1fe704, ART, "eeee")))
            .message(17, new Message().text(1, GAME));
    }

    @Test
    public void aRecordGivesTheFailedThreadsPlaceAndNothingElse() {
        assertEquals(
            "Signal: SIGSEGV SEGV_MAPERR, address 0x10\n"
                + "Cause: null pointer dereference\n"
                + "Abort: Scudo ERROR: corrupted chunk header\n"
                + "Stack a1b2c3d4: libc.so+0x5e0f0 +0x1a2b3c +0x1a0f00\n",
            TombstoneReader.reportLines(crash().toBytes()));
    }

    @Test
    public void aSignalWithoutAnAddressOrAMessage() {
        byte[] record = new Message()
            .number(6, 7)
            .message(10, signal("SIGABRT", "", false, 0))
            .message(16, thread(7, frame(0x20, GAME, BUILD_ID)))
            .toBytes();
        assertEquals("Signal: SIGABRT\nStack a1b2c3d4: +0x20\n", TombstoneReader.reportLines(record));
    }

    @Test
    public void aCrashOutsideTheGamesLibraryHasNoBuildId() {
        byte[] record = new Message()
            .number(6, 7)
            .message(16, thread(7, frame(0x20, LIBC, "ffff")))
            .toBytes();
        assertEquals("Stack: libc.so+0x20\n", TombstoneReader.reportLines(record));
    }

    @Test
    public void aLibraryInsideTheApkGoesByItsName() {
        byte[] record = new Message()
            .number(6, 7)
            .message(16, thread(7, frame(0x20, "/data/app/x/base.apk!libfallout2-ce.so", BUILD_ID)))
            .toBytes();
        assertEquals("Stack a1b2c3d4: +0x20\n", TombstoneReader.reportLines(record));
    }

    @Test
    public void aDeepStackIsCutAndItsLinesStayShort() {
        Message[] frames = new Message[100];
        for (int index = 0; index < frames.length; index++) {
            frames[index] = frame(0x1a0000 + index, index % 2 == 0 ? GAME : "/system/lib64/libandroid_runtime.so", BUILD_ID);
        }
        byte[] record = new Message().number(6, 7).message(16, thread(7, frames)).toBytes();
        String lines = TombstoneReader.reportLines(record);
        int count = 0;
        for (String line : lines.split("\n")) {
            assertTrue(line, line.startsWith("Stack"));
            assertTrue(line, line.length() <= 110);
            count += line.split(" ").length - (line.startsWith("Stack:") ? 1 : 2);
        }
        assertEquals(20, count);
    }

    @Test
    public void aLongMessageIsCutToOneLine() {
        StringBuilder message = new StringBuilder("first\nsecond");
        for (int index = 0; index < 300; index++) {
            message.append(" word");
        }
        byte[] record = new Message().text(14, message.toString()).toBytes();
        String lines = TombstoneReader.reportLines(record);
        assertTrue(lines, lines.startsWith("Abort: first second word"));
        assertEquals(1, lines.split("\n").length);
        assertTrue(lines, lines.length() <= 111);
    }

    @Test
    public void noPathsInAReport() {
        String lines = TombstoneReader.reportLines(crash().toBytes());
        assertFalse(lines, lines.contains("/"));
        assertFalse(lines, lines.contains("imbaner"));
        assertFalse(lines, lines.contains("deadbeef"));
    }

    @Test
    public void fieldsOfNewerSystemsAreSkipped() {
        Message record = new Message()
            .fixed(90, 1, 8)
            .fixed(91, 5, 4)
            .number(92, 5)
            .text(93, "new")
            .number(6, 7)
            .message(16, thread(7, frame(0x20, GAME, BUILD_ID)));
        assertEquals("Stack a1b2c3d4: +0x20\n", TombstoneReader.reportLines(record.toBytes()));
    }

    @Test
    public void aRecordCutAnywhereGivesWhatWasRead() {
        byte[] record = crash().toBytes();
        boolean signalWithoutStack = false;
        for (int length = 0; length < record.length; length++) {
            byte[] part = new byte[length];
            System.arraycopy(record, 0, part, 0, length);
            String lines = TombstoneReader.reportLines(part);
            signalWithoutStack |= lines.startsWith("Signal: ") && !lines.contains("Stack");
        }
        assertTrue(signalWithoutStack);
    }

    @Test
    public void brokenRecordsGiveNothingAndNeverFail() {
        assertEquals("", TombstoneReader.reportLines(new byte[0]));
        // A length past the end, a number that never ends, an unknown type.
        assertEquals("", TombstoneReader.reportLines(new byte[] { 0x52, 0x7F }));
        assertEquals("", TombstoneReader.reportLines(new byte[] { (byte) 0xFF, (byte) 0xFF, (byte) 0xFF, (byte) 0xFF, (byte) 0xFF, (byte) 0xFF, (byte) 0xFF, (byte) 0xFF, (byte) 0xFF, (byte) 0xFF, (byte) 0xFF }));
        assertEquals("", TombstoneReader.reportLines(new byte[] { 0x0B }));
        // A length that doesn't fit an int.
        assertEquals("", TombstoneReader.reportLines(new byte[] { 0x52, (byte) 0xFF, (byte) 0xFF, (byte) 0xFF, (byte) 0xFF, (byte) 0xFF, (byte) 0xFF, (byte) 0xFF, (byte) 0xFF, 0x7F }));

        Random random = new Random(20261105);
        byte[] good = crash().toBytes();
        for (int round = 0; round < 20000; round++) {
            byte[] record = good.clone();
            for (int change = 0; change < 1 + round % 8; change++) {
                record[random.nextInt(record.length)] = (byte) random.nextInt(256);
            }
            TombstoneReader.reportLines(record);
        }
        for (int round = 0; round < 20000; round++) {
            byte[] record = new byte[random.nextInt(200)];
            random.nextBytes(record);
            TombstoneReader.reportLines(record);
        }
    }

    @Test
    public void readingStopsAtTheLimit() throws IOException {
        byte[] large = new byte[9 * 1024 * 1024];
        byte[] read = TombstoneReader.read(new ByteArrayInputStream(large));
        assertTrue(read.length >= 8 * 1024 * 1024);
        assertTrue(read.length < large.length);
    }
}
