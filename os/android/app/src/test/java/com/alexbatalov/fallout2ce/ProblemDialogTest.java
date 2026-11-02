package com.alexbatalov.fallout2ce;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

public class ProblemDialogTest {
    @Test
    public void aLongReportFitsTheLinksOldestEventsFirst() {
        String text = "code\nLast events:\n- one\n- two\n";
        String fitted = "code\nLast events:\n- two\n";
        assertEquals(fitted, ProblemDialog.fittedText(text, ProblemDialog.encode(fitted).length()));
        assertEquals("one\n", ProblemDialog.fittedText("one\ntwo\n", ProblemDialog.encode("one\n").length()));
        assertEquals(text, ProblemDialog.fittedText(text, ProblemDialog.URL_LIMIT));

        StringBuilder report = new StringBuilder("Код: ошибка\nLast events:\n");
        for (int index = 0; index < 80; index++) {
            report.append("- карта и что-то ещё, строка события\n");
        }
        String result = ProblemDialog.fittedText(report.toString(), ProblemDialog.URL_LIMIT - 100);
        assertTrue(ProblemDialog.encode(result).length() <= ProblemDialog.URL_LIMIT - 100);
        assertTrue(result.startsWith("Код: ошибка\nLast events:\n- "));
    }
}
