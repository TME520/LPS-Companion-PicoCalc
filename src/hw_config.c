// PicoCalc SD wiring: ClockworkPi Code/MP3Player/config.h.
#include "hw_config.h"
static spi_t buses[] = {{
    .hw_inst = spi0, .miso_gpio = 16, .mosi_gpio = 19,
    .sck_gpio = 18, .baud_rate = 12500000
}};
static sd_card_t cards[] = {{
    .pcName = "0:", .spi = &buses[0], .ss_gpio = 17,
    .use_card_detect = false
}};
size_t sd_get_num(void) { return 1; }
sd_card_t *sd_get_by_num(size_t n) { return n < 1 ? &cards[n] : NULL; }
size_t spi_get_num(void) { return 1; }
spi_t *spi_get_by_num(size_t n) { return n < 1 ? &buses[n] : NULL; }
