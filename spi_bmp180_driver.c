#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>
#include <linux/delay.h>    // для msleep()

#define DRIVER_HEADER
#include "spi_bmp180.h"

static struct spi_device *spi_dev;
static DEFINE_MUTEX(spi_lock);

/* Функции file_operations */
static int spi_bmp180_open(struct inode *inode, struct file *filp)
{
    if (!mutex_trylock(&spi_lock))
        return -EBUSY;  /* Устройство уже открыто – эксклюзивный доступ */
    return 0;
}

static int spi_bmp180_release(struct inode *inode, struct file *filp)
{
    mutex_unlock(&spi_lock);
    return 0;
}


/* Реализация IOCTL. В зависимости от команды отправляется соответствующий байт,
   а затем возвращаются данные с устройства. */
#define FRAME_LEN 33        // 1 байт команды + 32 байта данных

static long spi_bmp180_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    int ret;
    uint8_t cmd_buf[1] = {0};
    uint8_t rx_buf[FRAME_LEN] = {0};
    uint8_t dummy_tx[FRAME_LEN] = {0};
    struct spi_transfer t_cmd = {
        .tx_buf = cmd_buf,
        .len    = 1,
    };
    struct spi_transfer t_data = {
        .tx_buf = dummy_tx,
        .rx_buf = rx_buf,
        .len    = FRAME_LEN,
    };
    struct spi_message m;

    if (!spi_dev)
        return -ENODEV;

    dev_info(&spi_dev->dev, "Starting IOCTL cmd: 0x%x\n", cmd);

    /* Код команды */
    switch (cmd) {
    case SPI_CMD_READ_TEMP:   cmd_buf[0] = 0x01; break;
    case SPI_CMD_READ_PRESS:  cmd_buf[0] = 0x02; break;
    case SPI_CMD_READ_ALT:    cmd_buf[0] = 0x03; break;
    case SPI_CMD_CALIBRATE:   cmd_buf[0] = 0x04; break;
    case SPI_CMD_READ_CALIB:  cmd_buf[0] = 0x05; break;
    case SPI_CMD_READ_STUDENT:cmd_buf[0] = 0x06; break;
    default:
        return -EINVAL;
    }

    dev_info(&spi_dev->dev, "Sending command: 0x%02x\n", cmd_buf[0]);

    /* 1) Отправляем 1 байт команды */
    spi_message_init(&m);
    spi_message_add_tail(&t_cmd, &m);
    ret = spi_sync(spi_dev, &m);
    if (ret < 0) {
        dev_err(&spi_dev->dev, "SPI command transfer failed with error %d\n", ret);
        return ret;
    }

    /* Небольшая пауза, чтобы ESP успел подготовить ответ */
    msleep(50);

    /* 2) Читаем полный кадр с ответом */
    spi_message_init(&m);
    spi_message_add_tail(&t_data, &m);
    ret = spi_sync(spi_dev, &m);
    if (ret < 0) {
        dev_err(&spi_dev->dev, "SPI data transfer failed with error %d\n", ret);
        return ret;
    }

    dev_info(&spi_dev->dev, "Received: %02x %02x %02x %02x %02x\n",
             rx_buf[0], rx_buf[1], rx_buf[2], rx_buf[3], rx_buf[4]);

    /* Разбираем полученные данные */
    switch (cmd) {
    case SPI_CMD_READ_TEMP:
    case SPI_CMD_READ_PRESS:
    case SPI_CMD_READ_ALT: {
        float value;
        memcpy(&value, rx_buf, sizeof(float));  // без смещения
        if (copy_to_user((void __user *)arg, &value, sizeof(value))) {
            dev_err(&spi_dev->dev, "copy_to_user() failed\n");
            return -EFAULT;
        }
        break;
    }
    case SPI_CMD_READ_CALIB: {
        struct calib_data calib;
        memcpy(&calib, rx_buf, sizeof(calib));
        if (copy_to_user((void __user *)arg, &calib, sizeof(calib))) {
            dev_err(&spi_dev->dev, "copy_to_user() failed\n");
            return -EFAULT;
        }
        break;
    }
    case SPI_CMD_READ_STUDENT: {
        /* Найдём длину строки в rx_buf (максимум FRAME_LEN) */
        size_t len = strnlen((char *)rx_buf, FRAME_LEN);
        /* У пользователя выделено 64 байта */
        if (len >= 64)
            len = 63;
        /* Скопируем и нуль‑терминатор */
        if (copy_to_user((char __user *)arg, rx_buf, len + 1))
            return -EFAULT;
        break;
    }
    default:
        break;
    }

    dev_info(&spi_dev->dev, "IOCTL completed\n");
    return 0;
}


static const struct file_operations spi_bmp180_fops = {
    .owner = THIS_MODULE,
    .open = spi_bmp180_open,
    .release = spi_bmp180_release,
    .unlocked_ioctl = spi_bmp180_ioctl,
};

static struct miscdevice spi_bmp180_misc_device = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = "spi_bmp180",
    .fops = &spi_bmp180_fops,
    .mode = 0666,  /* дПолный доступ для любого юзера */
};

/* SPI драйвер: получение указателя на spi_device через probe */
static int spi_bmp180_probe(struct spi_device *spi)
{
    spi_dev = spi;
    spi->mode = 0;
    spi->bits_per_word = 8;

    if (spi_setup(spi)) {
        dev_err(&spi->dev, "Failed to setup SPI device\n");
        return -ENODEV;
    }

    dev_info(&spi->dev, "spi_bmp180 driver probed\n");
    return misc_register(&spi_bmp180_misc_device);
}

static void spi_bmp180_remove(struct spi_device *spi)
{
    misc_deregister(&spi_bmp180_misc_device);
    spi_dev = NULL;
    dev_info(&spi->dev, "spi_bmp180 driver removed\n");
}

static const struct spi_device_id spi_bmp180_ids[] = {
    { "spi_bmp180", 0 },
    { }
};
MODULE_DEVICE_TABLE(spi, spi_bmp180_ids);

static const struct of_device_id spi_bmp180_of_match[] = {
    { .compatible = "custom,spi_bmp180" },
    { }
};
MODULE_DEVICE_TABLE(of, spi_bmp180_of_match);

static struct spi_driver spi_bmp180_driver = {
    .driver = {
        .name = "spi_bmp180",
        .of_match_table = spi_bmp180_of_match,
    },
    .probe = spi_bmp180_probe,
    .remove = spi_bmp180_remove,
    .id_table = spi_bmp180_ids,
};



static int __init spi_bmp180_init(void)
{
    int ret;
    ret = spi_register_driver(&spi_bmp180_driver);
    if (ret < 0)
        return ret;
    return 0;
}

static void __exit spi_bmp180_exit(void)
{
    spi_unregister_driver(&spi_bmp180_driver);
}

module_init(spi_bmp180_init);
module_exit(spi_bmp180_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Shaposhnikov Daniil");
MODULE_DESCRIPTION("SPI driver for BMP180 sensor with IOCTL interface");
