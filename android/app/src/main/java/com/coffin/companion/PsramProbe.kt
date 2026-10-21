package com.coffin.companion

import android.content.Context
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbManager
import com.hoho.android.usbserial.driver.UsbSerialPort
import com.hoho.android.usbserial.driver.UsbSerialProber
import java.io.ByteArrayOutputStream
import java.io.IOException

data class BoardInfo(val model: String, val psram: String?)

/** Read the chip id and, on an ESP32-S3, the package PSRAM size. Resets the chip into the ROM. */
object PsramProbe {
    fun read(context: Context, device: UsbDevice): BoardInfo {
        val usb = context.getSystemService(UsbManager::class.java)
            ?: throw IOException("No USB host")
        val driver = UsbSerialProber.getDefaultProber().probeDevice(device)
            ?: throw IOException("No serial interface")
        val connection = usb.openDevice(device) ?: throw IOException("USB port did not open")
        val port = driver.ports[0]
        val serial = try {
            port.open(connection)
            CoffinSerial(port, connection)
        } catch (e: Exception) {
            connection.close()
            throw e
        }
        serial.use { link ->
            var last: Exception? = null
            repeat(4) {
                try {
                    link.usbJtagReset()
                    Thread.sleep(150)
                    link.purgeInput()
                    sync(link)
                    val id = chipId(link)
                    val psram = if (id == ESP32_S3) psramLabel(psramCap(link)) else null
                    return BoardInfo(modelName(id), psram)
                } catch (e: Exception) {
                    last = e
                    link.purgeInput()
                }
            }
            throw last ?: IOException("No answer from the ROM")
        }
    }

    private fun psramLabel(cap: Int): String? = when (cap) {
        0 -> null
        1 -> "8 MB"
        2 -> "2 MB"
        3 -> "16 MB"
        4 -> "4 MB"
        else -> "unknown ($cap)"
    }

    /** Chip id from the ROM security-info block. 9 is the ESP32-S3. */
    private fun chipId(link: CoffinSerial): Int {
        val response = command(link, ESP_GET_SECURITY_INFO, ByteArray(0), 1000)
        val body = response.data
        if (body.size < 22 || body[20].toInt() != 0) {
            throw IOException("Chip id read failed")
        }
        var id = 0
        for (i in 0 until 4) {
            id = id or ((body[12 + i].toInt() and 0xFF) shl (8 * i))
        }
        return id
    }

    private fun modelName(id: Int): String = when (id) {
        0 -> "ESP32"
        2 -> "ESP32-S2"
        5 -> "ESP32-C3"
        ESP32_S3 -> "ESP32-S3"
        12 -> "ESP32-C2"
        13 -> "ESP32-C6"
        16 -> "ESP32-H2"
        18 -> "ESP32-P4"
        20 -> "ESP32-C61"
        23 -> "ESP32-C5"
        else -> "chip id $id"
    }

    /** Same packing as esptool's ESP32-S3 get_psram_cap. */
    private fun psramCap(link: CoffinSerial): Int {
        val low = (readReg(link, EFUSE_BLOCK1 + 16) shr 3) and 0x03
        val high = (readReg(link, EFUSE_BLOCK1 + 20) shr 19) and 0x01
        return ((high shl 2) or low).toInt()
    }

    private fun readReg(link: CoffinSerial, addr: Long): Long {
        val data = byteArrayOf(
            addr.toByte(),
            (addr shr 8).toByte(),
            (addr shr 16).toByte(),
            (addr shr 24).toByte(),
        )
        val response = command(link, ESP_READ_REG, data, 1000)
        val body = response.data
        if (body.size < 2 || body[0].toInt() != 0) {
            throw IOException("Register read failed")
        }
        return response.value
    }

    private fun sync(link: CoffinSerial) {
        val payload = byteArrayOf(0x07, 0x07, 0x12, 0x20) + ByteArray(32) { 0x55 }
        command(link, ESP_SYNC, payload, 300)
        repeat(7) { command(link, null, ByteArray(0), 200) }
    }

    private fun command(link: CoffinSerial, op: Int?, data: ByteArray, timeoutMs: Int): Response {
        if (op != null) {
            val packet = ByteArray(8 + data.size)
            packet[1] = op.toByte()
            packet[2] = (data.size and 0xFF).toByte()
            packet[3] = ((data.size shr 8) and 0xFF).toByte()
            System.arraycopy(data, 0, packet, 8, data.size)
            link.write(slipEncode(packet))
        }
        val frame = link.readSlip(timeoutMs)
        if (frame.size < 8 || frame[0].toInt() != 1) {
            throw IOException("ROM did not answer")
        }
        if (op != null && frame[1].toInt() != op) {
            throw IOException("ROM answered a different command")
        }
        var value = 0L
        for (i in 0 until 4) {
            value = value or ((frame[4 + i].toLong() and 0xFF) shl (8 * i))
        }
        return Response(value, frame.copyOfRange(8, frame.size))
    }

    private fun slipEncode(payload: ByteArray): ByteArray {
        val out = ByteArrayOutputStream(payload.size + 8)
        out.write(0xC0)
        for (b in payload) {
            when (b) {
                0xC0.toByte() -> {
                    out.write(0xDB)
                    out.write(0xDC)
                }
                0xDB.toByte() -> {
                    out.write(0xDB)
                    out.write(0xDD)
                }
                else -> out.write(b.toInt())
            }
        }
        out.write(0xC0)
        return out.toByteArray()
    }

    private class Response(val value: Long, val data: ByteArray)

    private const val ESP_SYNC = 0x08
    private const val ESP_READ_REG = 0x0A
    private const val ESP_GET_SECURITY_INFO = 0x14
    private const val ESP32_S3 = 9
    private const val EFUSE_BLOCK1 = 0x60007044L
}

private class CoffinSerial(
    private val port: UsbSerialPort,
    private val connection: android.hardware.usb.UsbDeviceConnection,
) : AutoCloseable {
    @Volatile private var open = true
    private val lock = Object()
    private var buffer = ByteArray(64 * 1024)
    private var head = 0
    private var size = 0

    private val reader = Thread {
        val buf = ByteArray(4096)
        while (open) {
            val n = try {
                port.read(buf, 200)
            } catch (_: Exception) {
                break
            }
            if (n <= 0) {
                continue
            }
            synchronized(lock) {
                for (i in 0 until n) {
                    if (size == buffer.size) {
                        head = (head + 1) % buffer.size
                        size--
                    }
                    buffer[(head + size) % buffer.size] = buf[i]
                    size++
                }
                lock.notifyAll()
            }
        }
        synchronized(lock) { lock.notifyAll() }
    }.apply { isDaemon = true; name = "coffin-usb" }

    init {
        port.setParameters(115200, 8, UsbSerialPort.STOPBITS_1, UsbSerialPort.PARITY_NONE)
        port.dtr = false
        port.rts = false
        reader.start()
    }

    fun write(data: ByteArray) {
        port.write(data, 2000)
    }

    fun purgeInput() {
        synchronized(lock) {
            head = 0
            size = 0
        }
        try {
            port.purgeHwBuffers(false, true)
        } catch (_: Exception) {
        }
    }

    /** USB-Serial-JTAG download-mode pulse. true asserts the line. */
    fun usbJtagReset() {
        setRts(false)
        setDtr(false)
        Thread.sleep(100)
        setDtr(true)
        setRts(false)
        Thread.sleep(100)
        setRts(true)
        setDtr(false)
        setRts(true)
        Thread.sleep(100)
        setDtr(false)
        setRts(false)
    }

    fun readSlip(timeoutMs: Int): ByteArray {
        val deadline = System.nanoTime() + timeoutMs * 1_000_000L
        val out = ByteArrayOutputStream()
        var started = false
        var escape = false
        while (System.nanoTime() < deadline) {
            val b = readByte(deadline) ?: break
            if (!started) {
                if (b == 0xC0.toByte()) {
                    started = true
                }
                continue
            }
            if (escape) {
                escape = false
                out.write(if (b == 0xDC.toByte()) 0xC0 else 0xDB)
                continue
            }
            when (b) {
                0xDB.toByte() -> escape = true
                0xC0.toByte() -> if (out.size() > 0) return out.toByteArray()
                else -> out.write(b.toInt())
            }
        }
        throw IOException("Timed out waiting for the ROM")
    }

    private fun readByte(deadline: Long): Byte? {
        synchronized(lock) {
            while (size == 0 && open && System.nanoTime() < deadline) {
                val waitMs = ((deadline - System.nanoTime()) / 1_000_000L).coerceIn(1, 50)
                lock.wait(waitMs)
            }
            if (size == 0) {
                return null
            }
            val b = buffer[head]
            head = (head + 1) % buffer.size
            size--
            return b
        }
    }

    private fun setDtr(asserted: Boolean) {
        try {
            port.dtr = asserted
        } catch (_: Exception) {
        }
    }

    private fun setRts(asserted: Boolean) {
        try {
            port.rts = asserted
        } catch (_: Exception) {
        }
    }

    override fun close() {
        open = false
        try {
            port.close()
        } catch (_: Exception) {
        }
        try {
            connection.close()
        } catch (_: Exception) {
        }
        synchronized(lock) { lock.notifyAll() }
    }
}
