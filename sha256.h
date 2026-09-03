/*
 * Minimal, self-contained SHA-256 implementation for AVR.
 * Standard FIPS 180-4 algorithm - no external library dependency.
 */
#ifndef SHA256_H
#define SHA256_H

#include <stdint.h>
#include <string.h>

class SHA256 {
  public:
    SHA256();
    void update(const uint8_t* data, size_t len);
    void finalize(uint8_t hash[32]);

  private:
    uint32_t state[8];
    uint64_t bitlen;
    uint8_t  buffer[64];
    uint8_t  bufferLen;
    void transform(const uint8_t block[64]);
};

#endif
