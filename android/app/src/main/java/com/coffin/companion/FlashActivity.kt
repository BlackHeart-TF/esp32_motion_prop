package com.coffin.companion

import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbManager
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Environment
import android.os.storage.StorageManager
import android.os.storage.StorageVolume
import android.provider.DocumentsContract
import android.view.View
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import com.coffin.companion.databinding.ActivityFlashBinding
import java.io.File

class FlashActivity : AppCompatActivity() {
    private lateinit var binding: ActivityFlashBinding
    private var pulling = false
    private var statusHeld = false
    private var askedUsb = false
    private var usbDenied = false
    private var probing = false
    private var psramLine: String? = null
    private var isS3 = false
    private var probeFailed = false
    private var pendingZip: File? = null

    private val pickDrive = registerForActivityResult(ActivityResultContracts.OpenDocumentTree()) { tree ->
        picked(tree)
    }

    private val usbReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context?, intent: Intent?) {
            if (intent?.action == ACTION_USB_PERMISSION) {
                usbDenied = !intent.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false)
            }
            refresh()
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityFlashBinding.inflate(layoutInflater)
        setContentView(binding.root)
        binding.update.setOnClickListener { pullRelease() }
        binding.pack.setOnClickListener { pullPack() }
        binding.browse.setOnClickListener { openDrive() }
    }

    override fun onStart() {
        super.onStart()
        val filter = IntentFilter().apply {
            addAction(UsbManager.ACTION_USB_DEVICE_ATTACHED)
            addAction(UsbManager.ACTION_USB_DEVICE_DETACHED)
            addAction(ACTION_USB_PERMISSION)
        }
        if (Build.VERSION.SDK_INT >= 33) {
            registerReceiver(usbReceiver, filter, RECEIVER_NOT_EXPORTED)
        } else {
            registerReceiver(usbReceiver, filter)
        }
        refresh()
    }

    override fun onStop() {
        unregisterReceiver(usbReceiver)
        super.onStop()
    }

    private fun pullRelease() {
        if (pulling) {
            return
        }
        val device = bootDevice() ?: return
        pulling = true
        statusHeld = true
        binding.update.isEnabled = false
        binding.status.text = getString(R.string.flash_checking)
        val dir = filesDir
        val app = applicationContext
        Thread {
            val text = try {
                val result = CoffinRelease.pull(dir, CoffinRelease.BIN_NAME) { progress ->
                    show(pullText(progress))
                }
                try {
                    RomFlasher.flash(app, device, result.bin) { pct ->
                        show(getString(R.string.flash_writing, pct))
                    }
                    getString(R.string.flash_done, result.tag)
                } catch (e: Exception) {
                    getString(R.string.flash_write_failed, e.message ?: e.javaClass.simpleName)
                }
            } catch (e: ReleaseException) {
                releaseError(e)
            } catch (e: Exception) {
                getString(R.string.flash_failed, e.message ?: e.javaClass.simpleName)
            }
            finish(text)
        }.start()
    }

    private fun pullPack() {
        if (pulling) {
            return
        }
        pulling = true
        statusHeld = true
        binding.pack.isEnabled = false
        binding.status.text = getString(R.string.flash_checking)
        val dir = filesDir
        Thread {
            val text = try {
                val result = CoffinRelease.pull(dir, CoffinRelease.PACK_NAME) { progress ->
                    show(pullText(progress))
                }
                runOnUiThread {
                    if (!isDestroyed) {
                        copyToDrive(result.bin)
                    }
                }
                return@Thread
            } catch (e: ReleaseException) {
                releaseError(e)
            } catch (e: Exception) {
                getString(R.string.flash_failed, e.message ?: e.javaClass.simpleName)
            }
            finish(text)
        }.start()
    }

    private fun copyToDrive(zip: File) {
        val volume = driveVolume()
        if (volume == null) {
            finish(getString(R.string.drive_not_mounted))
            return
        }
        val saved = getSharedPreferences(PREFS, MODE_PRIVATE).getString(KEY_TREE, null)?.let(Uri::parse)
        val usable = saved != null &&
            contentResolver.persistedUriPermissions.any { it.uri == saved && it.isWritePermission } &&
            DocumentsContract.getTreeDocumentId(saved).startsWith("${volume.uuid}:")
        if (usable) {
            runCopy(saved!!, zip)
            return
        }
        pendingZip = zip
        binding.status.text = getString(R.string.pack_pick_drive)
        val start = DocumentsContract.buildDocumentUri(EXTERNAL_STORAGE_DOCS, "${volume.uuid}:")
        AlertDialog.Builder(this)
            .setMessage(R.string.pack_pick_notice)
            .setPositiveButton(R.string.pack_pick_continue) { _, _ -> pickDrive.launch(start) }
            .setNegativeButton(android.R.string.cancel) { _, _ -> picked(null) }
            .setOnCancelListener { picked(null) }
            .show()
    }

    private fun picked(tree: Uri?) {
        val zip = pendingZip
        pendingZip = null
        if (tree == null || zip == null) {
            finish(getString(R.string.pack_cancelled))
            return
        }
        try {
            contentResolver.takePersistableUriPermission(
                tree,
                Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_WRITE_URI_PERMISSION,
            )
        } catch (_: SecurityException) {
        }
        getSharedPreferences(PREFS, MODE_PRIVATE).edit().putString(KEY_TREE, tree.toString()).apply()
        runCopy(tree, zip)
    }

    private fun runCopy(tree: Uri, zip: File) {
        val resolver = contentResolver
        Thread {
            val text = try {
                val result = DriveCopy.unpack(resolver, tree, zip) { path ->
                    show(getString(R.string.pack_copying, path))
                }
                getString(R.string.pack_copied, result.copied, result.skipped)
            } catch (e: Exception) {
                getString(R.string.pack_copy_failed, e.message ?: e.javaClass.simpleName)
            }
            finish(text)
        }.start()
    }

    private fun driveVolume(): StorageVolume? {
        val storage = getSystemService(StorageManager::class.java)
        val mounted = storage?.storageVolumes.orEmpty().filter { volume ->
            volume.isRemovable && volume.uuid != null && volume.state == Environment.MEDIA_MOUNTED
        }
        return mounted.firstOrNull { it.getDescription(this).contains("usb", ignoreCase = true) }
            ?: mounted.firstOrNull()
    }

    private fun finish(text: String) {
        runOnUiThread {
            if (isDestroyed) {
                return@runOnUiThread
            }
            pulling = false
            statusHeld = true
            binding.status.text = text
            refresh()
        }
    }

    private fun openDrive() {
        val volume = driveVolume()
        if (volume == null) {
            statusHeld = true
            binding.status.text = getString(R.string.drive_not_mounted)
            return
        }
        val root = DocumentsContract.buildRootUri(EXTERNAL_STORAGE_DOCS, volume.uuid)
        val top = DocumentsContract.buildDocumentUri(EXTERNAL_STORAGE_DOCS, "${volume.uuid}:")
        val asRoot = Intent(Intent.ACTION_VIEW).setDataAndType(root, DocumentsContract.Root.MIME_TYPE_ITEM)
        val folders = listOf(
            Intent(Intent.ACTION_VIEW).setDataAndType(top, DocumentsContract.Document.MIME_TYPE_DIR),
            Intent(Intent.ACTION_VIEW).setDataAndType(top, "resource/folder"),
            Intent(Intent.ACTION_VIEW).setDataAndType(top, "inode/directory"),
        ).filter { it.resolveActivity(packageManager) != null }
        for (intent in folders + asRoot) {
            try {
                startActivity(intent)
                return
            } catch (_: Exception) {
            }
        }
        try {
            if (Build.VERSION.SDK_INT >= 29) {
                startActivity(volume.createOpenDocumentTreeIntent())
                return
            }
        } catch (_: Exception) {
        }
        statusHeld = true
        binding.status.text = getString(R.string.no_file_manager)
    }

    private fun pullText(progress: PullProgress): String = when (progress) {
        PullProgress.Checking -> getString(R.string.flash_checking)
        is PullProgress.Downloading -> downloadText(progress)
    }

    private fun show(line: String) {
        runOnUiThread {
            if (!isDestroyed) {
                binding.status.text = line
            }
        }
    }

    private fun downloadText(progress: PullProgress.Downloading): String {
        return if (progress.total > 0) {
            getString(R.string.flash_downloading_pct, (progress.bytes * 100 / progress.total).toInt())
        } else {
            getString(R.string.flash_downloading_kb, progress.bytes / 1024)
        }
    }

    private fun releaseError(error: ReleaseException): String {
        return when (error.reason) {
            ReleaseException.Reason.NO_REPO -> getString(R.string.flash_no_repo)
            ReleaseException.Reason.NO_RELEASE -> getString(R.string.flash_no_release)
            ReleaseException.Reason.NO_BIN -> getString(R.string.flash_no_bin, error.message ?: "")
            ReleaseException.Reason.TOO_BIG -> getString(R.string.flash_too_big)
            ReleaseException.Reason.FAILED -> getString(R.string.flash_failed, error.message ?: "")
        }
    }

    private fun refresh() {
        if (pulling) {
            return
        }
        val device = bootDevice()
        if (device == null) {
            askedUsb = false
            usbDenied = false
            probing = false
            psramLine = null
            isS3 = false
            probeFailed = false
            binding.update.visibility = View.GONE
            val drive = coffinDrive() != null
            binding.pack.visibility = if (drive) View.VISIBLE else View.GONE
            binding.pack.isEnabled = drive
            binding.browse.visibility = binding.pack.visibility
            if (!statusHeld) {
                binding.status.text = getString(if (drive) R.string.flash_drive_found else R.string.flash_wait)
            }
            statusHeld = false
            return
        }
        binding.pack.visibility = View.GONE
        binding.browse.visibility = View.GONE
        val usb = getSystemService(UsbManager::class.java)
        if (usb == null || !usb.hasPermission(device)) {
            binding.update.visibility = View.GONE
            if (!statusHeld) {
                binding.status.text = getString(
                    if (usbDenied) R.string.flash_usb_denied else R.string.flash_usb_permission,
                )
            }
            if (usb != null && !usbDenied && !askedUsb) {
                askUsb(usb, device)
            }
            return
        }
        if (psramLine == null && !probeFailed && !probing) {
            readPsram(device)
        }
        val known = psramLine
        binding.update.visibility = if (known != null && isS3) View.VISIBLE else View.GONE
        binding.update.isEnabled = known != null && isS3 && !pulling
        if (!statusHeld && !probing && known != null) {
            binding.status.text = known
        }
    }

    private fun readPsram(device: UsbDevice) {
        probing = true
        binding.update.visibility = View.GONE
        if (!statusHeld) {
            binding.status.text = getString(R.string.flash_reading_psram)
        }
        val app = applicationContext
        Thread {
            val outcome = try {
                val info = PsramProbe.read(app, device)
                val psram = when {
                    info.model != "ESP32-S3" -> ""
                    info.psram == null -> "\n" + getString(R.string.flash_no_psram)
                    else -> "\n" + getString(R.string.flash_psram, info.psram)
                }
                Result.success((info.model == "ESP32-S3") to (info.model + psram))
            } catch (e: Exception) {
                Result.failure(e)
            }
            runOnUiThread {
                if (isDestroyed) {
                    return@runOnUiThread
                }
                probing = false
                if (bootDevice() == null) {
                    refresh()
                    return@runOnUiThread
                }
                outcome.fold(
                    onSuccess = { (s3, line) ->
                        isS3 = s3
                        psramLine = line
                        probeFailed = false
                        if (!statusHeld) {
                            binding.status.text = line
                        }
                    },
                    onFailure = { error ->
                        probeFailed = true
                        statusHeld = true
                        binding.status.text = getString(
                            R.string.flash_psram_failed,
                            error.message ?: error.javaClass.simpleName,
                        )
                    },
                )
                refresh()
            }
        }.start()
    }

    private fun askUsb(usb: UsbManager, device: UsbDevice) {
        askedUsb = true
        val flags = PendingIntent.FLAG_UPDATE_CURRENT or
            if (Build.VERSION.SDK_INT >= 31) PendingIntent.FLAG_MUTABLE else 0
        val intent = Intent(ACTION_USB_PERMISSION).setPackage(packageName)
        usb.requestPermission(device, PendingIntent.getBroadcast(this, 0, intent, flags))
    }

    private fun bootDevice(): UsbDevice? {
        val usb = getSystemService(UsbManager::class.java) ?: return null
        return usb.deviceList.values.firstOrNull { device ->
            device.vendorId == ESPRESSIF_VID && device.productId == USB_JTAG_PID
        }
    }

    private fun coffinDrive(): UsbDevice? {
        val usb = getSystemService(UsbManager::class.java) ?: return null
        return usb.deviceList.values.firstOrNull { device ->
            device.vendorId == ESPRESSIF_VID && device.productId == COFFIN_MSC_PID
        }
    }

    private companion object {
        const val ESPRESSIF_VID = 0x303A
        const val USB_JTAG_PID = 0x1001
        const val COFFIN_MSC_PID = 0x4002
        const val EXTERNAL_STORAGE_DOCS = "com.android.externalstorage.documents"
        const val ACTION_USB_PERMISSION = "com.coffin.companion.USB_PERMISSION"
        const val PREFS = "drive"
        const val KEY_TREE = "tree"
    }
}
