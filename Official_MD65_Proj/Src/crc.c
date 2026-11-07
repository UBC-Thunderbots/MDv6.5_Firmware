/**
 ******************************************************************************
 * @file    crc.c
 * @author  UBC Thunderbots
 * @brief   CRC computation module
 ******************************************************************************
 */

#include "crc.h"

const uint8_t crc_table[] = CRC_TABLE;

/**
 * Generates a CRC-8 checksum using the AUTOSAR polynomial (0x2F)
 *
 * This CRC calculation is based off the AUTOSAR Specificiation.
 * It has poly 0x2F, initial value 0xFF, and is finally xor'd with 0xFF.
 *
 * @param data     The array of bytes to calculate a checksum for
 * @param len      The length of the data array
 * @return uint8_t The calculated 8-bit CRC checksum
 */
uint8_t CrcGenerateChecksum(const uint8_t *data, size_t len)
{
  uint8_t crc = 0xFF;

  for (size_t i = 0; i < len; i++) 
  {
    crc = crc_table[crc ^ data[i]];
  }

  return crc ^ 0xFF;
}
