package run.xid.unigeek.ui.screens.remote

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.LocalTextStyle
import androidx.compose.material3.Text
import androidx.compose.material3.TextField
import androidx.compose.material3.TextFieldDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.FilterQuality
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.input.key.Key
import androidx.compose.ui.input.key.KeyEventType
import androidx.compose.ui.input.key.key
import androidx.compose.ui.input.key.onPreviewKeyEvent
import androidx.compose.ui.input.key.type
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.IntSize
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import run.xid.unigeek.feature.remote.BoardNav
import run.xid.unigeek.feature.remote.BoardNavs
import run.xid.unigeek.protocol.Proto
import run.xid.unigeek.transport.TransportKind
import run.xid.unigeek.ui.components.MonoLabel
import run.xid.unigeek.ui.components.SectionLabel
import run.xid.unigeek.ui.components.clickableNoRipple
import run.xid.unigeek.ui.components.scanlines
import run.xid.unigeek.ui.connection.ConnectionViewModel
import run.xid.unigeek.ui.screens.FeatureUnavailable
import run.xid.unigeek.ui.theme.Geek
import run.xid.unigeek.ui.theme.GeekMono
import run.xid.unigeek.ui.theme.GeekSans
import kotlin.math.roundToInt

@Composable
fun RemoteScreen(conn: ConnectionViewModel) {
    if (!conn.mirrorActive) {
        FeatureUnavailable(
            title = "Remote",
            reason = "The remote service is turned off on the device.",
            hint = "Enable it on the device: ${if (conn.kind == TransportKind.Ble) "Bluetooth → Remote Device" else "HID → USB Remote"}, then reconnect.",
        )
        return
    }

    DisposableEffect(Unit) {
        conn.startStream()
        onDispose { conn.stopStream() }
    }

    // ── Navigation detector: live caps win for touch/keyboard; board metadata adds
    //    the 4-way-vs-up/down distinction the caps byte can't express. ──
    val caps = conn.caps
    val nav = BoardNavs.forId(conn.info.board)
    val isTouch = caps?.touch == true || nav?.touch == true
    val hasKeyboard = caps?.keyboard == true || nav?.keyboard == true
    val fourWay = nav?.fourWay ?: true

    val scroll = rememberScrollState()
    Column(Modifier.fillMaxWidth().verticalScroll(scroll).padding(horizontal = 20.dp, vertical = 16.dp)) {
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween, verticalAlignment = Alignment.CenterVertically) {
            SectionLabel("Remote · UART")
            if (caps != null) MonoLabel("${caps.width}×${caps.height}", size = 10, color = Geek.InkDim)
        }

        Spacer(Modifier.height(16.dp))
        MirrorStage(conn, tappable = isTouch)

        Spacer(Modifier.height(10.dp))
        Row(verticalAlignment = Alignment.CenterVertically) {
            if (conn.streaming) MonoLabel("live", size = 10, color = Geek.Accent) else MonoLabel("paused", size = 10, color = Geek.Amber)
            if (isTouch) { Spacer(Modifier.size(10.dp)); MonoLabel("touch", size = 10, color = Geek.Accent) }
            if (hasKeyboard) { Spacer(Modifier.size(10.dp)); MonoLabel("keyboard", size = 10, color = Geek.Accent) }
            Spacer(Modifier.size(14.dp))
            Row(Modifier.clickableNoRipple { conn.toggleSwap() }, verticalAlignment = Alignment.CenterVertically) {
                Box(Modifier.size(12.dp).border(1.dp, if (conn.swapBytes) Geek.Accent else Geek.LineStrong).background(if (conn.swapBytes) Geek.Accent else Color.Transparent))
                Spacer(Modifier.size(6.dp)); MonoLabel("swap bytes", size = 9)
            }
        }

        Spacer(Modifier.height(16.dp))
        if (isTouch) {
            // Touch device — the mirror IS the control surface; no D-pad needed.
            Box(Modifier.fillMaxWidth().background(Geek.BgElev).border(1.dp, Geek.Line).padding(16.dp)) {
                Text("Tap the mirror to navigate — this device is touch-driven.",
                    color = Geek.InkDim, style = TextStyle(fontFamily = GeekSans, fontSize = 13.sp, lineHeight = 19.sp))
            }
        } else {
            DPad(conn, fourWay = fourWay)
            if (hasKeyboard) { Spacer(Modifier.height(16.dp)); KeyboardInput(conn) }
        }

        if (nav != null) { Spacer(Modifier.height(20.dp)); NavGuide(nav) }
        Spacer(Modifier.height(20.dp))
    }
}

@Composable
private fun MirrorStage(conn: ConnectionViewModel, tappable: Boolean) {
    val caps = conn.caps
    val w = caps?.width ?: 240
    val h = caps?.height ?: 135
    var boxSize by remember { mutableStateOf(IntSize.Zero) }
    val img = remember(conn.screen) { conn.screen?.asImageBitmap() }

    Box(
        Modifier
            .fillMaxWidth()
            .aspectRatio(w.toFloat() / h.toFloat())
            .background(Color.Black)
            .border(1.dp, Geek.LineStrong)
            .onSizeChanged { boxSize = it }
            .pointerInput(caps, conn.streaming, tappable) {
                detectTapGestures { off ->
                    if (!tappable || boxSize.width == 0) return@detectTapGestures
                    val tx = (off.x / boxSize.width * w).roundToInt().coerceIn(0, w - 1)
                    val ty = (off.y / boxSize.height * h).roundToInt().coerceIn(0, h - 1)
                    conn.touch(tx, ty)
                }
            },
        contentAlignment = Alignment.Center,
    ) {
        Canvas(Modifier.fillMaxWidth().aspectRatio(w.toFloat() / h.toFloat()).scanlines(0.05f)) {
            val tick = conn.frameTick; tick.let {}
            img?.let { drawImage(image = it, dstSize = IntSize(size.width.roundToInt(), size.height.roundToInt()), filterQuality = FilterQuality.None) }
        }
        if (!conn.streaming) Text("paused", color = Geek.InkMuted, style = TextStyle(fontFamily = GeekMono, fontSize = 12.sp))
    }
}

@Composable
private fun DPad(conn: ConnectionViewModel, fourWay: Boolean) {
    Column(horizontalAlignment = Alignment.CenterHorizontally, modifier = Modifier.fillMaxWidth()) {
        PadButton("▲") { conn.direction(Proto.DIR_UP) }
        Spacer(Modifier.height(8.dp))
        Row(verticalAlignment = Alignment.CenterVertically) {
            if (fourWay) { PadButton("◄") { conn.direction(Proto.DIR_LEFT) }; Spacer(Modifier.size(8.dp)) }
            PadButton("●", primary = true,
                onClick = { conn.direction(Proto.DIR_PRESS, 0) },
                onLongClick = { conn.direction(Proto.DIR_PRESS, 1) })
            if (fourWay) { Spacer(Modifier.size(8.dp)); PadButton("►") { conn.direction(Proto.DIR_RIGHT) } }
        }
        Spacer(Modifier.height(8.dp))
        PadButton("▼") { conn.direction(Proto.DIR_DOWN) }
        Spacer(Modifier.height(12.dp))
        Box(Modifier.background(Geek.BgElev).border(1.dp, Geek.LineStrong).clickableNoRipple { conn.direction(Proto.DIR_BACK) }.padding(horizontal = 24.dp, vertical = 10.dp)) {
            MonoLabel("back", size = 11)
        }
    }
}

@Composable
private fun PadButton(glyph: String, primary: Boolean = false, onLongClick: (() -> Unit)? = null, onClick: () -> Unit) {
    Box(
        Modifier
            .size(56.dp)
            .background(if (primary) Geek.AccentDim else Geek.BgElev)
            .border(1.dp, if (primary) Geek.Accent else Geek.LineStrong)
            .pointerInput(onLongClick) { detectTapGestures(onTap = { onClick() }, onLongPress = { onLongClick?.invoke() }) },
        contentAlignment = Alignment.Center,
    ) { Text(glyph, color = if (primary) Geek.Accent else Geek.Ink, style = TextStyle(fontFamily = GeekMono, fontSize = 18.sp)) }
}

@Composable
private fun NavGuide(nav: BoardNav) {
    Column(Modifier.fillMaxWidth().background(Geek.BgElev).border(1.dp, Geek.Line)) {
        Row(Modifier.fillMaxWidth().background(Geek.BgElev2).padding(horizontal = 14.dp, vertical = 8.dp)) {
            MonoLabel("device navigation · ${nav.scheme}", size = 10)
        }
        Column(Modifier.padding(horizontal = 14.dp, vertical = 10.dp)) {
            nav.guide.forEach { row ->
                Row(Modifier.fillMaxWidth().padding(vertical = 5.dp), horizontalArrangement = Arrangement.SpaceBetween, verticalAlignment = Alignment.CenterVertically) {
                    Text(row.input, color = Geek.InkDim, style = TextStyle(fontFamily = GeekMono, fontSize = 12.sp), modifier = Modifier.weight(1f))
                    MonoLabel(row.action, size = 10, color = Geek.Accent)
                }
            }
            nav.note?.let {
                Spacer(Modifier.height(6.dp))
                Text(it, color = Geek.InkMuted, style = TextStyle(fontFamily = GeekMono, fontSize = 11.sp, lineHeight = 16.sp))
            }
        }
    }
}

@Composable
private fun KeyboardInput(conn: ConnectionViewModel) {
    // Pure passthrough: every keystroke is forwarded and the field is cleared, so it
    // never holds text — what you type only shows up on the mirrored device screen.
    var text by remember { mutableStateOf("") }
    MonoLabel("keyboard input")
    Spacer(Modifier.height(6.dp))
    TextField(
        value = text,
        onValueChange = { new ->
            new.forEach { conn.key(it.code) }
            text = ""
        },
        modifier = Modifier
            .fillMaxWidth()
            .onPreviewKeyEvent { ev ->
                // The field is always empty, so deletes/enters arrive only as key events.
                if (ev.type == KeyEventType.KeyDown && ev.key == Key.Backspace) { conn.key(8); true }
                else if (ev.type == KeyEventType.KeyDown && ev.key == Key.Enter) { conn.key(10); true }
                else false
            },
        textStyle = LocalTextStyle.current.copy(fontFamily = GeekMono, fontSize = 14.sp),
        singleLine = true,
        placeholder = { Text("type to send keys to the device…", color = Geek.InkMuted, style = TextStyle(fontFamily = GeekMono, fontSize = 14.sp)) },
        keyboardOptions = KeyboardOptions(autoCorrect = false, keyboardType = KeyboardType.Ascii, imeAction = ImeAction.Send),
        keyboardActions = KeyboardActions(onSend = { conn.key(10) }),
        colors = TextFieldDefaults.colors(
            focusedContainerColor = Geek.BgElev, unfocusedContainerColor = Geek.BgElev,
            focusedTextColor = Geek.Ink, unfocusedTextColor = Geek.Ink, cursorColor = Geek.Accent,
            focusedIndicatorColor = Geek.Accent, unfocusedIndicatorColor = Geek.Line,
        ),
    )
}
