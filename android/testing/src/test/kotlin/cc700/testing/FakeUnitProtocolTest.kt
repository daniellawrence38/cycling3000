package cc700.testing

import cc700.link.LinkTransport

class FakeUnitProtocolTest : ProtocolSuite() {
    override fun open(setup: Setup, mtu: Int): LinkTransport {
        val u = FakeUnit(mtu)
        when (setup) {
            Setup.WITH_RIDE -> {
                val fit = ByteArray(9000) { (it * 31 + 7).toByte() }
                u.rides[1793000000L] = FakeUnit.Ride(1793000000L, fit, 0, 125, 900)
            }
            Setup.RECORDING -> u.state = 1
            Setup.EMPTY -> {}
        }
        return u
    }
}
