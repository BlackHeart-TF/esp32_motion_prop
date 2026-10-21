package com.coffin.companion

import android.Manifest
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothManager
import android.bluetooth.le.AdvertiseCallback
import android.bluetooth.le.AdvertiseData
import android.bluetooth.le.AdvertiseSettings
import android.bluetooth.le.BluetoothLeAdvertiser
import android.content.Intent
import android.content.pm.PackageManager
import android.content.Context
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.provider.Settings
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import com.coffin.companion.databinding.ActivityMainBinding

class MainActivity : AppCompatActivity() {
    private lateinit var binding: ActivityMainBinding
    private val handler = Handler(Looper.getMainLooper())
    private var advertiser: BluetoothLeAdvertiser? = null
    private var callback: AdvertiseCallback? = null
    private var sendingName: String? = null
    private var countWrapped = false
    private var askedPermissions = false
    private val stopRunnable = Runnable { stopAdvertising(markSent = true) }

    private val permissionLauncher = registerForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions()
    ) { refresh() }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)
        binding.scare.setOnClickListener { send(CoffinPacket.CMD_SCARE, getString(R.string.scare)) }
        binding.open.setOnClickListener { send(CoffinPacket.CMD_OPEN, getString(R.string.open)) }
        binding.reset.setOnClickListener { send(CoffinPacket.CMD_RESET, getString(R.string.reset)) }
        binding.firmware.setOnClickListener {
            startActivity(Intent(this, FlashActivity::class.java))
        }
    }

    override fun onResume() {
        super.onResume()
        refresh()
        if (missingPermissions() && !askedPermissions) {
            requestPermissions()
        }
    }

    override fun onDestroy() {
        stopAdvertising(markSent = false)
        super.onDestroy()
    }

    private fun refresh() {
        val problem = problemText()
        if (sendingName == null) {
            binding.status.text = problem ?: getString(R.string.ready)
        }
    }

    private fun send(cmd: Int, name: String) {
        val problem = problemText()
        if (problem != null) {
            binding.status.text = problem
            if (missingPermissions()) {
                requestPermissions()
            }
            return
        }

        stopAdvertising(markSent = false)
        val counter = nextCounter()
        val settings = AdvertiseSettings.Builder()
            .setAdvertiseMode(AdvertiseSettings.ADVERTISE_MODE_LOW_LATENCY)
            .setTxPowerLevel(AdvertiseSettings.ADVERTISE_TX_POWER_HIGH)
            .setConnectable(false)
            .setTimeout(0)
            .build()
        val data = AdvertiseData.Builder()
            .setIncludeDeviceName(false)
            .setIncludeTxPowerLevel(false)
            .addManufacturerData(CoffinPacket.COMPANY_ID, CoffinPacket.payload(cmd, phoneId(), counter))
            .build()
        val adv = advertiser ?: return
        sendingName = name
        binding.status.text = getString(R.string.sending, name)
        val cb = object : AdvertiseCallback() {
            override fun onStartSuccess(settingsInEffect: AdvertiseSettings) {
                runOnUiThread {
                    if (sendingName == name) {
                        binding.status.text = getString(R.string.sending, name)
                    }
                }
            }

            override fun onStartFailure(errorCode: Int) {
                runOnUiThread {
                    handler.removeCallbacks(stopRunnable)
                    callback = null
                    sendingName = null
                    binding.status.text = getString(R.string.advertise_failed, errorCode)
                }
            }
        }
        callback = cb
        try {
            adv.startAdvertising(settings, data, cb)
        } catch (_: SecurityException) {
            callback = null
            sendingName = null
            binding.status.text = getString(R.string.need_permission)
            return
        }
        handler.postDelayed(stopRunnable, ADVERT_MS)
    }

    private fun stopAdvertising(markSent: Boolean) {
        handler.removeCallbacks(stopRunnable)
        val adv = advertiser
        val cb = callback
        callback = null
        if (adv != null && cb != null) {
            try {
                adv.stopAdvertising(cb)
            } catch (_: SecurityException) {
            }
        }
        val name = sendingName
        sendingName = null
        if (markSent && name != null) {
            binding.status.text = if (countWrapped) {
                getString(R.string.wrapped, name)
            } else {
                getString(R.string.sent, name)
            }
        }
        countWrapped = false
    }

    private fun phoneId(): ByteArray {
        val androidId = Settings.Secure.getString(contentResolver, Settings.Secure.ANDROID_ID).orEmpty()
        return CoffinPacket.phoneId(androidId)
    }

    private fun nextCounter(): Long {
        val prefs = getSharedPreferences("coffin", Context.MODE_PRIVATE)
        val current = prefs.getLong("counter", 0L)
        val next = if (current >= 0xFFFFFFFFL) 1L else current + 1L
        countWrapped = current >= 0xFFFFFFFFL
        prefs.edit().putLong("counter", next).apply()
        return next
    }

    private fun requestPermissions() {
        val needed = requiredPermissions().filter {
            ContextCompat.checkSelfPermission(this, it) != PackageManager.PERMISSION_GRANTED
        }
        if (needed.isEmpty()) {
            return
        }
        val blocked = askedPermissions && needed.any { !shouldShowRequestPermissionRationale(it) }
        if (blocked) {
            startActivity(
                Intent(
                    android.provider.Settings.ACTION_APPLICATION_DETAILS_SETTINGS,
                    Uri.parse("package:$packageName"),
                )
            )
            return
        }
        askedPermissions = true
        permissionLauncher.launch(needed.toTypedArray())
    }

    private fun problemText(): String? {
        if (missingPermissions()) {
            return getString(R.string.need_permission)
        }
        val adapter = adapter() ?: return getString(R.string.no_bluetooth)
        if (!adapter.isEnabled) {
            return getString(R.string.bluetooth_off)
        }
        val adv = adapter.bluetoothLeAdvertiser
        advertiser = adv
        if (adv == null) {
            return getString(R.string.cannot_advertise)
        }
        return null
    }

    private fun adapter(): BluetoothAdapter? {
        return getSystemService(BluetoothManager::class.java)?.adapter
    }

    private fun missingPermissions(): Boolean {
        return requiredPermissions().any {
            ContextCompat.checkSelfPermission(this, it) != PackageManager.PERMISSION_GRANTED
        }
    }

    private fun requiredPermissions(): Array<String> {
        return if (Build.VERSION.SDK_INT >= 31) {
            arrayOf(
                Manifest.permission.BLUETOOTH_ADVERTISE,
                Manifest.permission.BLUETOOTH_CONNECT,
            )
        } else {
            arrayOf(Manifest.permission.ACCESS_FINE_LOCATION)
        }
    }

    private companion object {
        const val ADVERT_MS = 2000L
    }
}
