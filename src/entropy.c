/*
 * entropy.c — mbedTLS entropy callback for cosmocc out-of-tree builds.
 *
 * Cosmo's net/https/getentropy.c calls arc4random_buf() which is not
 * available in cosmocc's libcosmo.a. We provide our own GetEntropy()
 * using the POSIX getentropy() syscall wrapper instead.
 */
#include "libc/stdio/rand.h"
#include "net/https/https.h"

int GetEntropy(void *c, unsigned char *p, size_t n) {
  (void)c;
  /* getentropy() supports up to 256 bytes per call */
  while (n > 0) {
    size_t chunk = n > 256 ? 256 : n;
    if (getentropy(p, chunk) != 0)
      return -1;
    p += chunk;
    n -= chunk;
  }
  return 0;
}
