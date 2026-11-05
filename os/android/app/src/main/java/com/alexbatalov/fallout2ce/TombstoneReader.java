package com.alexbatalov.fallout2ce;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

// What the system kept of a native crash (Android 12+): the record
// `ApplicationExitInfo.getTraceInputStream` gives, debuggerd's Tombstone
// message (system/core/debuggerd/proto/tombstone.proto). Only what a problem
// report may carry (error_report.h) is taken from it: the signal, the
// system's words on the cause, the abort message and the failed thread's
// frames as offsets in their libraries. Registers, memory, paths and the
// other threads stay out. The offsets are read with the build's unstripped
// library, found by its Build ID.
final class TombstoneReader {
    // The largest record read: the frames come before its memory maps and
    // logs, a longer one is read as far as this.
    private static final int MAX_RECORD = 8 * 1024 * 1024;
    private static final int MAX_FRAMES = 20;
    private static final int MAX_LINE = 110;
    private static final int BUILD_ID_SHOWN = 8;
    private static final String GAME_LIBRARY = "libfallout2-ce.so";

    private static final int WIRE_VARINT = 0;
    private static final int WIRE_FIXED64 = 1;
    private static final int WIRE_BYTES = 2;
    private static final int WIRE_FIXED32 = 5;

    private static final int TOMBSTONE_TID = 6;
    private static final int TOMBSTONE_SIGNAL = 10;
    private static final int TOMBSTONE_ABORT_MESSAGE = 14;
    private static final int TOMBSTONE_CAUSES = 15;
    private static final int TOMBSTONE_THREADS = 16;
    private static final int SIGNAL_NAME = 2;
    private static final int SIGNAL_CODE_NAME = 4;
    private static final int SIGNAL_HAS_FAULT_ADDRESS = 8;
    private static final int SIGNAL_FAULT_ADDRESS = 9;
    private static final int CAUSE_TEXT = 1;
    private static final int THREADS_KEY = 1;
    private static final int THREADS_VALUE = 2;
    private static final int THREAD_BACKTRACE = 4;
    private static final int FRAME_REL_PC = 1;
    private static final int FRAME_FILE_NAME = 6;
    private static final int FRAME_BUILD_ID = 8;

    private TombstoneReader() {
    }

    private static final class Frame {
        long offset;
        String file = "";
        String buildId = "";
    }

    private static final class Record {
        long tid;
        String signal = "";
        String signalCode = "";
        boolean hasAddress;
        long address;
        String abortMessage = "";
        String cause = "";
        final Map<Long, List<Frame>> threads = new HashMap<>();
    }

    // Protocol buffers' wire format, within [end] of [data]. What is broken
    // or cut short ends with an IOException: what was read before it stays.
    private static final class Reader {
        private final byte[] data;
        private final int end;
        private int position;
        int wireType;

        Reader(byte[] data, int start, int end) {
            this.data = data;
            this.position = start;
            this.end = end;
        }

        boolean more() {
            return position < end;
        }

        // The next field's number.
        int field() throws IOException {
            long tag = varint();
            wireType = (int) (tag & 7);
            return (int) (tag >>> 3);
        }

        long varint() throws IOException {
            long value = 0;
            for (int shift = 0; shift < 64; shift += 7) {
                if (position >= end) {
                    throw new IOException("A record cut short");
                }
                byte next = data[position++];
                value |= (long) (next & 0x7F) << shift;
                if (next >= 0) {
                    return value;
                }
            }
            throw new IOException("A broken number");
        }

        // A field of bytes: a reader of its contents, this one goes past it.
        Reader message() throws IOException {
            if (wireType != WIRE_BYTES) {
                throw new IOException("Not a field of bytes");
            }
            long length = varint();
            if (length < 0 || length > end - position) {
                throw new IOException("A record cut short");
            }
            int start = position;
            position += (int) length;
            return new Reader(data, start, position);
        }

        String text() throws IOException {
            Reader contents = message();
            return new String(data, contents.position, contents.end - contents.position, StandardCharsets.UTF_8);
        }

        void skip() throws IOException {
            switch (wireType) {
            case WIRE_VARINT:
                varint();
                break;
            case WIRE_FIXED64:
                skipBytes(8);
                break;
            case WIRE_BYTES:
                message();
                break;
            case WIRE_FIXED32:
                skipBytes(4);
                break;
            default:
                throw new IOException("An unknown field type");
            }
        }

        private void skipBytes(int length) throws IOException {
            if (length > end - position) {
                throw new IOException("A record cut short");
            }
            position += length;
        }
    }

    static byte[] read(InputStream stream) throws IOException {
        ByteArrayOutputStream result = new ByteArrayOutputStream();
        byte[] buffer = new byte[64 * 1024];
        int length;
        while (result.size() < MAX_RECORD && (length = stream.read(buffer)) != -1) {
            result.write(buffer, 0, length);
        }
        return result.toByteArray();
    }

    // The report's lines of the [data] record (one per line, no longer than
    // `MAX_LINE`); "" when nothing of it can be read.
    static String reportLines(byte[] data) {
        Record record = new Record();
        try {
            readRecord(new Reader(data, 0, data.length), record);
        } catch (IOException e) {
            // What was read before the break still tells.
        }

        StringBuilder lines = new StringBuilder();
        if (!record.signal.isEmpty()) {
            lines.append("Signal: ").append(record.signal);
            if (!record.signalCode.isEmpty()) {
                lines.append(' ').append(record.signalCode);
            }
            if (record.hasAddress) {
                lines.append(", address 0x").append(Long.toHexString(record.address));
            }
            lines.append('\n');
        }
        appendText(lines, "Cause: ", record.cause);
        appendText(lines, "Abort: ", record.abortMessage);
        appendStack(lines, record.threads.get(record.tid));
        return lines.toString();
    }

    private static void appendText(StringBuilder lines, String title, String text) {
        String line = text.replaceAll("\\s+", " ").trim();
        if (!line.isEmpty()) {
            int room = MAX_LINE - title.length();
            lines.append(title).append(line.length() > room ? line.substring(0, room) : line).append('\n');
        }
    }

    // "Stack a1b2c3d4: +0x1a2b3c libc.so+0x5e0f0": offsets in the game's
    // library (its Build ID's start) and, by name, in others. What called
    // the game (the system's way to it) tells nothing.
    private static void appendStack(StringBuilder lines, List<Frame> frames) {
        if (frames == null || frames.isEmpty()) {
            return;
        }

        String buildId = "";
        int count = frames.size();
        int lastGameFrame = -1;
        for (int index = 0; index < frames.size(); index++) {
            Frame frame = frames.get(index);
            if (baseName(frame.file).equals(GAME_LIBRARY)) {
                lastGameFrame = index;
                if (buildId.isEmpty()) {
                    buildId = frame.buildId.substring(0, Math.min(frame.buildId.length(), BUILD_ID_SHOWN));
                }
            }
        }
        if (lastGameFrame != -1) {
            count = lastGameFrame + 1;
        }

        StringBuilder line = new StringBuilder(buildId.isEmpty() ? "Stack:" : "Stack " + buildId + ":");
        for (int index = 0; index < count && index < MAX_FRAMES; index++) {
            String name = frameName(frames.get(index));
            if (line.length() + 1 + name.length() > MAX_LINE) {
                lines.append(line).append('\n');
                line = new StringBuilder("Stack:");
            }
            line.append(' ').append(name);
        }
        lines.append(line).append('\n');
    }

    private static String frameName(Frame frame) {
        String library = baseName(frame.file);
        String offset = "+0x" + Long.toHexString(frame.offset);
        return library.equals(GAME_LIBRARY) ? offset : library + offset;
    }

    // "/apex/.../libc.so" -> "libc.so", ".../base.apk!libgame.so" ->
    // "libgame.so": no paths in a report.
    private static String baseName(String path) {
        int start = Math.max(path.lastIndexOf('/'), path.lastIndexOf('!')) + 1;
        return path.substring(start).toLowerCase(Locale.ROOT);
    }

    private static void readRecord(Reader reader, Record record) throws IOException {
        while (reader.more()) {
            switch (reader.field()) {
            case TOMBSTONE_TID:
                record.tid = reader.varint();
                break;
            case TOMBSTONE_SIGNAL:
                readSignal(reader.message(), record);
                break;
            case TOMBSTONE_ABORT_MESSAGE:
                record.abortMessage = reader.text();
                break;
            case TOMBSTONE_CAUSES:
                readCause(reader.message(), record);
                break;
            case TOMBSTONE_THREADS:
                readThreadsEntry(reader.message(), record);
                break;
            default:
                reader.skip();
                break;
            }
        }
    }

    private static void readSignal(Reader reader, Record record) throws IOException {
        while (reader.more()) {
            switch (reader.field()) {
            case SIGNAL_NAME:
                record.signal = reader.text();
                break;
            case SIGNAL_CODE_NAME:
                record.signalCode = reader.text();
                break;
            case SIGNAL_HAS_FAULT_ADDRESS:
                record.hasAddress = reader.varint() != 0;
                break;
            case SIGNAL_FAULT_ADDRESS:
                record.address = reader.varint();
                break;
            default:
                reader.skip();
                break;
            }
        }
    }

    // The first one: the likeliest, as the system lists them.
    private static void readCause(Reader reader, Record record) throws IOException {
        while (reader.more()) {
            if (reader.field() == CAUSE_TEXT && record.cause.isEmpty()) {
                record.cause = reader.text();
            } else {
                reader.skip();
            }
        }
    }

    private static void readThreadsEntry(Reader reader, Record record) throws IOException {
        long tid = 0;
        List<Frame> frames = new ArrayList<>();
        while (reader.more()) {
            switch (reader.field()) {
            case THREADS_KEY:
                tid = reader.varint();
                break;
            case THREADS_VALUE:
                readThread(reader.message(), frames);
                break;
            default:
                reader.skip();
                break;
            }
        }
        record.threads.put(tid, frames);
    }

    private static void readThread(Reader reader, List<Frame> frames) throws IOException {
        while (reader.more()) {
            if (reader.field() == THREAD_BACKTRACE) {
                frames.add(readFrame(reader.message()));
            } else {
                reader.skip();
            }
        }
    }

    private static Frame readFrame(Reader reader) throws IOException {
        Frame frame = new Frame();
        while (reader.more()) {
            switch (reader.field()) {
            case FRAME_REL_PC:
                frame.offset = reader.varint();
                break;
            case FRAME_FILE_NAME:
                frame.file = reader.text();
                break;
            case FRAME_BUILD_ID:
                frame.buildId = reader.text();
                break;
            default:
                reader.skip();
                break;
            }
        }
        return frame;
    }
}
