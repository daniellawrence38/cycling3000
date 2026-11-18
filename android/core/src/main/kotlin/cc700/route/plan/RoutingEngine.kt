package cc700.route.plan

import cc700.route.Pt

/** The kind of riding a plan is for. It picks the routing profile and how unpaved surface is scored. */
enum class BikeProfile { ROAD, GRAVEL, MTB }

/** A run of route points `[from, to]` (indices into [Candidate.points]) sharing one value of an
 * attribute such as way type or surface, as the routing service reports it. */
data class Span(val from: Int, val to: Int, val value: Int)

/**
 * One generated route. Geometry always comes from the routing service; nothing here is invented.
 * [wayTypes], [surfaces] and [steepness] use OpenRouteService's extra-info codes (see [WayType] and
 * [Surface]); an engine that has no such data leaves them empty and scores neutrally.
 */
data class Candidate(
    val id: String,
    val points: List<Pt>,
    val distanceM: Double,
    val ascentM: Double,
    val durationS: Double,
    val wayTypes: List<Span> = emptyList(),
    val surfaces: List<Span> = emptyList(),
    val steepness: List<Span> = emptyList(),
)

/** A routing service that can make a loop of roughly a given length from a start point. Kept as an
 * interface so an offline engine (BRouter) or another service can stand in later. */
interface RoutingEngine {
    /** A loop starting and ending at [start], about [lengthM] long; [seed] picks one of many loops. */
    fun roundTrip(start: Pt, lengthM: Int, seed: Int, profile: BikeProfile, favourPaths: Boolean): Candidate

    /** A route through [waypoints] in order (the caller closes a loop by repeating the start). */
    fun route(waypoints: List<Pt>, profile: BikeProfile, favourPaths: Boolean): Candidate
}

/** A named place near the start, for routes "via the reservoir" or "with a café stop". */
data class Place(val name: String, val pt: Pt, val distanceKm: Double?)

/** Searches places by name or kind near a point. */
interface PlaceSearch {
    fun searchPlaces(query: String, near: Pt, radiusKm: Double, max: Int = 5): List<Place>
}

/** Errors a [RoutingEngine] reports. Network failures surface as `java.io.IOException`. */
sealed class RoutingError(msg: String) : Exception(msg) {
    /** No key set, or the service refused it: ask for a key in Settings. */
    class NeedsKey : RoutingError("the routing service needs a valid API key")
    /** The daily or per-minute quota is used up. */
    class RateLimited : RoutingError("the routing service's quota is used up; try again later")
    /** The service could not make a route for this request (for example no roads near the start). */
    class NoRoute(msg: String) : RoutingError(msg)
    class Failed(val code: Int, msg: String) : RoutingError(msg)
}

/** OpenRouteService `waytype` extra-info codes. */
object WayType {
    const val UNKNOWN = 0; const val STATE_ROAD = 1; const val ROAD = 2; const val STREET = 3
    const val PATH = 4; const val TRACK = 5; const val CYCLEWAY = 6; const val FOOTWAY = 7
    const val STEPS = 8; const val FERRY = 9; const val CONSTRUCTION = 10
}

/** OpenRouteService `surface` extra-info codes, grouped the way the scorer needs them. */
object Surface {
    private val paved = setOf(1, 3, 4, 5, 6, 7, 14)
    private val unpaved = setOf(2, 8, 9, 10, 11, 12, 13, 15, 16, 17, 18)
    fun isPaved(code: Int) = code in paved
    fun isUnpaved(code: Int) = code in unpaved
}
