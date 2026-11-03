package cc700.sync

import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Assertions.assertFalse
import org.junit.jupiter.api.Assertions.assertTrue
import org.junit.jupiter.api.Test
import org.junit.jupiter.api.io.TempDir
import java.io.File

class FitShiftTest {
    @Test fun crcCheckValue() {
        // FIT SDK CRC-16 (CRC-16/ARC) of "123456789"
        assertEquals(0xBB3D, fitCrc16("123456789".toByteArray()))
    }

    @Test fun shiftsEveryTimeFieldAndKeepsTheFileValid(@TempDir dir: File) {
        Sim.need()
        val unit = File(dir, "CC700")
        Sim.recordRide(unit, null)
        val fit = Sim.rideFiles(unit).single().readBytes()
        assertTrue(fitValid(fit))
        val start = fitStartUtc(fit)!!
        // provisional: 2000-01-01 plus the monotonic clock
        assertTrue(start in 946684800L until 946684800L + 86400, "provisional start $start")
        val target = 1793000000L
        val shifted = shiftFitTimes(fit, target - start)
        assertTrue(fitValid(shifted))
        assertEquals(target, fitStartUtc(shifted))
        val a = fitTimeFields(fit); val b = fitTimeFields(shifted)
        assertEquals(a.size, b.size)
        assertTrue(a.size > 100)
        for (i in a.indices) if (a[i] != 0xFFFFFFFFL) assertEquals(a[i] + target - start, b[i])
        // only the time fields changed
        var diff = 0
        for (i in fit.indices) if (fit[i] != shifted[i]) diff++
        assertTrue(diff <= a.size * 4 + 4)
        // python fitcheck agrees, when available
        val py = ProcessBuilder("python3", File(Sim.root, "tools/fitcheck/fitcheck.py").path,
            File(dir, "s.fit").also { it.writeBytes(shifted) }.path).redirectErrorStream(true).start()
        val out = py.inputStream.bufferedReader().readText()
        assertEquals(0, py.waitFor(), out)
    }

    @Test fun rejectsDamage() {
        assertFalse(fitValid(ByteArray(20)))
    }
}
