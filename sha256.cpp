#include <Arduino.h>
#include <avr/pgmspace.h>
#include "sha256.h"

static const uint32_t K[64] PROGMEM = {
  0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
  0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
  0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
  0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
  0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
  0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
  0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
  0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

#define ROTR(x,n) (((x) >> (n)) | ((x) << (32 - (n))))
#define CH(x,y,z)  (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x,y,z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x) (ROTR(x,2) ^ ROTR(x,13) ^ ROTR(x,22))
#define EP1(x) (ROTR(x,6) ^ ROTR(x,11) ^ ROTR(x,25))
#define SIG0(x) (ROTR(x,7) ^ ROTR(x,18) ^ ((x) >> 3))
#define SIG1(x) (ROTR(x,17) ^ ROTR(x,19) ^ ((x) >> 10))

SHA256::SHA256() {
  bitlen = 0;
  bufferLen = 0;
  state[0]=0x6a09e667; state[1]=0xbb67ae85; state[2]=0x3c6ef372; state[3]=0xa54ff53a;
  state[4]=0x510e527f; state[5]=0x9b05688c; state[6]=0x1f83d9ab; state[7]=0x5be0cd19;
}

void SHA256::transform(const uint8_t block[64]) {
  uint32_t w[64];
  for (int i = 0; i < 16; i++) {
    w[i] = ((uint32_t)block[i*4] << 24) | ((uint32_t)block[i*4+1] << 16) |
           ((uint32_t)block[i*4+2] << 8) | ((uint32_t)block[i*4+3]);
  }
  for (int i = 16; i < 64; i++) {
    w[i] = SIG1(w[i-2]) + w[i-7] + SIG0(w[i-15]) + w[i-16];
  }

  uint32_t a=state[0], b=state[1], c=state[2], d=state[3];
  uint32_t e=state[4], f=state[5], g=state[6], h=state[7];

  for (int i = 0; i < 64; i++) {
    uint32_t k = pgm_read_dword(&K[i]);
    uint32_t t1 = h + EP1(e) + CH(e,f,g) + k + w[i];
    uint32_t t2 = EP0(a) + MAJ(a,b,c);
    h=g; g=f; f=e; e=d+t1;
    d=c; c=b; b=a; a=t1+t2;
  }

  state[0]+=a; state[1]+=b; state[2]+=c; state[3]+=d;
  state[4]+=e; state[5]+=f; state[6]+=g; state[7]+=h;
}

void SHA256::update(const uint8_t* data, size_t len) {
  for (size_t i = 0; i < len; i++) {
    buffer[bufferLen++] = data[i];
    bitlen += 8;
    if (bufferLen == 64) {
      transform(buffer);
      bufferLen = 0;
    }
  }
}

void SHA256::finalize(uint8_t hash[32]) {
  uint64_t bitlenSnapshot = bitlen;
  uint8_t pad = 0x80;
  update(&pad, 1);

  uint8_t zero = 0x00;
  while (bufferLen != 56) {
    update(&zero, 1);
  }

  uint8_t lenBytes[8];
  for (int i = 0; i < 8; i++) {
    lenBytes[7 - i] = (uint8_t)(bitlenSnapshot >> (8 * i));
  }
  // Bypass update()'s bit counting for the length field itself.
  for (int i = 0; i < 8; i++) {
    buffer[bufferLen++] = lenBytes[i];
    if (bufferLen == 64) {
      transform(buffer);
      bufferLen = 0;
    }
  }

  for (int i = 0; i < 8; i++) {
    hash[i*4]   = (uint8_t)(state[i] >> 24);
    hash[i*4+1] = (uint8_t)(state[i] >> 16);
    hash[i*4+2] = (uint8_t)(state[i] >> 8);
    hash[i*4+3] = (uint8_t)(state[i]);
  }
}
