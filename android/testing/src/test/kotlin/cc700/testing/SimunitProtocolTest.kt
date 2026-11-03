package cc700.testing

import cc700.link.LinkTransport
import org.junit.jupiter.api.Assumptions.assumeTrue
import java.io.File
import java.net.ServerSocket
import java.nio.file.Files

/** The same suite against the real C core in simunit (build/host/simunit/simunit, made by `make test`). */
class SimunitProtocolTest : ProtocolSuite() {
    private val exe by lazy { File(root, "build/host/simunit/simunit") }
    private val procs = ArrayList<Process>()
    private val dirs = ArrayList<File>()

    private fun freePort() = ServerSocket(0).use { it.localPort }

    override fun open(setup: Setup, mtu: Int): LinkTransport {
        assumeTrue(exe.canExecute(), "simunit not built (make test-c-plain)")
        val dir = Files.createTempDirectory("cc700-suite").toFile().also { dirs += it }
        val unitRoot = File(dir, "CC700")
        val fx = File(root, "core/tests/fixtures")
        if (setup == Setup.WITH_RIDE) {
            val p = ProcessBuilder(exe.path, "--root", unitRoot.path, "--port", "0", "--script", File(fx, "redlight.csv").path,
                "--speed", "0", "--finish", "--exit", "--utc", "1793000000", "--quiet").inheritIO().start()
            check(p.waitFor() == 0) { "scripted ride failed" }
        }
        val port = freePort()
        val args = mutableListOf(exe.path, "--root", unitRoot.path, "--port", "$port", "--mtu", "$mtu", "--utc", "1793000000", "--quiet")
        if (setup == Setup.RECORDING) args += listOf("--script", File(fx, "ride30.csv").path, "--speed", "1")
        procs += ProcessBuilder(args).inheritIO().start()
        repeat(100) {
            try { return SocketTransport(port, mtu) } catch (_: Exception) { Thread.sleep(50) }
        }
        error("simunit did not start")
    }

    override fun cleanup() {
        procs.forEach { it.destroy(); it.waitFor() }
        procs.clear()
        dirs.forEach { it.deleteRecursively() }
        dirs.clear()
    }
}
