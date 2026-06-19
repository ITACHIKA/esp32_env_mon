static const uint8_t SCD40_I2C_ADDR = 0x62;
static const uint8_t SHT40_I2C_ADDR = 0x44;
static const uint8_t SGP41_I2C_ADDR = 0x59;

static const uint16_t SCD40_STOP_PERIODIC = 0x3f86;
static const uint16_t SCD40_REINIT = 0x3646;
static const uint16_t SCD40_START_PERIODIC = 0x21b1;
static const uint16_t SCD40_READ_MEASUREMENT = 0xec05;

static const uint8_t SHT40_START_MEASUREMENT = 0xFD;

static const uint8_t SGP41_CONDITIONING_CMD[8] = {
    0x26, 0x12,
    0x80, 0x00, 0xA2,
    0x66, 0x66, 0x93
};