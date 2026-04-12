#include "netudp_simd.h"
#include <cstring>

namespace netudp {
namespace simd {

/* CRC32C — slicing-by-4 table implementation (Castagnoli polynomial 0x1EDC6F41) */

static const uint32_t g_crc32c_table[256] = {
    0x00000000, 0xF26B8303, 0xE13B70F7, 0x1350F3F4, 0xC79A971F, 0x35F1141C, 0x26A1E7E8, 0xD4CA64EB,
    0x8AD958CF, 0x78B2DBCC, 0x6BE22838, 0x9989AB3B, 0x4D43CFD0, 0xBF284CD3, 0xAC78BF27, 0x5E133C24,
    0x105EC76F, 0xE235446C, 0xF165B798, 0x030E349B, 0xD7C45070, 0x25AFD373, 0x36FF2087, 0xC494A384,
    0x9A879FA0, 0x68EC1CA3, 0x7BBCEF57, 0x89D76C54, 0x5D1D08BF, 0xAF768BBC, 0xBC267848, 0x4E4DFB4B,
    0x20BD8EDE, 0xD2D60DDD, 0xC186FE29, 0x33ED7D2A, 0xE72719C1, 0x154C9AC2, 0x061C6936, 0xF477EA35,
    0xAA64D611, 0x580F5512, 0x4B5FA6E6, 0xB93425E5, 0x6DFE410E, 0x9F95C20D, 0x8CC531F9, 0x7EAEB2FA,
    0x30E349B1, 0xC288CAB2, 0xD1D83946, 0x23B3BA45, 0xF779DEAE, 0x05125DAD, 0x1642AE59, 0xE4292D5A,
    0xBA3A117E, 0x4851927D, 0x5B016189, 0xA96AE28A, 0x7DA08661, 0x8FCB0562, 0x9C9BF696, 0x6EF07595,
    0x417B1DBC, 0xB3109EBF, 0xA0406D4B, 0x522BEE48, 0x86E18AA3, 0x74880BA0, 0x67D8F854, 0x95B37B57,
    0xCBA04773, 0x39CBC470, 0x2A9B3784, 0xD8F0B487, 0x0C3AD06C, 0xFE51536F, 0xED01A09B, 0x1F6A2398,
    0x5127F8D3, 0xA34C7BD0, 0xB01C8824, 0x42770B27, 0x96BD6FCC, 0x64D6ECCF, 0x77861F3B, 0x85ED9C38,
    0xDBFEA01C, 0x2995231F, 0x3AC5D0EB, 0xC8AE53E8, 0x1C643703, 0xEE0FB400, 0xFD5F47F4, 0x0F34C4F7,
    0x61C49362, 0x93AF1061, 0x80FFE395, 0x72946096, 0xA65E047D, 0x5435877E, 0x4765748A, 0xB50EF789,
    0xEB1DCBAD, 0x197648AE, 0x0A26BB5A, 0xF84D3859, 0x2C875CB2, 0xDEECDFB1, 0xCDBC2C45, 0x3FD7AF46,
    0x719A540D, 0x83F1D70E, 0x90A124FA, 0x62CAA7F9, 0xB600C312, 0x446B4011, 0x573BB3E5, 0xA55030E6,
    0xFB430CC2, 0x09288FC1, 0x1A787C35, 0xE813FF36, 0x3CD99BDD, 0xCEB218DE, 0xDDE2EB2A, 0x2F896829,
    0x82D6A4F1, 0x70BD27F2, 0x63EDD406, 0x91865705, 0x454C33EE, 0xB727B0ED, 0xA4774319, 0x561CC01A,
    0x080FFC3E, 0xFA647F3D, 0xE9348CC9, 0x1B5F0FCA, 0xCF956B21, 0x3DFEE822, 0x2EAE1BD6, 0xDCC598D5,
    0x9288639E, 0x60E3E09D, 0x73B31369, 0x81D8906A, 0x5512F481, 0xA7797782, 0xB4298476, 0x46420775,
    0x18513B51, 0xEA3AB852, 0xF96A4BA6, 0x0B01C8A5, 0xDFCBAC4E, 0x2DA02F4D, 0x3EF0DCB9, 0xCCDB5FBA,
    0xA22BE68D, 0x5040658E, 0x4310967A, 0xB17B1579, 0x65B17192, 0x97DAF291, 0x848A0165, 0x76E18266,
    0x28F2BE42, 0xDAD93D41, 0xC989CEB5, 0x3BE24DB6, 0xEF28295D, 0x1D43AA5E, 0x0E1359AA, 0xFC78DAA9,
    0xB23521E2, 0x405EA2E1, 0x530E5115, 0xA165D216, 0x75AFB6FD, 0x87C435FE, 0x9494C60A, 0x66FF4509,
    0x38EC792D, 0xCA87FA2E, 0xD9D709DA, 0x2BBC8AD9, 0xFF76EE32, 0x0D1D6D31, 0x1E4D9EC5, 0xEC261DC6,
    0xC3AD7548, 0x31C6F64B, 0x229605BF, 0xD0FD86BC, 0x0437E257, 0xF65C6154, 0xE50C92A0, 0x176711A3,
    0x49742D87, 0xBB1FAE84, 0xA84F5D70, 0x5A24DE73, 0x8EEEBA98, 0x7C85399B, 0x6FD5CA6F, 0x9DBE496C,
    0xD3F3B227, 0x21983124, 0x32C8C2D0, 0xC0A341D3, 0x14692538, 0xE602A63B, 0xF55255CF, 0x0739D6CC,
    0x592AEAe8, 0xAB4169EB, 0xB8119A1F, 0x4A7A191C, 0x9EB07DF7, 0x6CDBFEF4, 0x7F8B0D00, 0x8DE08E03,
    0xE3B07AF6, 0x11DBF9F5, 0x028B0A01, 0xF0E08902, 0x242AEDE9, 0xD641EEEA, 0xC5111D1E, 0x377A9E1D,
    0x6969A239, 0x9B02213A, 0x8852D2CE, 0x7A3951CD, 0xAEF33526, 0x5C98B625, 0x4FC845D1, 0xBDA3C6D2,
    0xF3EE3D99, 0x0185BE9A, 0x12D54D6E, 0xE0BECE6D, 0x3474AA86, 0xC61F2985, 0xD54FDA71, 0x27245972,
    0x79376556, 0x8B5CE655, 0x980C15A1, 0x6A6796A2, 0xBEADF249, 0x4CC6714A, 0x5F9682BE, 0xADFD01BD,
};

static uint32_t generic_crc32c(const uint8_t* data, int len) {
    uint32_t crc = 0xFFFFFFFF;
    for (int i = 0; i < len; ++i) {
        crc = g_crc32c_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFF;
}

static void generic_memcpy_nt(void* dst, const void* src, size_t len) {
    std::memcpy(dst, src, len);
}

static void generic_memset_zero(void* dst, size_t len) {
    std::memset(dst, 0, len);
}

static int generic_ack_bits_scan(uint32_t bits, int* indices) {
    int count = 0;
    for (int i = 0; i < 32; ++i) {
        if ((bits & (1U << i)) != 0) {
            indices[count++] = i;
        }
    }
    return count;
}

static int generic_popcount32(uint32_t v) {
    v = v - ((v >> 1) & 0x55555555U);
    v = (v & 0x33333333U) + ((v >> 2) & 0x33333333U);
    return static_cast<int>(((v + (v >> 4)) & 0x0F0F0F0FU) * 0x01010101U >> 24);
}

static int generic_replay_check(const uint64_t* window, uint64_t seq, int size) {
    for (int i = 0; i < size; ++i) {
        if (window[i] == seq) {
            return 1; /* duplicate found */
        }
    }
    return 0;
}

static int generic_fragment_bitmask_complete(const uint8_t* mask, int total) {
    for (int i = 0; i < total; ++i) {
        if ((mask[i / 8] & (1U << (i % 8))) == 0) {
            return 0;
        }
    }
    return 1;
}

static int generic_fragment_next_missing(const uint8_t* mask, int total) {
    for (int i = 0; i < total; ++i) {
        if ((mask[i / 8] & (1U << (i % 8))) == 0) {
            return i;
        }
    }
    return -1;
}

static void generic_accumulate_u64(uint64_t* dst, const uint64_t* src, int count) {
    for (int i = 0; i < count; ++i) {
        dst[i] += src[i];
    }
}

static int generic_addr_equal(const void* a, const void* b, int len) {
    return std::memcmp(a, b, static_cast<size_t>(len)) == 0 ? 1 : 0;
}

const SimdOps g_ops_generic = {
    generic_crc32c,
    generic_memcpy_nt,
    generic_memset_zero,
    generic_ack_bits_scan,
    generic_popcount32,
    generic_replay_check,
    generic_fragment_bitmask_complete,
    generic_fragment_next_missing,
    generic_accumulate_u64,
    generic_addr_equal,
};

} // namespace simd
} // namespace netudp
