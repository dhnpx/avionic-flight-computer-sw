#ifndef __BMI088_H__
#define __BMI088_H__


#include <stdint.h>


typedef enum
{
    BMI088_SUCCESS,
    BMI088_FAIL
} bmi088_status_t;


// Defines i2c interaction callbacks
//
// i2c_write : write bytes over i2c
//      data : data buffer to write
//      size : size of total data in bytes
// 
// i2c_read  : write then read bytes over i2c
//      data     : data buffer to write
//      dataSize : size of total data to write
//      rxBuff   : buffer to received data
//      rxSize   : size of expected bytes back
typedef struct
{
    void ( *i2c_write )( uint8_t* data, uint16_t size );
    void ( *i2c_read  )( uint8_t* data, uint16_t dataSize, uint8_t* rxBuff, uint16_t rxSize );
} bmi088_t;


// Read registers starting from an address
//
// NOTE: the bmi088 auto increments the address so that multiple sequential registers can be read
//       in one transaction
//
// devAddr   : slave address of device
//
// startAddr : register address to begin read from
//
// rxBuff    : buffer to hold read back bytes
//
// rxSize    : total bytes to read
//
// bmi       : bmi088 instance
void bmi088_read( uint8_t devAddr, uint8_t startAddr, uint8_t* rxBuff, uint16_t rxSize, bmi088_t* bmi );


// Write to a single register
//
// devAddr : slave address of device
//
// addr    : register address to write to
//
// data    : data to write into register
//
// bmi     : bmi088 instance
void bmi088_write( uint8_t devAddr, uint8_t addr, uint8_t data, bmi088_t* bmi );


#endif
