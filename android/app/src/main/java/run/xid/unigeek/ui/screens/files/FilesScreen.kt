package run.xid.unigeek.ui.screens.files

import android.provider.OpenableColumns
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.LocalTextStyle
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TextField
import androidx.compose.material3.TextFieldDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
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
            IconBtn("⤒ upload") { pickFile.launch(arrayOf("*/*")) }
            IconBtn("⟳") { conn.refresh() }
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

    conn.viewer?.let { TextViewerDialog(it.name, it.text, onSave = conn::saveText, onClose = conn::closeViewer) }
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
        if (!entry.isDir) { IconBtn("⤓", onDownload); Spacer(Modifier.size(4.dp)) }
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
    AlertDialog(
        onDismissRequest = onDismiss, containerColor = Geek.BgElev,
        title = { Text(title, color = Geek.Ink, style = TextStyle(fontFamily = GeekSans, fontSize = 18.sp)) },
        text = {
            TextField(value = text, onValueChange = { text = it }, singleLine = true,
                textStyle = LocalTextStyle.current.copy(fontFamily = GeekMono, fontSize = 14.sp),
                placeholder = { Text(label, color = Geek.InkMuted) }, colors = fieldColors())
        },
        confirmButton = { TextButton(onClick = { if (text.isNotBlank()) onConfirm(text.trim()) }) { Text("OK", color = Geek.Accent) } },
        dismissButton = { TextButton(onClick = onDismiss) { Text("Cancel", color = Geek.InkDim) } },
    )
}

@Composable
private fun ConfirmDialog(title: String, body: String, onConfirm: () -> Unit, onDismiss: () -> Unit) {
    AlertDialog(
        onDismissRequest = onDismiss, containerColor = Geek.BgElev,
        title = { Text(title, color = Geek.Ink, style = TextStyle(fontFamily = GeekSans, fontSize = 18.sp)) },
        text = { Text(body, color = Geek.InkDim, style = TextStyle(fontFamily = GeekSans, fontSize = 14.sp)) },
        confirmButton = { TextButton(onClick = onConfirm) { Text("Delete", color = Geek.Danger) } },
        dismissButton = { TextButton(onClick = onDismiss) { Text("Cancel", color = Geek.InkDim) } },
    )
}

@Composable
private fun TextViewerDialog(name: String, initial: String, onSave: (String) -> Unit, onClose: () -> Unit) {
    var text by remember(name) { mutableStateOf(initial) }
    AlertDialog(
        onDismissRequest = onClose, containerColor = Geek.BgElev,
        title = { Text(name, color = Geek.Ink, style = TextStyle(fontFamily = GeekMono, fontSize = 14.sp)) },
        text = {
            TextField(value = text, onValueChange = { text = it }, modifier = Modifier.fillMaxWidth().height(320.dp),
                textStyle = LocalTextStyle.current.copy(fontFamily = GeekMono, fontSize = 13.sp), colors = fieldColors())
        },
        confirmButton = { TextButton(onClick = { onSave(text) }) { Text("Save", color = Geek.Accent) } },
        dismissButton = { TextButton(onClick = onClose) { Text("Close", color = Geek.InkDim) } },
    )
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
