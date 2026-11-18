#include "rpi.h"
#include "i2c.h"


#include "se050.h"

#define FILL_ME 42
#define T1_EXTRA_LEN (T1_HDR_LEN + T1_CRC_LEN)

// use this to toggle the N(S) bit in the I-blocks, to test the R-blocks and S-blocks handling.
static int n_s_toggle = 0;
static int n_r_toggle = 0;

// T1 constants
enum
{
    T1_NAD_HD2SE = 0x5A,
    T1_NAD_SE2HD = 0xA5,
    T1_HDR_LEN = 3,
    T1_CRC_LEN = 2,
    T1_LEN_EXTENDED = 0xFF,
    T1_IFSC_DEFAULT = 254,
    T1_FRAME_MAX = T1_HDR_LEN + 2 + 255 + T1_CRC_LEN,

    // I-block: 0 N(S) M 0 0 0 0 0
    T1_I_NS = 1 << 6,
    T1_I_M = 1 << 5,
    // R-block: 1 0 0 N(R) 0 0 err err
    T1_R = 0x80,
    T1_R_NR = 1 << 4,
    T1_R_ERR_CRC = 0x01,
    T1_R_ERR_OTHER = 0x02,
    // S-block: may need to support S-blocks for WTX, resync, etc. but not absolutely needed for this lab.
    T1_S_RESP = 0x20,
    T1_S_RESYNC_REQ = 0xC0,
    T1_S_IFS_REQ = 0xC1,
    T1_S_ABORT_REQ = 0xC2,
    T1_S_WTX_REQ = 0xC3,
    T1_S_END_APDU_REQ = 0xC5,
    T1_S_CHIP_RESET_REQ = 0xC6,
    T1_S_GET_ATR_REQ = 0xC7,
    T1_S_SOFT_RESET_REQ = 0xCF,
};

// SE050 constants
enum
{
    // 4.3.3 instruction
    INS_WRITE = 0x01,
    INS_READ = 0x02,
    INS_CRYPTO = 0x03,
    INS_MGMT = 0x04,
    INS_TRANSIENT = 0x80,
    // 4.3.4 P1
    P1_DEFAULT = 0x00,
    P1_HMAC = 0x05,
    P1_MAC = 0x0D,
    // 4.3.5 P2
    P2_DEFAULT = 0x00,
    P2_DELETE_OBJECT = 0x28,
    P2_GENERATE_ONESHOT = 0x45,
    P2_VALIDATE_ONESHOT = 0x46,
    // 4.3.9 tags
    TAG_1 = 0x41,
    TAG_2 = 0x42,
    TAG_3 = 0x43,
    TAG_4 = 0x44,
    TAG_5 = 0x45,
    // 4.3.18 MACAlgo
    MAC_HMAC_SHA1 = 0x18,

    OBJECT_ID_SIZE = 4,
    HMAC_SHA1_LEN = 20,
};

static uint16_t crc16_x25_step(uint16_t crc, const uint8_t *data, uint32_t length)
{
    // use as is
    for (uint32_t i = 0; i < length; i++)
    {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++)
            if (crc & 0x0001)
                crc = (crc >> 1) ^ 0x8408;
            else
                crc >>= 1;
    }
    return crc;
}

uint16_t crc16_x25(const uint8_t *data, uint32_t length)
{
    // use as is
    return ~crc16_x25_step(0xFFFF, data, length);
}

int t1_send(uint8_t pcb, uint8_t len, uint8_t *buf)
{
    uint8_t frame[len + T1_EXTRA_LEN];
    
    frame[0] = T1_NAD_HD2SE;
    frame[1] = pcb;
    frame[2] = len;
    for (int i = 0; i < len; i++)
        frame[T1_HDR_LEN + i] = buf[i];
    
    uint16_t crc = crc16_x25(frame, T1_HDR_LEN + len);
    frame[T1_HDR_LEN + len] = crc & 0xFF;
    frame[T1_HDR_LEN + len + 1] = (crc >> 8) & 0xFF;

    return i2c_xfer_poll(0, frame, T1_EXTRA_LEN + len);
}

int t1_recv(uint8_t *pcb, uint8_t *buf, uint8_t max_len)
{
    uint8_t header[3];
    // read header, decode header
    if (i2c_xfer_poll(1, header, T1_HDR_LEN) < 0)
        return -1;

    // Check NAD code
    if(header[0] != T1_NAD_SE2HD)
        return -1;

    // write decoded pcb to output
    *pcb = header[1];
    int len = header[2];
    if (len > max_len)
        return -2;
    
    // read buffer out
    if (len > 0 && i2c_xfer_poll(1, buf, len) < 0)
        return -1;

    uint8_t crc_bytes[2];
    // read CRC, calculate on buf, and compare
    if (i2c_xfer_poll(1, crc_bytes, T1_CRC_LEN) < 0)
        return -1;
    
    uint16_t crc = (crc_bytes[1] << 8) | crc_bytes[0];
    uint16_t calc = crc16_x25_step(0xFFFF, header, T1_HDR_LEN);
    calc = ~crc16_x25_step(calc, buf, len);
    if (calc != crc)
        return -1;
    
    return len; // success
}

uint8_t *tlv_encode(uint8_t *tlv, uint8_t tag, const uint8_t *val, uint8_t len)
{
    // encode tlv into tlv buffer, return pointer to next free byte in tlv buffer (so that you can chain multiple TLVs encode together)
    *tlv++ = tag;
    if(len >= 0x80)
        *tlv++ = 0x81;
    *tlv++ = len;

    for(int i = 0; i < len; i++)
        *tlv++ = val[i];

    return tlv;
}

uint8_t *tlv_decode(uint8_t *tlv, uint8_t tlv_len, uint8_t *tag, uint8_t **val, uint16_t *val_len)
{
    // decode tlv from tlv buffer, return pointer to next TLV in buffer
    // (or NULL if tlv_len is too short for the header or the value)
    uint8_t *tlv_start = tlv;
    *tag = *tlv++;
    
    if(*tlv < 0x80) {
        *val_len = *tlv++;
    } else if(*tlv == 0x81) {
        tlv++;
        *val_len = *tlv++;
    } else if(*tlv == 0x82) {
        tlv++;
        *val_len = (*tlv++) << 8;
        *val_len |= *tlv++;
    } else {
        return NULL;
    }

    size_t hdr_len = tlv - tlv_start;
    if(tlv_len < hdr_len + *val_len)
        return NULL;

    *val = tlv;

    return tlv + *val_len;
}

int apdu_assemble(uint8_t *apdu_frame, uint8_t ins, uint8_t p1, uint8_t p2, uint8_t *data, uint8_t data_len, uint32_t need_resp)
{
    // need_resp can be used for both the flag to indicate if response is needed, and also the Le value if needed.
    // Le = (need_resp >> 8) & 0xff and lower 8 bits is boolean flag to indicate if Le is needed.
    // assemble APDU into output buffer, return length of APDU / error of your choice (>= 0 for success, < 0 for error)
    int ptr = 0;
    
    apdu_frame[ptr++] = 0x80;
    apdu_frame[ptr++] = ins;
    apdu_frame[ptr++] = p1;
    apdu_frame[ptr++] = p2;

    if(data_len > 0)
    {
        apdu_frame[ptr++] = data_len;
        for(int i = 0; i < data_len; i++)
            apdu_frame[ptr++] = data[i];
    }

    if(need_resp & 0xff)
        apdu_frame[ptr++] = (need_resp >> 8) & 0xff; // Le
    return ptr;
}

int rapdu_decode(uint8_t *rapdu_frame, int len, uint8_t **body, uint16_t *status)
{
    // return <0 to indicate error, >=0 for body length
    // can be simple as moving the body pointer to the start of the body, and calculating the length.
    if(len < 2)
        return -1;
    *body = rapdu_frame;
    *status = (rapdu_frame[len - 2] << 8) | rapdu_frame[len - 1];
    return len - 2;
}


int se050_AppletSelect()
{
    // put in SE050 applet AID
    uint8_t cmd[] = {   0x00, 0xa4, 0x04, 0x00,
                        0x10, 0xA0, 0x00, 0x00,
                        0x03, 0x96, 0x54, 0x53,
                        0x00, 0x00, 0x00, 0x01,
                        0x03, 0x00, 0x00, 0x00,
                        0x00, 0x00};
    
    if(t1_send(n_s_toggle ? 0x40 : 0x00, sizeof(cmd), cmd) < 0) return -1;
    n_s_toggle ^= 1;

    uint8_t pcb;
    uint8_t resp[254];
    int size = t1_recv(&pcb, resp, sizeof(resp));
    if(size < 2) return -1;

    if(resp[size - 2] != 0x90 || resp[size - 1] != 0x00)
        return -1;

    output("AppletSelect: %02x %02x %02x\n", resp[0], resp[1], resp[2]);
    
    return 0;
}

int se050_WriteSymmKey(uint8_t *keyid, uint8_t *key, uint8_t key_len)
{
    // keyid is a four byte array, key is the bin of the key in key_len bytes.
    // return >=0 for success, <0 for error

    // TLV[TAG_1] keyid, TLV[TAG_3] key
    uint8_t data[64];
    uint8_t *p = tlv_encode(data, TAG_1, keyid, OBJECT_ID_SIZE);
    p = tlv_encode(p, TAG_3, key, key_len);

    // no Le
    uint8_t apdu[80];
    int n = apdu_assemble(apdu, INS_WRITE, P1_HMAC, P2_DEFAULT, data, p - data, 0);

    if(t1_send(n_s_toggle ? 0x40 : 0x00, n, apdu) < 0) return -1;
    n_s_toggle ^= 1;

    uint8_t pcb;
    uint8_t resp[254];
    int size = t1_recv(&pcb, resp, sizeof(resp));
    if(size < 0) return -1;

    uint8_t *body;
    uint16_t status;
    if(rapdu_decode(resp, size, &body, &status) < 0 || status != 0x9000)
        return -1;

    return 0;
}

int se050_MACOneShot_HMAC_SHA1(uint8_t *keyid, uint8_t *msg, uint8_t msg_len, uint8_t *mac)
{
    // keyid is a four byte array, msg is the bin of the message in msg_len bytes.
    // mac is the output buffer for the MAC, with fixed length of 20 bytes for HMAC-SHA1.
    // return >=0 for success, <0 for error

    uint8_t algo = MAC_HMAC_SHA1;
    uint8_t data[64];
    uint8_t *p = tlv_encode(data, TAG_1, keyid, OBJECT_ID_SIZE);
    p = tlv_encode(p, TAG_2, &algo, 1);
    p = tlv_encode(p, TAG_3, msg, msg_len);

    uint8_t apdu[80];
    int len = apdu_assemble(apdu, INS_CRYPTO, P1_MAC, P2_GENERATE_ONESHOT, data, p - data, 1);

    if(t1_send(n_s_toggle ? 0x40 : 0x00, len, apdu) < 0) return -1;
    n_s_toggle ^= 1;

    uint8_t pcb;
    uint8_t resp[254];
    int size = t1_recv(&pcb, resp, sizeof(resp));
    if(size < 0) return -1;

    uint8_t *body;
    uint16_t status;
    int body_len = rapdu_decode(resp, size, &body, &status);
    if(body_len < 0 || status != 0x9000)
        return -1;

    // MAC is in TLV[TAG_1]
    uint8_t tag, *val;
    uint16_t val_len;
    if(!tlv_decode(body, body_len, &tag, &val, &val_len) || tag != TAG_1 || val_len != HMAC_SHA1_LEN)
        return -1;

    memcpy(mac, val, HMAC_SHA1_LEN);
    return 0;
}

int se050_DeleteSecureObject(uint8_t *keyid)
{
    // keyid is a four byte array.
    // return >=0 for success, <0 for error

    // TLV[TAG_1] keyid
    uint8_t data[8];
    uint8_t *p = tlv_encode(data, TAG_1, keyid, OBJECT_ID_SIZE);

    // no Le
    uint8_t apdu[16];
    int len = apdu_assemble(apdu, INS_MGMT, P1_DEFAULT, P2_DELETE_OBJECT, data, p - data, 0);

    if(t1_send(n_s_toggle ? 0x40 : 0x00, len, apdu) < 0) return -1;
    n_s_toggle ^= 1;

    uint8_t pcb;
    uint8_t resp[254];
    int size = t1_recv(&pcb, resp, sizeof(resp));
    if(size < 0) return -1;

    uint8_t *body;
    uint16_t status;
    if(rapdu_decode(resp, size, &body, &status) < 0 || status != 0x9000)
        return -1;

    return 0;
}

int totp_generate(uint8_t *keyid, int64_t unix_time)
{
    // keyid is a four byte array, time is the current unix time in seconds.
    // return the 6 digit TOTP code, or negative value for error.

    uint64_t t = unix_time / 30;
    uint8_t msg[8];
    for(int i = 7; i >= 0; i--, t >>= 8)
        msg[i] = t & 0xff;

    uint8_t mac[HMAC_SHA1_LEN];
    if(se050_MACOneShot_HMAC_SHA1(keyid, msg, sizeof(msg), mac) < 0)
        return -1;

    // truncation
    int off = mac[19] & 0x0f;
    uint32_t code = (mac[off] & 0x7f) << 24 | mac[off + 1] << 16 | mac[off + 2] << 8 | mac[off + 3];
    return code % 1000000;
}