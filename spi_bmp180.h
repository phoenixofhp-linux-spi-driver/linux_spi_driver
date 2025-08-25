#ifndef SPI_BMP180_H
#define SPI_BMP180_H

#include <linux/ioctl.h>
#include <linux/types.h>
#ifndef DRIVER_HEADER
#include <stdint.h>
#endif

#define SPI_IOC_MAGIC 'k'

/* IOCTL commands. Values 0x01–0x06 correspond to the commands
   sent by the master (Raspberry Pi) to interact with the slave device. */
#define SPI_CMD_READ_TEMP     _IOR(SPI_IOC_MAGIC, 0x01, float)
#define SPI_CMD_READ_PRESS    _IOR(SPI_IOC_MAGIC, 0x02, float)
#define SPI_CMD_READ_ALT      _IOR(SPI_IOC_MAGIC, 0x03, float)
#define SPI_CMD_CALIBRATE     _IO(SPI_IOC_MAGIC, 0x04)
#define SPI_CMD_READ_CALIB    _IOR(SPI_IOC_MAGIC, 0x05, struct calib_data)
#define SPI_CMD_READ_STUDENT  _IOR(SPI_IOC_MAGIC, 0x06, char *)

struct calib_data {
    int16_t AC1, AC2, AC3;
    uint16_t AC4, AC5, AC6;
    int16_t B1, B2, MB, MC, MD;
};

#endif /* SPI_BMP180_H */
