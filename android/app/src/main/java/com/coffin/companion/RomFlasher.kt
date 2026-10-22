package com.coffin.companion

import android.content.Context
import android.hardware.usb.UsbDevice
import java.io.File
import java.io.IOException
import java.security.MessageDigest

/** Writes a merged image at 0x0 through the ESP32-S3 ROM loader, checks its MD5, then restarts the board. */
object RomFlasher {
    fun flash(context: Context, device: UsbDevice, bin: File, onProgress: (Int) -> Unit) {
        var image = bin.readBytes()
        if (image.size % 4 != 0) {
            image += ByteArray(4 - image.size % 4) { 0xFF.toByte() }
        }
        if (image.size < 8 || image[0] != 0xE9.toByte()) {
            throw IOException("${bin.name} is not a firmware image")
        }
        val flashSize = 1 shl (20 + ((image[3].toInt() shr 4) and 0x0F))
        val md5 = MessageDigest.getInstance("MD5").digest(image)
            .joinToString("") { "%02x".format(it) }
        val blocks = (image.size + BLOCK - 1) / BLOCK
        val eraseMs = maxOf(10_000L, image.size.toLong() * 30_000 / (1024 * 1024)).toInt()

        PsramProbe.withRom(context, device) { link ->
            send(link, ESP_SPI_ATTACH, u32(0) + ByteArray(4), 3000)
            send(
                link,
                ESP_SPI_SET_PARAMS,
                u32(0) + u32(flashSize) + u32(64 * 1024) + u32(4096) + u32(256) + u32(0xFFFF),
                3000,
            )
            send(link, ESP_FLASH_BEGIN, u32(image.size) + u32(blocks) + u32(BLOCK) + u32(0) + u32(0), eraseMs)
            for (seq in 0 until blocks) {
                val start = seq * BLOCK
                val chunk = ByteArray(BLOCK) { 0xFF.toByte() }
                System.arraycopy(image, start, chunk, 0, minOf(BLOCK, image.size - start))
                send(link, ESP_FLASH_DATA, u32(BLOCK) + u32(seq) + u32(0) + u32(0) + chunk, 5000, checksum(chunk))
                onProgress((seq + 1) * 100 / blocks)
            }
            val body = PsramProbe.command(
                link,
                ESP_SPI_FLASH_MD5,
                u32(0) + u32(image.size) + u32(0) + u32(0),
                30_000,
            ).data
            if (body.size < 33 || body[32].toInt() != 0) {
                throw IOException("Flash check failed")
            }
            if (String(body, 0, 32, Charsets.US_ASCII).lowercase() != md5) {
                throw IOException("Written image does not match the download")
            }
            reboot(link)
        }
    }

    /** Same order as esptool on the ESP32-S3: clear forced download boot, then let the RTC watchdog reset the chip. */
    private fun reboot(link: CoffinSerial) {
        try {
            writeReg(link, RTC_CNTL_OPTION1_REG, 0, FORCE_DOWNLOAD_BOOT)
            writeReg(link, RTC_CNTL_WDTWPROTECT_REG, RTC_CNTL_WDT_WKEY)
            writeReg(link, RTC_CNTL_WDTCONFIG1_REG, 5000)
            writeReg(link, RTC_CNTL_WDTCONFIG0_REG, (1 shl 31) or (5 shl 28) or (1 shl 8) or 2)
            writeReg(link, RTC_CNTL_WDTWPROTECT_REG, 0)
        } catch (_: Exception) {
        }
        Thread.sleep(300)
        link.hardReset()
    }

    private fun writeReg(link: CoffinSerial, addr: Int, value: Int, mask: Int = -1) {
        send(link, ESP_WRITE_REG, u32(addr) + u32(value) + u32(mask) + u32(0), 1000)
    }

    private fun send(link: CoffinSerial, op: Int, data: ByteArray, timeoutMs: Int, checksum: Int = 0) {
        val body = PsramProbe.command(link, op, data, timeoutMs, checksum).data
        if (body.isEmpty() || body[0].toInt() != 0) {
            val code = if (body.size > 1) body[1].toInt() and 0xFF else -1
            throw IOException("ROM refused command 0x%02x (error 0x%02x)".format(op, code))
        }
    }

    private fun checksum(data: ByteArray): Int {
        var sum = 0xEF
        for (b in data) {
            sum = sum xor (b.toInt() and 0xFF)
        }
        return sum
    }

    private fun u32(v: Int) = byteArrayOf(
        v.toByte(),
        (v shr 8).toByte(),
        (v shr 16).toByte(),
        (v shr 24).toByte(),
    )

    private const val BLOCK = 0x400
    private const val ESP_FLASH_BEGIN = 0x02
    private const val ESP_FLASH_DATA = 0x03
    private const val ESP_SPI_SET_PARAMS = 0x0B
    private const val ESP_SPI_ATTACH = 0x0D
    private const val ESP_SPI_FLASH_MD5 = 0x13
    private const val ESP_WRITE_REG = 0x09

    private const val RTC_CNTL_BASE = 0x60008000
    private const val RTC_CNTL_WDTCONFIG0_REG = RTC_CNTL_BASE + 0x98
    private const val RTC_CNTL_WDTCONFIG1_REG = RTC_CNTL_BASE + 0x9C
    private const val RTC_CNTL_WDTWPROTECT_REG = RTC_CNTL_BASE + 0xB0
    private const val RTC_CNTL_OPTION1_REG = RTC_CNTL_BASE + 0x12C
    private const val RTC_CNTL_WDT_WKEY = 0x50D83AA1
    private const val FORCE_DOWNLOAD_BOOT = 0x1
}
