package com.coffin.companion

import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbManager
import android.os.Build
import android.os.Bundle
import android.view.View
import androidx.appcompat.app.AppCompatActivity
import com.coffin.companion.databinding.ActivityFlashBinding

class FlashActivity : AppCompatActivity() {
    private lateinit var binding: ActivityFlashBinding
    private var pulling = false
    private var statusHeld = false
    private var askedUsb = false
    private var usbDenied = false
    private var probing = false
    private var psramLine: String? = null
    private var probeFailed = false

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
        pulling = true
        statusHeld = true
        binding.update.isEnabled = false
        binding.status.text = getString(R.string.flash_checking)
        val dir = filesDir
        Thread {
            val text = try {
                val result = CoffinRelease.pull(dir) { progress ->
                    val line = when (progress) {
                        PullProgress.Checking -> getString(R.string.flash_checking)
                        is PullProgress.Downloading -> downloadText(progress)
                        PullProgress.Unpacking -> getString(R.string.flash_unpacking)
                    }
                    runOnUiThread {
                        if (!isDestroyed) {
                            binding.status.text = line
                        }
                    }
                }
                getString(R.string.flash_got, result.tag, result.screams, result.creaks)
            } catch (e: ReleaseException) {
                releaseError(e)
            } catch (e: Exception) {
                getString(R.string.flash_failed, e.message ?: e.javaClass.simpleName)
            }
            runOnUiThread {
                if (isDestroyed) {
                    return@runOnUiThread
                }
                pulling = false
                statusHeld = true
                binding.status.text = text
                refresh()
            }
        }.start()
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
            ReleaseException.Reason.NO_ZIP -> getString(R.string.flash_no_zip)
            ReleaseException.Reason.NO_BIN -> getString(R.string.flash_no_bin)
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
            statusHeld = false
            probing = false
            psramLine = null
            probeFailed = false
            binding.update.visibility = View.GONE
            binding.status.text = getString(R.string.flash_wait)
            return
        }
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
        binding.update.visibility = if (known != null) View.VISIBLE else View.GONE
        binding.update.isEnabled = known != null && !pulling
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
                Result.success(info.model + psram)
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
                    onSuccess = { line ->
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

    private companion object {
        const val ESPRESSIF_VID = 0x303A
        const val USB_JTAG_PID = 0x1001
        const val ACTION_USB_PERMISSION = "com.coffin.companion.USB_PERMISSION"
    }
}
