package cc700.app.ui

import android.Manifest
import android.app.DatePickerDialog
import android.app.TimePickerDialog
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyListScope
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.RadioButton
import androidx.compose.material3.Slider
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import cc700.app.data.ConnectionMode
import cc700.app.data.Here
import cc700.link.ObjInfo
import cc700.route.cr1HasProfile
import cc700.route.plan.BikeProfile
import cc700.route.plan.Scored
import cc700.route.plan.defaultSpeedKmh
import kotlin.math.roundToInt
import cc700.sync.ElevationSource
import cc700.sync.LinkState
import cc700.sync.RideRecord
import cc700.sync.RideState
import cc700.sync.RouteImport
import cc700.sync.WorkoutImport
import cc700.sync.importWorkout
import cc700.sync.previewSteps
import java.time.Instant
import java.time.LocalDateTime
import java.time.ZoneId
import java.time.format.DateTimeFormatter

// ------------------------------------------------------------------ building blocks
private fun LazyListScope.header(text: String) = item {
    Text(text.uppercase(), color = Tok.muted, fontSize = 12.sp, fontWeight = FontWeight.SemiBold,
        modifier = Modifier.padding(start = 16.dp, top = 16.dp, bottom = 6.dp))
}

@Composable
private fun RowItem(title: String, sub: String? = null, trailing: @Composable () -> Unit = {}, onClick: (() -> Unit)? = null) = Column {
    Row(
        Modifier.fillMaxWidth().heightIn(min = 46.dp).background(Tok.panel)
            .let { if (onClick != null) it.clickable(onClick = onClick) else it }
            .padding(horizontal = 16.dp, vertical = 8.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Column(Modifier.weight(1f)) {
            Text(title, color = Tok.text)
            if (sub != null) Text(sub, color = Tok.muted, fontSize = 13.sp)
        }
        trailing()
    }
    HorizontalDivider(color = Tok.divider)
}

@Composable
private fun Buttons(content: @Composable () -> Unit) {
    Row(Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 8.dp), horizontalArrangement = Arrangement.spacedBy(8.dp)) { content() }
}

@Composable
private fun Note(text: String) = Text(text, color = Tok.muted, fontSize = 13.sp, modifier = Modifier.padding(horizontal = 16.dp, vertical = 4.dp))

/** Destructive actions ask first, with the safe choice as the default (spec 12). */
@Composable
private fun Confirm(title: String, text: String, yes: String, onYes: () -> Unit, onDismiss: () -> Unit) {
    AlertDialog(onDismissRequest = onDismiss, title = { Text(title) }, text = { Text(text) },
        confirmButton = { TextButton(onClick = { onYes(); onDismiss() }) { Text(yes, color = Tok.bad) } },
        dismissButton = { Button(onClick = onDismiss) { Text("Cancel") } })
}

// ------------------------------------------------------------------ 1. Unit
@Composable
fun UnitScreen(m: AppModel) {
    val st by m.status.collectAsState()
    val found by m.found.collectAsState()
    val settings by m.settings.collectAsState()
    var forget by remember { mutableStateOf(false) }
    var pairTarget by remember { mutableStateOf<cc700.sync.FoundUnit?>(null) }
    LazyColumn {
        header("Unit")
        item {
            val stateText = when (st.state) {
                LinkState.NOT_PAIRED -> "Not paired"
                LinkState.DISCONNECTED -> if (st.needsRepair) "Not recognised" else "Not connected"
                LinkState.CONNECTING -> "Connecting"
                LinkState.CONNECTED -> when (st.unitState) { 1 -> "Connected, recording"; 2 -> "Connected, paused"; else -> "Connected" }
            }
            val colour = when (st.state) { LinkState.CONNECTED -> Tok.ok; LinkState.CONNECTING -> Tok.warn; else -> Tok.muted }
            RowItem(st.unit?.name ?: "No unit", st.error, trailing = { Chip(stateText, colour) })
        }
        st.hello?.let { h ->
            item { RowItem("Battery", "${h.battery} %") }
            item { RowItem("Firmware", "v${h.fw}") }
            item { RowItem("Free slots", "${h.workoutFree} workouts, ${h.routeFree} routes") }
        }
        item { RowItem("FTP", st.ftp?.let { "$it W" } ?: "Read when connected") }
        item {
            Buttons {
                when (st.state) {
                    LinkState.NOT_PAIRED -> Button(onClick = { m.scan() }) { Text("Add unit") }
                    // Once the unit has refused this phone, Connect would only pop pairing requests.
                    LinkState.DISCONNECTED -> if (!st.needsRepair) Button(onClick = { m.connect() }) { Text("Connect") }
                    LinkState.CONNECTED -> OutlinedButton(onClick = { m.disconnect() }) { Text("Disconnect") }
                    LinkState.CONNECTING -> {}
                }
                if (st.unit != null) OutlinedButton(onClick = { forget = true }) { Text("Forget unit") }
            }
        }
        if (st.needsRepair) item {
            // The row above already shows REPAIR_TEXT as the error; this says how.
            Note("To pair again: Forget unit here, then on the unit open Menu, Phone, PAIR and choose Add unit " +
                "within 60 s. (If the unit still lists this phone, forget it there too: Phone, menu, Forget phone.)")
        }
        if (st.state == LinkState.NOT_PAIRED) item {
            Note(if (settings.mode == ConnectionMode.SIMULATED)
                "Simulated unit at ${settings.simHost}:${settings.simPort} (Settings). Its passkey is 123456."
            else "On the unit: Menu, Phone, Pair. Then Add unit here and type the passkey the unit shows within 60 s.")
        }
        found?.let { list ->
            header("Units found")
            if (list.isEmpty()) item { Note("No unit found. Is the pairing window open on the unit?") }
            items(list) { u -> RowItem(u.name, u.address, onClick = {
                if (settings.mode == ConnectionMode.SIMULATED) pairTarget = u else m.pair(u, null)
            }) }
        }
    }
    if (forget) Confirm("Forget unit?", "The phone drops its pairing. To pair again, also forget the phone on the unit " +
        "(Menu, Phone, Forget).", "Forget", { m.forget() }, { forget = false })
    pairTarget?.let { u ->
        var key by remember { mutableStateOf("") }
        AlertDialog(onDismissRequest = { pairTarget = null }, title = { Text("Passkey") },
            text = {
                OutlinedTextField(key, { key = it.filter(Char::isDigit).take(6) }, label = { Text("6 digits shown on the unit") },
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.NumberPassword), singleLine = true,
                    modifier = Modifier.testTag("passkey"))
            },
            confirmButton = { Button(onClick = { m.pair(u, key); pairTarget = null }, enabled = key.length == 6) { Text("Pair") } },
            dismissButton = { TextButton(onClick = { pairTarget = null }) { Text("Cancel") } })
    }
}

// ------------------------------------------------------------------ 2. Workouts
/**
 * Paste or pick a workout, validate it, show the preview or the readable error (brief 7.3). Separate
 * from the screen so the instrumented tests can drive it alone.
 */
@Composable
fun WorkoutImportPane(ftp: Int, initial: String = "", onPickFile: () -> Unit = {}, onSave: (WorkoutImport.Ok) -> Unit) {
    var text by remember(initial) { mutableStateOf(initial) }
    var result by remember(initial) { mutableStateOf<WorkoutImport?>(if (initial.isBlank()) null else importWorkout(initial)) }
    Column(Modifier.padding(horizontal = 16.dp)) {
        OutlinedTextField(text, { text = it; result = null }, label = { Text("Paste the workout JSON") },
            modifier = Modifier.fillMaxWidth().heightIn(min = 120.dp).testTag("workout-text"), maxLines = 12)
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp), modifier = Modifier.padding(vertical = 8.dp)) {
            Button(onClick = { result = importWorkout(text) }, enabled = text.isNotBlank(), modifier = Modifier.testTag("validate")) { Text("Validate") }
            OutlinedButton(onClick = onPickFile) { Text("Open file") }
        }
        when (val r = result) {
            is WorkoutImport.Invalid -> Row(Modifier.fillMaxWidth().background(Tok.panel).padding(10.dp)) {
                Chip("ERROR", Tok.bad)
                Spacer(Modifier.width(8.dp))
                Text(r.message, color = Tok.text, modifier = Modifier.testTag("import-error"))
            }
            is WorkoutImport.Ok -> Column(Modifier.testTag("preview")) {
                val steps = previewSteps(r.workout, ftp)
                Text(r.workout.name, color = Tok.text, fontWeight = FontWeight.SemiBold)
                Text("${hms(r.workout.totalSecs.toLong())}, ${r.workout.steps.size} steps, watts at FTP $ftp W", color = Tok.muted, fontSize = 13.sp)
                WorkoutChart(steps, Modifier.fillMaxWidth().heightIn(min = 100.dp).padding(vertical = 8.dp))
                for (s in steps) Row(Modifier.fillMaxWidth().padding(vertical = 2.dp)) {
                    Chip("Z${s.zone}", Tok.zone(s.zone))
                    Spacer(Modifier.width(8.dp))
                    Text(s.label + if (s.rep.isNotEmpty()) " ${s.rep}" else "", color = Tok.text, modifier = Modifier.weight(1f))
                    Text(hms(s.secs.toLong()), color = Tok.muted)
                    Spacer(Modifier.width(8.dp))
                    Text(if (s.ramp) "${s.lowW}→${s.highW} W" else "${s.lowW}–${s.highW} W", color = Tok.text)
                }
                Button(onClick = { onSave(r) }, modifier = Modifier.padding(vertical = 8.dp).testTag("save")) { Text("Save to library") }
            }
            null -> {}
        }
    }
}

@Composable
fun WorkoutsScreen(m: AppModel) {
    val st by m.status.collectAsState()
    val local by m.workouts.collectAsState()
    val incoming by m.incomingWorkout.collectAsState()
    val ctx = LocalContext.current
    var importing by remember { mutableStateOf(false) }
    var delete by remember { mutableStateOf<ObjInfo?>(null) }
    val pick = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        uri?.let { m.incomingWorkout.value = ctx.contentResolver.openInputStream(it)?.use { s -> s.readBytes().decodeToString() } }
    }
    LazyColumn {
        header("On the unit")
        if (!st.connected) item { Note("Connect to see the unit's workouts.") }
        items(st.workouts) { o ->
            RowItem(o.name, "${hms(o.a)}, ${o.b} steps", trailing = { TextButton(onClick = { delete = o }) { Text("Delete") } })
        }
        header("Import")
        item {
            if (importing || incoming != null) {
                WorkoutImportPane(st.ftp ?: 200, incoming ?: "", onPickFile = { pick.launch(arrayOf("application/json", "text/*")) }) {
                    m.saveWorkout(it); importing = false; m.incomingWorkout.value = null
                }
            } else Buttons { Button(onClick = { importing = true }) { Text("Import workout") } }
        }
        header("Library on the phone")
        if (local.isEmpty()) item { Note("Workouts you import appear here; send them to the unit from here.") }
        items(local) { w ->
            val onUnit = st.workouts.any { it.id.contentEquals(w.cw1.copyOfRange(4, 12)) }
            RowItem(w.name, "${hms(w.totalSecs.toLong())}, ${w.steps} steps" + if (onUnit) ", on the unit" else "", trailing = {
                Row {
                    if (st.connected && !onUnit) TextButton(onClick = { m.send(1, w.cw1, w.name) }) { Text("Send") }
                    TextButton(onClick = { m.deleteLocalWorkout(w) }) { Text("Remove") }
                }
            })
        }
    }
    delete?.let { o -> Confirm("Delete ${o.name}?", "It is removed from the unit. The phone's copy stays.", "Delete",
        { m.deleteOnUnit(o) }, { delete = null }) }
}

// ------------------------------------------------------------------ 3. Routes
@Composable
fun RoutesScreen(m: AppModel) {
    val st by m.status.collectAsState()
    val local by m.routes.collectAsState()
    val imp by m.routeImport.collectAsState()
    val settings by m.settings.collectAsState()
    var delete by remember { mutableStateOf<ObjInfo?>(null) }
    val pick = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        uri?.let { m.importGpx(it, it.lastPathSegment?.substringAfterLast('/')?.substringBeforeLast('.') ?: "Route") }
    }
    LazyColumn {
        header("On the unit")
        if (!st.connected) item { Note("Connect to see the unit's routes.") }
        items(st.routes) { o ->
            RowItem(o.name, "${km(o.a)}, ${o.b} m up, ${o.c} climbs", trailing = { TextButton(onClick = { delete = o }) { Text("Delete") } })
        }
        if (imp == null) planSection(m)
        header("Import")
        when (val r = imp) {
            null -> item { Buttons { Button(onClick = { pick.launch(arrayOf("*/*")) }) { Text("Import GPX") } } }
            is RouteImport.Invalid -> item {
                Column {
                    Row(Modifier.fillMaxWidth().background(Tok.panel).padding(10.dp)) {
                        Chip("ERROR", Tok.bad); Spacer(Modifier.width(8.dp)); Text(r.message, color = Tok.text)
                    }
                    Buttons { OutlinedButton(onClick = { m.clearRouteImport() }) { Text("Close") } }
                }
            }
            is RouteImport.Ok -> item {
                val b = r.route
                Column(Modifier.padding(horizontal = 16.dp)) {
                    Text(r.name, color = Tok.text, fontWeight = FontWeight.SemiBold)
                    // With no elevation at all the object's ascent is 0 because nothing is known, not
                    // because the route is flat, and the climb list is empty for the same reason. Saying
                    // "0 m up, 0 climbs" would assert both.
                    val up = if (b.hasElevation) "${b.obj.ascentM} m up, ${b.climbs.size} climbs"
                             else "elevation unknown"
                    Text("${km(b.obj.distM)}, $up, ${b.turns.size} turn cues", color = Tok.muted, fontSize = 13.sp)
                    Text(
                        if (!b.hasElevation) "Elevation: none found. ${settings.elevation.label} returned nothing and the file has no elevation, so this route has no profile and no climbs."
                        else if (b.fromFile) "Elevation: from file"
                        else "Elevation: ${settings.elevation.label}",
                        color = if (b.hasElevation) Tok.muted else Tok.warn, fontSize = 13.sp)
                    ProfileChart(b.profileM, b.step, b.climbs, Modifier.fillMaxWidth().heightIn(min = 120.dp).padding(vertical = 8.dp))
                    val t = settings.thresholds
                    Text("Climbs: at least ${t.minLenM} m long, ${t.minAscentM} m up, ${t.minGradePct} % average (Settings)",
                        color = Tok.muted, fontSize = 13.sp)
                    for ((i, c) in b.climbs.withIndex()) Row(Modifier.padding(vertical = 2.dp)) {
                        Chip("CLIMB ${i + 1}", Tok.warn); Spacer(Modifier.width(8.dp))
                        Text("at ${km(c.startM)}: ${c.lengthM} m, ${c.ascentM} m up, ${c.avgX10 / 10.0} % avg, ${c.maxX10 / 10.0} % max", color = Tok.text)
                    }
                    Buttons {
                        Button(onClick = { m.saveRoute(r) }) { Text("Save to library") }
                        OutlinedButton(onClick = { m.clearRouteImport() }) { Text("Discard") }
                    }
                }
            }
        }
        header("Library on the phone")
        if (local.isEmpty()) item { Note("Routes you import appear here; send them to the unit from here.") }
        items(local) { r ->
            val onUnit = st.routes.any { it.id.contentEquals(r.cr1.copyOfRange(4, 12)) }
            val hasProf = cr1HasProfile(r.cr1)
            RowItem(r.name, "${km(r.distM)}, " + (if (hasProf) "${r.ascentM} m up, ${r.climbs} climbs" else "elevation unknown") +
                (if (hasProf && r.elevationFromFile) ", elevation from file" else "") +
                if (onUnit) ", on the unit" else "", trailing = {
                Row {
                    if (st.connected && !onUnit) TextButton(onClick = { m.send(2, r.cr1, r.name) }) { Text("Send") }
                    TextButton(onClick = { m.deleteLocalRoute(r) }) { Text("Remove") }
                }
            })
        }
    }
    delete?.let { o -> Confirm("Delete ${o.name}?", "It is removed from the unit. The phone's copy stays.", "Delete",
        { m.deleteOnUnit(o) }, { delete = null }) }
}

// ------------------------------------------------------------------ 3a. Plan a ride
private fun LazyListScope.planSection(m: AppModel) {
    header("Plan a ride")
    item { PlanForm(m) }
    item { PlanOptions(m) }
}

@Composable
private fun PlanForm(m: AppModel) {
    val ctx = LocalContext.current
    val s by m.settings.collectAsState()
    val hasKey by m.hasOrsKey.collectAsState()
    val busy by m.busy.collectAsState()
    var minutes by remember(s.planMinutes) { mutableFloatStateOf(s.planMinutes.toFloat()) }
    var manual by remember { mutableStateOf("") }
    val ask = rememberLauncherForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) { granted ->
        if (granted.values.any { it }) m.planRide(minutes.roundToInt(), null)
        else m.say("Location was not allowed. Type a start point instead.")
    }
    Column(Modifier.padding(horizontal = 16.dp)) {
        if (!hasKey) {
            Note("Planning uses OpenRouteService. Add a free key in Settings to start.")
            return@Column
        }
        Text("${minutes.roundToInt()} min, about ${"%.0f".format(minutes / 60 * m.prefs(s).speedKmh)} km", color = Tok.text)
        Slider(minutes, { minutes = (it / 15).roundToInt() * 15f }, valueRange = 30f..240f, steps = 13)
        Row(verticalAlignment = Alignment.CenterVertically) {
            for (p in BikeProfile.entries) {
                RadioButton(s.planProfile == p, { m.updateSettings { it.copy(planProfile = p) } })
                Text(p.name.lowercase().replaceFirstChar { it.uppercase() }, color = Tok.text, modifier = Modifier.padding(end = 8.dp))
            }
        }
        Row(verticalAlignment = Alignment.CenterVertically) {
            Switch(s.planFavourPaths, { v -> m.updateSettings { it.copy(planFavourPaths = v) } })
            Spacer(Modifier.width(8.dp))
            Text("Favour cycle paths", color = Tok.text)
        }
        Buttons {
            Button(onClick = {
                if (Here.permitted(ctx)) m.planRide(minutes.roundToInt(), null)
                else ask.launch(arrayOf(Manifest.permission.ACCESS_FINE_LOCATION, Manifest.permission.ACCESS_COARSE_LOCATION))
            }, enabled = busy == null) { Text("Plan from here") }
        }
        if (Here.approximateOnly(ctx)) Note("Only approximate location is allowed, so the start may be a kilometre or more out.")
        OutlinedTextField(manual, { manual = it }, label = { Text("Or start at (lat, lon)") }, singleLine = true,
            modifier = Modifier.fillMaxWidth())
        val typed = Here.parse(manual)
        Buttons {
            OutlinedButton(onClick = { typed?.let { m.planRide(minutes.roundToInt(), it) } }, enabled = typed != null && busy == null) {
                Text("Plan from there")
            }
        }
        Note("Your start point is sent to OpenRouteService to make the loops. Nothing else is.")
    }
}

@Composable
private fun PlanOptions(m: AppModel) {
    val plan by m.plan.collectAsState()
    val p = plan ?: return
    var exporting by remember { mutableStateOf<Pair<Scored, Int>?>(null) }
    val save = rememberLauncherForActivityResult(ActivityResultContracts.CreateDocument("application/gpx+xml")) { uri ->
        val e = exporting
        if (uri != null && e != null) m.exportPlanned(e.first, e.second, uri)
        exporting = null
    }
    val speed = m.prefs().speedKmh
    Column {
        for ((i, o) in p.options.withIndex()) {
            val n = i + 1
            val c = o.candidate
            val b = o.breakdown
            val mins = (c.distanceM / 1000 / speed * 60).roundToInt()
            val sub = "${km(c.distanceM.toLong())}, ${c.ascentM.roundToInt()} m up, about ${hms(mins * 60L)}\n" +
                "${pct(b.cyclePath)} cycle path, ${pct(b.quietRoad)} quiet streets, ${pct(b.busyRoad)} main road" +
                (if (b.unpaved >= 0.05) ", ${pct(b.unpaved)} unpaved" else "") +
                (if (b.backtrack >= 0.1) ", ${pct(b.backtrack)} doubles back" else "")
            RowItem("Option $n", sub, trailing = {
                Row {
                    TextButton(onClick = { m.usePlanned(o, n) }) { Text("Use") }
                    TextButton(onClick = { exporting = o to n; save.launch(m.suggestedGpxName(o, n)) }) { Text("GPX") }
                }
            })
        }
        if (p.failedTries > 0) Note("${p.failedTries} of the tries returned no loop.")
        Buttons { OutlinedButton(onClick = { m.clearPlan() }) { Text("Clear options") } }
    }
}

private fun pct(f: Double) = "${(f * 100).roundToInt()} %"

// ------------------------------------------------------------------ 4. Rides
private val DAY = DateTimeFormatter.ofPattern("EEE d MMM yyyy, HH:mm")

@Composable
fun RidesScreen(m: AppModel) {
    val st by m.status.collectAsState()
    val rides by m.rides.collectAsState()
    val hasKey by m.hasKey.collectAsState()
    val ctx = LocalContext.current
    LazyColumn {
        header("Rides")
        item {
            Column {
                Buttons { Button(onClick = { m.syncNow() }) { Text("Sync now") } }
                if (!st.connected) Note("Connect to fetch new rides. Rides already on the phone still upload.")
                if (!hasKey) Note("Add your Intervals.icu API key in Settings to upload.")
            }
        }
        val onUnitOnly = st.rides.filter { u -> rides.none { it.rideId == u.rideId } }
        items(rides) { r -> RideRow(r, onRetry = { m.retry(r) }, onDate = {
            val now = LocalDateTime.now()
            DatePickerDialog(ctx, { _, y, mo, d ->
                TimePickerDialog(ctx, { _, h, mi ->
                    val t = LocalDateTime.of(y, mo + 1, d, h, mi).atZone(ZoneId.systemDefault()).toEpochSecond()
                    m.setStart(r, t)
                }, 7, 0, true).show()
            }, now.year, now.monthValue - 1, now.dayOfMonth).show()
        }) }
        if (onUnitOnly.isNotEmpty()) {
            header("On the unit, not fetched")
            items(onUnitOnly) { u ->
                RowItem(if (u.timeUnknown) "Date unknown" else DAY.format(Instant.ofEpochSecond(u.rideId).atZone(ZoneId.systemDefault())),
                    "${hms(u.durationS)}, ${km(u.distanceM)}",
                    trailing = { Chip(if (u.uploaded) "UPLOADED" else "ON UNIT", if (u.uploaded) Tok.ok else Tok.muted) })
            }
        }
    }
}

@Composable
private fun RideRow(r: RideRecord, onRetry: () -> Unit, onDate: () -> Unit) {
    val title = if (r.state == RideState.NEEDS_DATE) "Date unknown" else DAY.format(Instant.ofEpochSecond(r.startUtc).atZone(ZoneId.systemDefault()))
    val sub = buildString {
        append("${hms(r.durationS)}, ${km(r.distanceM)}")
        if (r.recovered) append(", recovered after power loss")
        if (r.message.isNotEmpty()) append("\n").append(r.message)
    }
    RowItem(title, sub, trailing = {
        Row(verticalAlignment = Alignment.CenterVertically) {
            when (r.state) {
                RideState.NEEDS_DATE -> TextButton(onClick = onDate) { Text("Set date") }
                RideState.FAILED, RideState.NEEDS_KEY -> TextButton(onClick = onRetry) { Text("Retry") }
                else -> {}
            }
            val (label, colour) = when (r.state) {
                RideState.NEEDS_DATE -> "NEEDS DATE" to Tok.warn
                RideState.WAITING -> "WAITING" to Tok.muted
                RideState.UPLOADED -> "UPLOADED" to Tok.ok
                RideState.FAILED -> "FAILED" to Tok.bad
                RideState.NEEDS_KEY -> "NEEDS KEY" to Tok.bad
            }
            Chip(label, colour)
        }
    })
}

// ------------------------------------------------------------------ 5. Profile
@Composable
fun ProfileScreen(m: AppModel) {
    val st by m.status.collectAsState()
    var text by remember(st.ftp) { mutableStateOf(st.ftp?.toString() ?: "") }
    LazyColumn {
        header("Profile")
        item {
            Column(Modifier.padding(horizontal = 16.dp)) {
                OutlinedTextField(text, { text = it.filter(Char::isDigit).take(3) }, label = { Text("FTP (W)") },
                    enabled = st.connected && !st.recording, singleLine = true,
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number))
                val v = text.toIntOrNull()
                Buttons {
                    Button(onClick = { v?.let { m.setFtp(it) } }, enabled = st.connected && !st.recording && v != null && v in 50..600 && v != st.ftp) {
                        Text("Save to unit")
                    }
                }
                Note(when {
                    !st.connected -> "FTP is read from the unit when it connects, and can be changed only while connected."
                    st.recording -> "The unit is recording; FTP can be changed after the ride."
                    else -> "50 to 600 W. The unit recalculates the loaded workout."
                })
            }
        }
    }
}

// ------------------------------------------------------------------ 6. Settings
@Composable
fun SettingsScreen(m: AppModel) {
    val s by m.settings.collectAsState()
    val hasKey by m.hasKey.collectAsState()
    var key by remember { mutableStateOf("") }
    LazyColumn {
        header("Intervals.icu")
        item {
            Column(Modifier.padding(horizontal = 16.dp)) {
                Text(if (hasKey) "An API key is saved." else "No API key saved.", color = Tok.text)
                OutlinedTextField(key, { key = it }, label = { Text("New API key") }, singleLine = true,
                    visualTransformation = PasswordVisualTransformation(),
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Password), modifier = Modifier.fillMaxWidth())
                Buttons {
                    Button(onClick = { m.saveKey(key); key = "" }, enabled = key.isNotBlank()) { Text("Save key") }
                    if (hasKey) OutlinedButton(onClick = { m.clearKey() }) { Text("Remove key") }
                }
                Note("From intervals.icu, Settings, Developer Settings. Stored encrypted on this phone only.")
            }
        }
        header("Route planner")
        item { RoutePlannerSettings(m) }
        header("Climbs")
        item {
            Column(Modifier.padding(horizontal = 16.dp)) {
                NumberField("Minimum length (m)", s.thresholds.minLenM) { v -> m.updateSettings { it.copy(thresholds = it.thresholds.copy(minLenM = v)) } }
                NumberField("Minimum ascent (m)", s.thresholds.minAscentM) { v -> m.updateSettings { it.copy(thresholds = it.thresholds.copy(minAscentM = v)) } }
                NumberField("Minimum average grade (tenths of %)", (s.thresholds.minGradePct * 10).toInt()) { v ->
                    m.updateSettings { it.copy(thresholds = it.thresholds.copy(minGradePct = v / 10.0)) }
                }
                Note("Used for routes imported from now on.")
            }
        }
        header("Elevation source")
        items(ElevationSource.entries.toList()) { e ->
            RowItem(e.label, trailing = { RadioButton(selected = s.elevation == e, onClick = { m.updateSettings { it.copy(elevation = e) } }) },
                onClick = { m.updateSettings { it.copy(elevation = e) } })
        }
        header("Connection")
        item { Column {
            RowItem("Bluetooth", trailing = { RadioButton(s.mode == ConnectionMode.BLE, { m.updateSettings { it.copy(mode = ConnectionMode.BLE) } }) })
            RowItem("Simulated unit (simunit)", trailing = {
                RadioButton(s.mode == ConnectionMode.SIMULATED, { m.updateSettings { it.copy(mode = ConnectionMode.SIMULATED) } })
            })
            if (s.mode == ConnectionMode.SIMULATED) Column(Modifier.padding(horizontal = 16.dp)) {
                var host by remember(s.simHost) { mutableStateOf(s.simHost) }
                OutlinedTextField(host, { host = it }, label = { Text("Host") }, singleLine = true)
                NumberField("Port", s.simPort) { v -> m.updateSettings { it.copy(simHost = host.trim(), simPort = v) } }
                var base by remember(s.intervalsBase) { mutableStateOf(s.intervalsBase) }
                OutlinedTextField(base, { base = it }, label = { Text("Intervals.icu server (testing)") }, singleLine = true)
                Buttons {
                    OutlinedButton(onClick = { m.updateSettings { it.copy(simHost = host.trim(), intervalsBase = base.trim().trimEnd('/')) } }) { Text("Apply") }
                    OutlinedButton(onClick = { m.updateSettings { it.copy(intervalsBase = "https://intervals.icu") }; base = "https://intervals.icu" }) {
                        Text("Real server")
                    }
                }
                Note("From the emulator, the development machine is 10.0.2.2. Forget the unit before switching modes.")
            }
        } }
        header("About")
        item { RowItem("CC700 app", "v0.1.0. Workouts in the coach format v1, GPX routes, Intervals.icu upload.") }
    }
}

@Composable
private fun RoutePlannerSettings(m: AppModel) {
    val s by m.settings.collectAsState()
    val hasKey by m.hasOrsKey.collectAsState()
    var key by remember { mutableStateOf("") }
    Column(Modifier.padding(horizontal = 16.dp)) {
        Text(if (hasKey) "An OpenRouteService key is saved." else "No OpenRouteService key saved.", color = Tok.text)
        OutlinedTextField(key, { key = it }, label = { Text("New OpenRouteService key") }, singleLine = true,
            visualTransformation = PasswordVisualTransformation(),
            keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Password), modifier = Modifier.fillMaxWidth())
        Buttons {
            Button(onClick = { m.saveOrsKey(key); key = "" }, enabled = key.isNotBlank()) { Text("Save key") }
            if (hasKey) OutlinedButton(onClick = { m.clearOrsKey() }) { Text("Remove key") }
        }
        Note("Free from openrouteservice.org (sign up, then Dashboard, then Tokens). Stored encrypted on this phone only.")
        val speed = s.planSpeedKmh ?: defaultSpeedKmh(s.planProfile)
        NumberField("Average speed for planning (km/h)", speed.roundToInt()) { v ->
            if (v in 5..50) m.updateSettings { it.copy(planSpeedKmh = v.toDouble()) }
        }
        Note(if (s.planSpeedKmh == null) "Default for ${s.planProfile.name.lowercase()}. Set your own to match your rides."
             else "Turns the ride length in minutes into a distance.")
        if (s.planSpeedKmh != null) Buttons {
            OutlinedButton(onClick = { m.updateSettings { it.copy(planSpeedKmh = null) } }) { Text("Use default") }
        }
    }
}

@Composable
private fun NumberField(label: String, value: Int, onDone: (Int) -> Unit) {
    var t by remember(value) { mutableStateOf(value.toString()) }
    Row(verticalAlignment = Alignment.CenterVertically) {
        OutlinedTextField(t, { t = it.filter(Char::isDigit).take(5) }, label = { Text(label) }, singleLine = true,
            keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number), modifier = Modifier.weight(1f))
        TextButton(onClick = { t.toIntOrNull()?.let(onDone) }, enabled = t.toIntOrNull() != null && t.toIntOrNull() != value) { Text("Set") }
    }
}
