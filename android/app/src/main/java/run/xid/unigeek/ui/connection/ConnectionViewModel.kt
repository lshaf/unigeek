package run.xid.unigeek.ui.connection

import android.app.Application
import android.content.ContentValues
import android.graphics.Bitmap
import android.os.Build
import android.os.Environment
import android.provider.MediaStore
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.hoho.android.usbserial.driver.UsbSerialPort
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import kotlinx.coroutines.withTimeoutOrNull
import org.json.JSONObject
import run.xid.unigeek.feature.files.FileEntry
import run.xid.unigeek.feature.files.FileManagerClient
import run.xid.unigeek.feature.install.Board
import run.xid.unigeek.feature.install.Boards
import run.xid.unigeek.feature.install.EspFlasher
import run.xid.unigeek.feature.install.Firmware
import run.xid.unigeek.feature.remote.BoardNavs
import run.xid.unigeek.feature.remote.DeviceCaps
import run.xid.unigeek.feature.remote.RemoteClient
import run.xid.unigeek.transport.BleDevice
import run.xid.unigeek.transport.BleSupport
import run.xid.unigeek.transport.BleTransport
import run.xid.unigeek.transport.Transport
import run.xid.unigeek.transport.TransportKind
import run.xid.unigeek.transport.UsbSerialTransport
import run.xid.unigeek.transport.UsbSupport
import java.io.ByteArrayOutputStream
import java.io.File

enum class ConnStatus { Disconnected, Scanning, Connecting, Connected, Error }
enum class InstallStatus { Idle, Downloading, Flashing, Done, Error }

data class DeviceInfo(val version: String?, val board: String?, val total: Long, val used: Long)
data class Progress(val label: String, val value: Long, val total: Long)

enum class ViewerMode { Text, Hex }

/** A file opened for viewing. Holds the raw bytes plus a best-effort UTF-8 decode. */
class Viewer(
    val path: String,
    val name: String,
    val bytes: ByteArray,
    val text: String,
    val mode: ViewerMode,
    val editable: Boolean,
)

/** Extensions that open in the text editor by default (when the bytes look textual). */
private val TEXT_EXTENSIONS = setOf(
    "txt", "md", "markdown", "json", "csv", "tsv", "log", "ini", "cfg", "conf", "config",
    "lua", "js", "mjs", "ts", "html", "htm", "css", "xml", "svg", "yaml", "yml", "toml",
    "sh", "bash", "c", "cc", "cpp", "h", "hpp", "py", "rb", "go", "rs", "java", "kt", "kts",
    "gd", "ino", "pde", "properties", "env", "gitignore", "list", "m3u", "m3u8", "srt", "vtt",
    "sql", "gcode", "nfo", "asc", "pem", "hex",
)

/** Extensions that always open in the hex viewer regardless of content. */
private val HEX_EXTENSIONS = setOf(
    "bin", "nfc", "dump", "dat", "raw", "img", "mfd", "eml", "pcap", "pcapng",
    "png", "jpg", "jpeg", "gif", "bmp", "webp", "ico", "wav", "mp3", "ogg", "ttf", "bdf",
)

/** Largest file we load into memory for in-app viewing; bigger files are saved instead. */
private const val MAX_VIEW_BYTES = 512 * 1024

/**
 * Single app-wide connection. Owns the active transport and both protocol clients,
 * detects what the device exposes (file manager / screen mirror / board / version),
 * and drives every feature so all tabs share one link. Activity-scoped.
 */
class ConnectionViewModel(app: Application) : AndroidViewModel(app) {

    // ── connection ──
    var status by mutableStateOf(ConnStatus.Disconnected); private set
    var kind by mutableStateOf<TransportKind?>(null); private set
    var error by mutableStateOf<String?>(null); private set
    var info by mutableStateOf(DeviceInfo(null, null, 0, 0)); private set
    var caps by mutableStateOf<DeviceCaps?>(null); private set
    var fmActive by mutableStateOf(false); private set
    var mirrorActive by mutableStateOf(false); private set
    val bleDevices = mutableStateListOf<BleDevice>()

    // ── remote ──
    var streaming by mutableStateOf(false); private set
    var screen by mutableStateOf<Bitmap?>(null); private set
    var frameTick by mutableStateOf(0L); private set
    var swapBytes by mutableStateOf(false); private set

    // ── files ──
    var cwd by mutableStateOf("/"); private set
    val entries = mutableStateListOf<FileEntry>()
    var fileError by mutableStateOf<String?>(null); private set
    var fsBusy by mutableStateOf(false); private set
    var progress by mutableStateOf<Progress?>(null); private set
    var viewer by mutableStateOf<Viewer?>(null); private set

    // ── install ──
    var versions by mutableStateOf(listOf<Firmware.Release>()); private set
    var selectedVersion by mutableStateOf(Firmware.FALLBACK_VERSION); private set
    var installStatus by mutableStateOf(InstallStatus.Idle); private set
    var installPhase by mutableStateOf("Ready"); private set
    var installProgress by mutableStateOf(0f); private set
    var installError by mutableStateOf<String?>(null); private set
    val installLog = mutableStateListOf("$ unigeek-flasher --ready")

    private val remoteClient = RemoteClient()
    private val fmClient = FileManagerClient()
    private var transport: Transport? = null
    private var scanJob: Job? = null
    private var helloProbe: CompletableDeferred<DeviceCaps>? = null

    val boardName: String? get() = info.board?.let { Boards.byId[it]?.name ?: it }
    val detectedBoard: Board? get() = info.board?.let { Boards.byId[it] }

    init {
        remoteClient.onHello = { c -> caps = c; ensureBitmap(c); helloProbe?.complete(c) }
        remoteClient.onRect = ::onRect
        remoteClient.onFill = ::onFill
        viewModelScope.launch {
            val v = Firmware.versions()
            versions = v
            v.firstOrNull()?.let { selectedVersion = it.version }
        }
    }

    // ── connect / detect ────────────────────────────────────────────────────────

    fun connectUsb() {
        if (status == ConnStatus.Connecting || status == ConnStatus.Connected) return
        error = null; status = ConnStatus.Connecting
        viewModelScope.launch {
            val ctx = getApplication<Application>()
            val driver = UsbSupport.firstDriver(ctx)
            if (driver == null) { fail("No USB device found. Connect a UniGeek (or a bare ESP32 to flash) with a USB-OTG cable."); return@launch }
            if (!UsbSupport.ensurePermission(ctx, driver.device)) { fail("USB permission denied."); return@launch }
            try {
                val port = withContext(Dispatchers.IO) { UsbSupport.openPort(ctx, driver) }
                val label = "USB · " + driver.javaClass.simpleName.removeSuffix("SerialDriver")
                bind(UsbSerialTransport(port, label, ::ingest, { onLinkClosed(it) }, 115200), TransportKind.Usb)
                detect()
            } catch (e: Exception) { fail(e.message ?: "Couldn't open the USB port.") }
        }
    }

    fun startBleScan() {
        if (status == ConnStatus.Connecting || status == ConnStatus.Connected) return
        error = null; bleDevices.clear(); status = ConnStatus.Scanning
        scanJob?.cancel()
        scanJob = viewModelScope.launch {
            try {
                BleSupport.scan(getApplication()).collect { d ->
                    if (bleDevices.none { it.device.address == d.device.address }) bleDevices.add(d)
                }
            } catch (e: Exception) { fail(e.message ?: "BLE scan failed.") }
        }
    }

    fun stopBleScan() {
        scanJob?.cancel(); scanJob = null
        if (status == ConnStatus.Scanning) status = ConnStatus.Disconnected
    }

    fun connectBle(device: BleDevice) {
        stopBleScan()
        error = null; status = ConnStatus.Connecting
        viewModelScope.launch {
            try {
                val t = BleTransport(getApplication(), device.device, ::ingest, { onLinkClosed(it) })
                t.connect()
                bind(t, TransportKind.Ble)
                detect()
            } catch (e: Exception) { fail(e.message ?: "Couldn't connect over BLE.") }
        }
    }

    private fun ingest(bytes: ByteArray) { fmClient.ingest(bytes); remoteClient.ingest(bytes) }

    private fun bind(t: Transport, k: TransportKind) {
        transport = t; kind = k
        fmClient.attach(t); remoteClient.attach(t)
    }

    /** Probe which subsystems are live: FM (INFO) and screen mirror (HELLO). Both
     *  ride either transport now — the firmware mirrors over BLE as well as USB. */
    private suspend fun detect() {
        val infoJson = withTimeoutOrNull(2500) { runCatching { fmClient.info() }.getOrNull() }
        if (infoJson != null) { fmActive = true; parseInfo(infoJson) } else fmActive = false

        val probe = CompletableDeferred<DeviceCaps>(); helloProbe = probe
        runCatching { remoteClient.startStream() }
        val c = withTimeoutOrNull(2500) { probe.await() }
        helloProbe = null
        runCatching { remoteClient.stopStream() }
        streaming = false
        mirrorActive = c != null

        if (!fmActive && !mirrorActive) {
            disconnectInternal()
            val menu = if (kind == TransportKind.Ble) "Bluetooth → Remote Device" else "HID → USB Remote"
            fail("Couldn't reach a UniGeek. Turn on $menu on the device, then reconnect. (A bare ESP32 can still be flashed from the Update screen.)")
            return
        }
        status = ConnStatus.Connected
        if (fmActive) runCatching { reload("/") }
    }

    private fun parseInfo(json: String) {
        runCatching {
            val o = JSONObject(json)
            info = DeviceInfo(
                version = o.optString("version").ifBlank { null },
                board = o.optString("board").ifBlank { null },
                total = o.optLong("total"),
                used = o.optLong("used"),
            )
        }
    }

    private fun onLinkClosed(@Suppress("UNUSED_PARAMETER") t: Throwable?) {
        if (installStatus == InstallStatus.Flashing || installStatus == InstallStatus.Downloading) return
        if (status == ConnStatus.Connected) disconnect()
    }

    fun disconnect() {
        disconnectInternal()
        status = ConnStatus.Disconnected
    }

    private fun disconnectInternal() {
        stopBleScan()
        val t = transport; transport = null
        viewModelScope.launch { runCatching { remoteClient.stopStream() }; remoteClient.detach(); fmClient.detach(); runCatching { t?.close() } }
        kind = null; caps = null; fmActive = false; mirrorActive = false
        streaming = false; screen = null
        info = DeviceInfo(null, null, 0, 0)
        entries.clear(); cwd = "/"; viewer = null; progress = null; fileError = null
    }

    /** Release the USB port for flashing while keeping detected board/version visible. */
    private fun releaseForFlash() {
        val t = transport; transport = null
        viewModelScope.launch { runCatching { remoteClient.stopStream() }; remoteClient.detach(); fmClient.detach(); runCatching { t?.close() } }
        status = ConnStatus.Disconnected; kind = null
        fmActive = false; mirrorActive = false; streaming = false
        entries.clear()
    }

    // ── remote ────────────────────────────────────────────────────────────────

    private fun ensureBitmap(c: DeviceCaps) {
        val b = screen
        if (b == null || b.width != c.width || b.height != c.height) {
            screen = Bitmap.createBitmap(c.width.coerceAtLeast(1), c.height.coerceAtLeast(1), Bitmap.Config.ARGB_8888)
        }
        frameTick++
    }

    private fun onRect(x: Int, y: Int, w: Int, h: Int, argb: IntArray) {
        val b = screen ?: return
        runCatching { b.setPixels(argb, 0, w, x, y, w, h); frameTick++ }
    }

    private fun onFill(x: Int, y: Int, w: Int, h: Int, argb: Int) {
        val b = screen ?: return
        val row = IntArray(w) { argb }
        runCatching { for (yy in y until y + h) b.setPixels(row, 0, w, x, yy, w, 1); frameTick++ }
    }

    fun startStream() {
        if (!mirrorActive) return
        viewModelScope.launch { runCatching { remoteClient.startStream() }; streaming = true }
    }

    fun stopStream() {
        viewModelScope.launch { runCatching { remoteClient.stopStream() }; streaming = false }
    }

    fun toggleSwap() { swapBytes = !swapBytes; remoteClient.swapBytes = swapBytes }

    fun direction(dir: Int, event: Int = 0) {
        if (!streaming) return
        viewModelScope.launch { runCatching { remoteClient.sendDirection(dir, event) } }
    }

    fun key(code: Int) { if (streaming) viewModelScope.launch { runCatching { remoteClient.sendKey(code) } } }

    fun touch(x: Int, y: Int) {
        if (!streaming) return
        // Allow touch when the device reports it OR the board is touch-driven.
        val touchable = caps?.touch == true || BoardNavs.forId(info.board)?.touch == true
        if (!touchable) return
        viewModelScope.launch { runCatching { remoteClient.sendTouch(x, y) } }
    }

    // ── files ───────────────────────────────────────────────────────────────────

    private suspend fun reload(path: String) {
        val list = fmClient.list(path); entries.clear(); entries.addAll(list); cwd = path
    }

    fun refresh() = fsWork("refresh") { reload(cwd) }
    fun open(entry: FileEntry) { if (entry.isDir) fsWork("open ${entry.name}") { reload(join(cwd, entry.name)) } else openFile(entry) }
    fun goUp() { if (cwd != "/") fsWork("up") { reload(parent(cwd)) } }
    fun delete(entry: FileEntry) = fsWork("delete ${entry.name}") { fmClient.delete(join(cwd, entry.name)); reload(cwd) }
    fun rename(entry: FileEntry, newName: String) = fsWork("rename") { fmClient.rename(join(cwd, entry.name), join(cwd, newName)); reload(cwd) }
    fun mkdir(name: String) = fsWork("mkdir $name") { fmClient.mkdir(join(cwd, name)); reload(cwd) }
    fun newFile(name: String) = fsWork("new $name") { fmClient.createFile(join(cwd, name)); reload(cwd) }

    fun upload(name: String, bytes: ByteArray) = fsWork("upload $name") {
        progress = Progress("upload $name", 0, bytes.size.toLong())
        fmClient.put(join(cwd, name), bytes) { sent -> progress = Progress("upload $name", sent, bytes.size.toLong()) }
        progress = null; reload(cwd)
    }

    fun download(entry: FileEntry) = fsWork("download ${entry.name}") {
        val data = collect(entry)
        val where = withContext(Dispatchers.IO) { saveToDownloads(entry.name, data) }
        fileError = "saved → $where"
    }

    private fun openFile(entry: FileEntry) = fsWork("open ${entry.name}") {
        if (entry.size > MAX_VIEW_BYTES) { val w = withContext(Dispatchers.IO) { saveToDownloads(entry.name, collect(entry)) }; fileError = "large file saved → $w"; return@fsWork }
        val data = collect(entry)
        val mode = pickViewerMode(entry.name, data)
        viewer = Viewer(join(cwd, entry.name), entry.name, bytes = data, text = String(data, Charsets.UTF_8), mode = mode, editable = mode == ViewerMode.Text)
    }

    /** Choose Text vs Hex from the filename extension, falling back to content sniffing. */
    private fun pickViewerMode(name: String, data: ByteArray): ViewerMode {
        val ext = name.substringAfterLast('.', "").lowercase()
        val hasNull = data.any { it.toInt() == 0 }
        return when {
            ext in HEX_EXTENSIONS -> ViewerMode.Hex
            ext in TEXT_EXTENSIONS -> if (hasNull) ViewerMode.Hex else ViewerMode.Text
            hasNull -> ViewerMode.Hex            // unknown extension, binary content
            else -> ViewerMode.Text              // unknown extension, printable content
        }
    }

    fun saveText(content: String) {
        val v = viewer ?: return
        fsWork("save ${v.name}") {
            val bytes = content.toByteArray(Charsets.UTF_8)
            progress = Progress("save ${v.name}", 0, bytes.size.toLong())
            fmClient.put(v.path, bytes) { sent -> progress = Progress("save ${v.name}", sent, bytes.size.toLong()) }
            progress = null
            viewer = Viewer(v.path, v.name, bytes = bytes, text = content, mode = ViewerMode.Text, editable = true)
            reload(cwd)
        }
    }

    fun closeViewer() { viewer = null }
    fun clearFileError() { fileError = null }

    private suspend fun collect(entry: FileEntry): ByteArray {
        val baos = ByteArrayOutputStream(entry.size.toInt().coerceAtLeast(64))
        progress = Progress("download ${entry.name}", 0, entry.size)
        fmClient.get(join(cwd, entry.name)) { chunk -> baos.write(chunk); progress = Progress("download ${entry.name}", baos.size().toLong(), entry.size) }
        progress = null
        return baos.toByteArray()
    }

    private fun fsWork(label: String, block: suspend () -> Unit) {
        if (!fmActive || status != ConnStatus.Connected) return
        viewModelScope.launch {
            fsBusy = true; fileError = null
            try { block() } catch (e: Exception) { fileError = e.message; progress = null }
            finally { fsBusy = false }
        }
    }

    // ── install ───────────────────────────────────────────────────────────────

    val supportedBoardIds: Set<String>
        get() = versions.firstOrNull { it.version == selectedVersion }?.boardIds ?: Boards.all.map { it.id }.toSet()

    val latestVersion: String? get() = versions.firstOrNull()?.version

    /** True when the connected board has a newer build available to flash. */
    val updateAvailable: Boolean
        get() {
            val board = info.board ?: return false
            val latest = versions.firstOrNull() ?: return false
            if (board !in latest.boardIds) return false
            val cur = info.version
            return cur == null || Firmware.compareVersions(latest.version, cur) > 0
        }

    /** Flash the latest release for the currently-detected board. */
    fun updateFirmware() {
        val latest = versions.firstOrNull() ?: return
        val board = info.board ?: return
        selectedVersion = latest.version
        flash(board)
    }

    fun selectVersion(v: String) { selectedVersion = v }

    private fun ilog(m: String) { installLog.add(m); while (installLog.size > 120) installLog.removeAt(0) }

    fun flash(boardId: String) {
        val board = Boards.byId[boardId] ?: return
        if (installStatus == InstallStatus.Downloading || installStatus == InstallStatus.Flashing) return
        installError = null; installStatus = InstallStatus.Downloading; installPhase = "Preparing"; installProgress = 0f
        viewModelScope.launch {
            val ctx = getApplication<Application>()
            // Flashing needs exclusive USB access — drop any live USB link first, but
            // keep the detected board/version on screen for the Update view.
            if (kind == TransportKind.Usb) { releaseForFlash() }
            val driver = UsbSupport.firstDriver(ctx)
            if (driver == null) { installFail("No USB device found. Connect the board with a USB-OTG cable."); return@launch }
            if (!UsbSupport.ensurePermission(ctx, driver.device)) { installFail("USB permission denied."); return@launch }
            var port: UsbSerialPort? = null
            try {
                ilog("$ flash --board ${board.id} --fw $selectedVersion")
                installPhase = "Downloading firmware"; installStatus = InstallStatus.Downloading
                val image = Firmware.download(selectedVersion, board.id) { rec, total -> installProgress = if (total > 0) rec.toFloat() / total else 0f }
                ilog("firmware ready: ${image.size / 1024} KB")
                installPhase = "Flashing"; installStatus = InstallStatus.Flashing; installProgress = 0f
                port = withContext(Dispatchers.IO) { UsbSupport.openPort(ctx, driver) }
                val flasher = EspFlasher(port) { ilog("  $it") }
                withContext(Dispatchers.IO) { flasher.flash(image, board.isS3, board.flashBaud) { p -> installProgress = p } }
                ilog("✓ flash complete — unplug and reconnect ${board.name}")
                installStatus = InstallStatus.Done; installPhase = "Complete"; installProgress = 1f
            } catch (e: Exception) { installFail(e.message ?: "Flash failed.") }
            finally { withContext(Dispatchers.IO) { runCatching { port?.close() } } }
        }
    }

    fun resetInstall() { if (installStatus != InstallStatus.Flashing && installStatus != InstallStatus.Downloading) { installStatus = InstallStatus.Idle; installPhase = "Ready"; installProgress = 0f; installError = null } }

    private fun installFail(msg: String) { installError = msg; installStatus = InstallStatus.Error; installPhase = "Error"; ilog("error: $msg") }

    // ── helpers ───────────────────────────────────────────────────────────────

    private fun fail(msg: String) { error = msg; status = ConnStatus.Error }

    private fun saveToDownloads(name: String, data: ByteArray): String {
        val ctx = getApplication<Application>()
        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            val values = ContentValues().apply {
                put(MediaStore.Downloads.DISPLAY_NAME, name)
                put(MediaStore.Downloads.MIME_TYPE, "application/octet-stream")
                put(MediaStore.Downloads.IS_PENDING, 1)
            }
            val resolver = ctx.contentResolver
            val uri = resolver.insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, values) ?: throw java.io.IOException("Couldn't create download")
            resolver.openOutputStream(uri)?.use { it.write(data) }
            values.clear(); values.put(MediaStore.Downloads.IS_PENDING, 0); resolver.update(uri, values, null, null)
            "Downloads/$name"
        } else {
            val dir = File(ctx.getExternalFilesDir(Environment.DIRECTORY_DOWNLOADS), ""); dir.mkdirs()
            val f = File(dir, name); f.writeBytes(data); f.absolutePath
        }
    }

    private fun join(base: String, name: String) = if (name.startsWith("/")) name else if (base.endsWith("/")) "$base$name" else "$base/$name"
    private fun parent(path: String): String { val t = path.trimEnd('/'); val i = t.lastIndexOf('/'); return if (i <= 0) "/" else t.substring(0, i) }

    override fun onCleared() { super.onCleared(); disconnectInternal() }
}
