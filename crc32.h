#ifndef CRC32_HEADER_INCLUDED
#define    CRC32_HEADER_INCLUDED

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include <stdint.h>

uint32_t get_crc32(uint32_t crcinit, uint8_t * bs, uint32_t bssize);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* CRC32_HEADER_INCLUDED */
