package cc700.route.plan

import cc700.route.Pt
import java.math.BigDecimal

/** Writes a minimal GPX 1.1 track that [cc700.route.parseGpx] reads back to the same points. */
object GpxWriter {
    fun write(name: String, pts: List<Pt>): String {
        val sb = StringBuilder()
        sb.append("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n")
        sb.append("<gpx version=\"1.1\" creator=\"CC700 route planner\" xmlns=\"http://www.topografix.com/GPX/1/1\">\n")
        sb.append("  <metadata><name>").append(esc(name)).append("</name></metadata>\n")
        sb.append("  <trk><name>").append(esc(name)).append("</name><trkseg>\n")
        for (p in pts) {
            sb.append("    <trkpt lat=\"").append(deg(p.latE7)).append("\" lon=\"").append(deg(p.lonE7)).append('"')
            val e = p.ele
            if (e != null && e.isFinite()) sb.append("><ele>").append(BigDecimal.valueOf(e).toPlainString()).append("</ele></trkpt>\n")
            else sb.append("/>\n")
        }
        sb.append("  </trkseg></trk>\n</gpx>\n")
        return sb.toString()
    }

    private fun deg(e7: Int) = BigDecimal.valueOf(e7.toLong(), 7).toPlainString()

    private fun esc(s: String) = buildString {
        for (c in s) when (c) {
            '&' -> append("&amp;"); '<' -> append("&lt;"); '>' -> append("&gt;")
            '"' -> append("&quot;"); '\'' -> append("&apos;")
            else -> if (c >= ' ' || c == '\t') append(c)
        }
    }
}
