package run.xid.unigeek.ui.screens.files

import android.provider.OpenableColumns
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.systemBarsPadding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.LocalTextStyle
import androidx.compose.material3.Text
import androidx.compose.material3.TextField
import androidx.compose.material3.TextFieldDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import run.xid.unigeek.feature.files.FileEntry
import run.xid.unigeek.ui.components.Banner
import run.xid.unigeek.ui.components.BadgeKind
import run.xid.unigeek.ui.components.MonoLabel
import run.xid.unigeek.ui.components.ProgressBar
import run.xid.unigeek.ui.components.SectionLabel
import run.xid.unigeek.ui.components.clickableNoRipple
import run.xid.unigeek.ui.components.formatBytes
import run.xid.unigeek.transport.TransportKind
import run.xid.unigeek.ui.connection.ConnectionViewModel
import run.xid.unigeek.ui.connection.Viewer
import run.xid.unigeek.ui.connection.ViewerMode
import run.xid.unigeek.ui.screens.FeatureUnavailable
import run.xid.unigeek.ui.theme.Geek
import run.xid.unigeek.ui.theme.GeekMono
import run.xid.unigeek.ui.theme.GeekSans

private enum class DialogKind { Mkdir, NewFile, Rename, Delete }

@Composable
fun FilesScreen(conn: ConnectionViewModel) {
    if (!conn.fmActive) {
        FeatureUnavailable(
            title = "File Manager",
            reason = "The remote service is turned off on the device.",
            hint = "Enable it: ${if (conn.kind == TransportKind.Ble) "Bluetooth → Remote Device" else "HID → USB Remote"}, then reconnect.",
        )
        return
    }

    val context = LocalContext.current
    val pickFile = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) {
            val name = queryName(context, uri) ?: "upload.bin"
            runCatching { context.contentResolver.openInputStream(uri)?.use { it.readBytes() } }.getOrNull()?.let { conn.upload(name, it) }
        }
    }

    var dialog by remember { mutableStateOf<DialogKind?>(null) }
    var target by remember { mutableStateOf<FileEntry?>(null) }

    Column(Modifier.fillMaxSize().padding(horizontal = 20.dp, vertical = 16.dp)) {
        SectionLabel("File Manager")
        Spacer(Modifier.height(14.dp))

        Row(Modifier.fillMaxWidth().background(Geek.BgElev2).border(1.dp, Geek.Line).padding(10.dp), verticalAlignment = Alignment.CenterVertically) {
            Text(conn.cwd, color = Geek.Ink, style = TextStyle(fontFamily = GeekMono, fontSize = 12.sp), modifier = Modifier.weight(1f))
            if (conn.cwd != "/") IconBtn("↑") { conn.goUp() }
        }
        Spacer(Modifier.height(8.dp))
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            IconBtn("+ dir") { dialog = DialogKind.Mkdir }
            IconBtn("+ file") { dialog = DialogKind.NewFile }
            IconBtn("⇧ upload") { pickFile.launch(arrayOf("*/*")) }
            IconBtn("↻") { conn.refresh() }
        }

        Spacer(Modifier.height(10.dp))
        LazyColumn(Modifier.fillMaxWidth().weight(1f)) {
            items(conn.entries, key = { it.name }) { entry ->
                EntryRow(
                    entry,
                    onOpen = { conn.open(entry) },
                    onDownload = { conn.download(entry) },
                    onRename = { target = entry; dialog = DialogKind.Rename },
                    onDelete = { target = entry; dialog = DialogKind.Delete },
                )
            }
            if (conn.entries.isEmpty()) item {
                Text("empty", color = Geek.InkMuted, modifier = Modifier.padding(12.dp), style = TextStyle(fontFamily = GeekMono, fontSize = 12.sp))
            }
        }

        conn.progress?.let { p ->
            Spacer(Modifier.height(8.dp))
            Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                MonoLabel(p.label, size = 10)
                Spacer(Modifier.size(10.dp))
                Box(Modifier.weight(1f)) { ProgressBar(if (p.total > 0) p.value.toFloat() / p.total else 0f) }
                Spacer(Modifier.size(10.dp))
                MonoLabel(if (p.total > 0) "${p.value * 100 / p.total}%" else "…", size = 10, color = Geek.Accent)
            }
        }
        conn.fileError?.let {
            Spacer(Modifier.height(8.dp))
            val ok = it.startsWith("saved") || it.startsWith("binary") || it.startsWith("large")
            Box(Modifier.clickableNoRipple { conn.clearFileError() }) { Banner(it, if (ok) BadgeKind.Ok else BadgeKind.Danger) }
        }
        Spacer(Modifier.height(8.dp))
    }

    when (dialog) {
        DialogKind.Mkdir -> InputDialog("New folder", "name", "", onConfirm = { conn.mkdir(it); dialog = null }, onDismiss = { dialog = null })
        DialogKind.NewFile -> InputDialog("New file", "name", "", onConfirm = { conn.newFile(it); dialog = null }, onDismiss = { dialog = null })
        DialogKind.Rename -> target?.let { t -> InputDialog("Rename", "new name", t.name, onConfirm = { conn.rename(t, it); dialog = null }, onDismiss = { dialog = null }) }
        DialogKind.Delete -> target?.let { t -> ConfirmDialog("Delete ${t.name}?", "This can't be undone.", onConfirm = { conn.delete(t); dialog = null }, onDismiss = { dialog = null }) }
        null -> {}
    }

    conn.viewer?.let { FileViewerDialog(it, onSave = conn::saveText, onClose = conn::closeViewer) }
}

@Composable
private fun EntryRow(entry: FileEntry, onOpen: () -> Unit, onDownload: () -> Unit, onRename: () -> Unit, onDelete: () -> Unit) {
    Row(
        Modifier.fillMaxWidth().border(1.dp, Geek.Line).background(Geek.BgElev).clickableNoRipple(onOpen).padding(horizontal = 12.dp, vertical = 10.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(if (entry.isDir) "▸" else "·", color = Geek.Accent, modifier = Modifier.size(16.dp), style = TextStyle(fontFamily = GeekMono, fontSize = 14.sp))
        Spacer(Modifier.size(8.dp))
        Column(Modifier.weight(1f)) {
            Text(entry.name, color = Geek.Ink, style = TextStyle(fontFamily = GeekSans, fontSize = 14.sp))
            if (!entry.isDir) MonoLabel(formatBytes(entry.size), size = 10)
        }
        if (!entry.isDir) { IconBtn("⇩", onDownload); Spacer(Modifier.size(4.dp)) }
        IconBtn("✎", onRename); Spacer(Modifier.size(4.dp)); IconBtn("✕", onDelete)
    }
    Spacer(Modifier.height(6.dp))
}

@Composable
private fun IconBtn(label: String, onClick: () -> Unit) {
    Box(Modifier.background(Geek.BgElev).border(1.dp, Geek.LineStrong).clickableNoRipple(onClick).padding(horizontal = 10.dp, vertical = 6.dp), contentAlignment = Alignment.Center) {
        Text(label, color = Geek.InkDim, style = TextStyle(fontFamily = GeekMono, fontSize = 12.sp))
    }
}

@Composable
private fun InputDialog(title: String, label: String, initial: String, onConfirm: (String) -> Unit, onDismiss: () -> Unit) {
    var text by remember { mutableStateOf(initial) }
    val focus = remember { FocusRequester() }
    val submit = { if (text.isNotBlank()) onConfirm(text.trim()) }
    Dialog(onDismissRequest = onDismiss) {
        Column(
            Modifier.fillMaxWidth().background(Geek.BgElev).border(1.dp, Geek.LineStrong).padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(14.dp),
        ) {
            Text(title, color = Geek.Ink, style = TextStyle(fontFamily = GeekSans, fontSize = 16.sp))
            Row(
                Modifier.fillMaxWidth().background(Geek.Bg).border(1.dp, Geek.Line).padding(horizontal = 10.dp, vertical = 9.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Text("›", color = Geek.Accent, style = TextStyle(fontFamily = GeekMono, fontSize = 14.sp))
                Spacer(Modifier.size(8.dp))
                BasicTextField(
                    value = text, onValueChange = { text = it }, singleLine = true,
                    textStyle = TextStyle(fontFamily = GeekMono, fontSize = 14.sp, color = Geek.Ink),
                    cursorBrush = SolidColor(Geek.Accent),
                    keyboardOptions = KeyboardOptions(imeAction = ImeAction.Done),
                    keyboardActions = KeyboardActions(onDone = { submit() }),
                    modifier = Modifier.weight(1f).focusRequester(focus),
                    decorationBox = { inner ->
                        Box(contentAlignment = Alignment.CenterStart) {
                            if (text.isEmpty()) Text(label, color = Geek.InkMuted, style = TextStyle(fontFamily = GeekMono, fontSize = 14.sp))
                            inner()
                        }
                    },
                )
            }
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Spacer(Modifier.weight(1f))
                DialogBtn("Cancel", Geek.InkDim, false, onDismiss)
                DialogBtn("OK", Geek.Accent, true, submit)
            }
        }
    }
    LaunchedEffect(Unit) { focus.requestFocus() }
}

@Composable
private fun ConfirmDialog(title: String, body: String, onConfirm: () -> Unit, onDismiss: () -> Unit) {
    Dialog(onDismissRequest = onDismiss) {
        Column(
            Modifier.fillMaxWidth().background(Geek.BgElev).border(1.dp, Geek.LineStrong).padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp),
        ) {
            Text(title, color = Geek.Ink, style = TextStyle(fontFamily = GeekSans, fontSize = 16.sp))
            Text(body, color = Geek.InkDim, style = TextStyle(fontFamily = GeekSans, fontSize = 13.sp))
            Spacer(Modifier.size(2.dp))
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Spacer(Modifier.weight(1f))
                DialogBtn("Cancel", Geek.InkDim, false, onDismiss)
                DialogBtn("Delete", Geek.Danger, true, onConfirm)
            }
        }
    }
}

@Composable
private fun DialogBtn(label: String, color: Color, emphasized: Boolean, onClick: () -> Unit) {
    Box(
        Modifier
            .border(1.dp, if (emphasized) color else Geek.LineStrong)
            .clickableNoRipple(onClick)
            .padding(horizontal = 16.dp, vertical = 7.dp),
        contentAlignment = Alignment.Center,
    ) {
        Text(label, color = color, style = TextStyle(fontFamily = GeekMono, fontSize = 13.sp))
    }
}

@Composable
private fun FileViewerDialog(v: Viewer, onSave: (String) -> Unit, onClose: () -> Unit) {
    var mode by remember(v) { mutableStateOf(v.mode) }
    var text by remember(v) { mutableStateOf(v.text) }
    Dialog(onDismissRequest = onClose, properties = DialogProperties(usePlatformDefaultWidth = false)) {
        Column(Modifier.fillMaxSize().background(Geek.Bg).systemBarsPadding().padding(16.dp)) {
            Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                Column(Modifier.weight(1f)) {
                    Text(v.name, color = Geek.Ink, style = TextStyle(fontFamily = GeekMono, fontSize = 15.sp))
                    MonoLabel("${formatBytes(v.bytes.size.toLong())} · ${v.path}", size = 10)
                }
                IconBtn("✕", onClose)
            }
            Spacer(Modifier.height(12.dp))
            Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                ViewerTab("Text", mode == ViewerMode.Text) { mode = ViewerMode.Text }
                ViewerTab("Hex", mode == ViewerMode.Hex) { mode = ViewerMode.Hex }
            }
            Spacer(Modifier.height(10.dp))
            Box(Modifier.fillMaxWidth().weight(1f).border(1.dp, Geek.Line).background(Geek.BgElev)) {
                when (mode) {
                    ViewerMode.Text ->
                        if (v.editable)
                            TextField(value = text, onValueChange = { text = it }, modifier = Modifier.fillMaxSize(),
                                textStyle = LocalTextStyle.current.copy(fontFamily = GeekMono, fontSize = 13.sp), colors = fieldColors())
                        else
                            Text(text.ifEmpty { "(no printable text)" }, color = Geek.InkDim,
                                modifier = Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(10.dp),
                                style = TextStyle(fontFamily = GeekMono, fontSize = 13.sp))
                    ViewerMode.Hex -> HexDump(v.bytes)
                }
            }
            Spacer(Modifier.height(12.dp))
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Spacer(Modifier.weight(1f))
                if (mode == ViewerMode.Text && v.editable) IconBtn("Save") { onSave(text) }
                IconBtn("Close", onClose)
            }
        }
    }
}

@Composable
private fun ViewerTab(label: String, selected: Boolean, onClick: () -> Unit) {
    Box(
        Modifier
            .background(if (selected) Geek.AccentDim else Geek.BgElev)
            .border(1.dp, if (selected) Geek.Accent else Geek.LineStrong)
            .clickableNoRipple(onClick)
            .padding(horizontal = 14.dp, vertical = 7.dp),
        contentAlignment = Alignment.Center,
    ) {
        Text(label, color = if (selected) Geek.Accent else Geek.InkDim, style = TextStyle(fontFamily = GeekMono, fontSize = 12.sp))
    }
}

@Composable
private fun HexDump(bytes: ByteArray) {
    if (bytes.isEmpty()) {
        Text("(empty)", color = Geek.InkMuted, modifier = Modifier.padding(12.dp),
            style = TextStyle(fontFamily = GeekMono, fontSize = 11.sp))
        return
    }
    val style = TextStyle(fontFamily = GeekMono, fontSize = 11.sp)
    val measurer = rememberTextMeasurer()
    val density = LocalDensity.current
    BoxWithConstraints(Modifier.fillMaxSize()) {
        // Width of one monospace glyph (averaged from a long sample for accuracy).
        val charW = with(density) { (measurer.measure("0".repeat(64), style).size.width / 64f).toDp() }
        // Per row, chars = offset(6) + hex(3N-1) + ascii(N) = 4N+5, plus two 10.dp gaps + 20.dp padding.
        val usable = maxWidth - 40.dp
        val fitChars = if (charW > 0.dp) (usable / charW).toInt() else 8
        val perRow = listOf(32, 24, 16, 8).firstOrNull { 4 * it + 5 <= fitChars } ?: 8
        val rowCount = (bytes.size + perRow - 1) / perRow
        LazyColumn(Modifier.fillMaxSize().padding(horizontal = 10.dp, vertical = 8.dp)) {
            items(rowCount) { row ->
                val start = row * perRow
                val end = minOf(start + perRow, bytes.size)
                val offset = start.toString(16).padStart(6, '0')
                val hex = (0 until perRow).joinToString(" ") { j ->
                    if (start + j < end) (bytes[start + j].toInt() and 0xFF).toString(16).padStart(2, '0') else "  "
                }
                val ascii = buildString {
                    for (i in start until end) {
                        val c = bytes[i].toInt() and 0xFF
                        append(if (c in 0x21..0x7e) c.toChar() else '.')
                    }
                }
                Row(Modifier.fillMaxWidth().padding(vertical = 1.dp)) {
                    Text(offset, color = Geek.Accent, style = style, softWrap = false, maxLines = 1)
                    Spacer(Modifier.size(10.dp))
                    Text(hex, color = Geek.Ink, style = style, softWrap = false, maxLines = 1)
                    Spacer(Modifier.size(10.dp))
                    Text(ascii, color = Geek.InkDim, style = style, softWrap = false, maxLines = 1)
                }
            }
        }
    }
}

@Composable
private fun fieldColors() = TextFieldDefaults.colors(
    focusedContainerColor = Geek.Bg, unfocusedContainerColor = Geek.Bg,
    focusedTextColor = Geek.Ink, unfocusedTextColor = Geek.Ink, cursorColor = Geek.Accent,
    focusedIndicatorColor = Geek.Accent, unfocusedIndicatorColor = Geek.Line,
)

private fun queryName(context: android.content.Context, uri: android.net.Uri): String? {
    context.contentResolver.query(uri, null, null, null, null)?.use { c ->
        val idx = c.getColumnIndex(OpenableColumns.DISPLAY_NAME)
        if (idx >= 0 && c.moveToFirst()) return c.getString(idx)
    }
    return null
}
