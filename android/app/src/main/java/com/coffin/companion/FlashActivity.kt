package com.coffin.companion

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
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

    private val usbReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context?, intent: Intent?) {
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
        val connected = bootDeviceConnected()
        binding.update.visibility = if (connected) View.VISIBLE else View.GONE
        binding.update.isEnabled = connected
        if (!connected) {
            statusHeld = false
            binding.status.text = getString(R.string.flash_wait)
            return
        }
        if (!statusHeld) {
            binding.status.text = getString(R.string.flash_ready)
        }
    }

    private fun bootDeviceConnected(): Boolean {
        val usb = getSystemService(UsbManager::class.java) ?: return false
        return usb.deviceList.values.any { device ->
            device.vendorId == ESPRESSIF_VID && device.productId == USB_JTAG_PID
        }
    }

    private companion object {
        const val ESPRESSIF_VID = 0x303A
        const val USB_JTAG_PID = 0x1001
    }
}
