package cc700.sync

import org.junit.jupiter.api.Assumptions.assumeTrue
import java.io.File
import java.net.ServerSocket

/** simunit helpers (build/host/simunit/simunit, made by `make test-c-plain`). */
object Sim {
    val root = File(System.getProperty("cc700.root"))
    val exe = File(root, "build/host/simunit/simunit")
    val fixtures = File(root, "core/tests/fixtures")
    const val UTC = 1793000000L

    fun need() = assumeTrue(exe.canExecute(), "simunit not built (make test-c-plain)")

    /** Records the red-light fixture ride into [unitRoot]; [utc] null records without UTC (time unknown). */
    fun recordRide(unitRoot: File, utc: Long?) {
        val p = ProcessBuilder(exe.path, "--root", unitRoot.path, "--port", "0", "--script", File(fixtures, "redlight.csv").path,
            "--speed", "0", "--finish", "--exit", "--utc", utc?.toString() ?: "none", "--quiet").inheritIO().start()
        check(p.waitFor() == 0) { "scripted ride failed" }
    }

    fun freePort() = ServerSocket(0).use { it.localPort }

    fun serve(unitRoot: File, port: Int): Process =
        ProcessBuilder(exe.path, "--root", unitRoot.path, "--port", "$port", "--utc", "$UTC", "--quiet").inheritIO().start()

    fun rideFiles(unitRoot: File) = File(unitRoot, "rides").listFiles { f -> f.name.endsWith(".fit") }!!.sortedBy { it.name }
}
