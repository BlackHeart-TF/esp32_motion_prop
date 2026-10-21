package com.coffin.companion

import java.security.MessageDigest
import javax.crypto.Mac
import javax.crypto.spec.SecretKeySpec

/**
 * Advertisement body for the coffin. Matches main/config.h.
 * Android writes the company id itself, so this array starts at the magic.
 * The tag is HMAC-SHA1 over the magic, command, phone id, and counter.
 */
object CoffinPacket {
    const val COMPANY_ID = 0xFFFF
    const val CMD_SCARE = 0x01
    const val CMD_OPEN = 0x02
    const val CMD_RESET = 0x03

    private val SECRET = byteArrayOf(
        0x6B, 0xC1.toByte(), 0x09, 0xE4.toByte(), 0x52, 0x8A.toByte(), 0x17, 0xD3.toByte(),
        0xF0.toByte(), 0x3E, 0x91.toByte(), 0x44, 0xAB.toByte(), 0x76, 0x2C, 0x58, 0x0F,
        0xDE.toByte(), 0x85.toByte(), 0x19,
    )

    fun phoneId(androidId: String): ByteArray {
        val digest = MessageDigest.getInstance("SHA-256").digest(androidId.toByteArray(Charsets.UTF_8))
        val id = digest.copyOf(4)
        if (id.all { it == 0.toByte() }) {
            id[0] = 1
        }
        return id
    }

    fun payload(cmd: Int, phoneId: ByteArray, counter: Long): ByteArray {
        val msg = ByteArray(13)
        msg[0] = 0xC0.toByte()
        msg[1] = 0xFF.toByte()
        msg[2] = 0x1E
        msg[3] = 0x57
        msg[4] = cmd.toByte()
        phoneId.copyInto(msg, 5, 0, 4)
        writeLe32(msg, 9, counter)
        val mac = Mac.getInstance("HmacSHA1")
        mac.init(SecretKeySpec(SECRET, "HmacSHA1"))
        val tag = mac.doFinal(msg)
        val out = ByteArray(21)
        msg.copyInto(out, 0, 0, 13)
        tag.copyInto(out, 13, 0, 8)
        return out
    }

    private fun writeLe32(out: ByteArray, at: Int, value: Long) {
        var bits = value
        for (i in 0 until 4) {
            out[at + i] = (bits and 0xff).toByte()
            bits = bits ushr 8
        }
    }
}
