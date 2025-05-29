#include "backlight_controller.h"

BacklightController::BacklightController(gpio_num_t lcdBlPin)
    : pinLcdBl_(lcdBlPin){
    gpio_config_t io_conf;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1ULL << pinLcdBl_);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);
    gpio_set_level(pinLcdBl_, 0);
    vTaskDelay(50 / portTICK_PERIOD_MS);
    xQueue = xQueueCreate(4, sizeof(uint8_t));
}

BacklightController::~BacklightController(){
    if (xQueue != NULL) {
        vQueueDelete(xQueue);
    }
}


void BacklightController::SetBrightnessImpl(uint8_t brightness) {
    if (xQueue != NULL) {
        xQueueSend(xQueue, &brightness, 0);
    }
}

void BacklightController::loop(){
    if (xQueue != NULL){
        uint8_t brightness;
        if (xQueueReceive(xQueue, &brightness, portMAX_DELAY) == pdTRUE){         
            setBrightnessLedIcMode(brightness);
        }
    }
}

void BacklightController::setBrightnessLedIcMode(uint8_t brightness){
    if (brightness>100)
        brightness=100;
    brightness = 100 - brightness;
    brightness /= 6.6; // 映射到 0~15

    if (brightness == 15){
        gpio_set_level(pinLcdBl_, 0);
        oldBrightness_ = brightness;
        return;
    }

    if (oldBrightness_ == 15){
        // 唤醒背光
        gpio_set_level(pinLcdBl_, 0);
        esp_rom_delay_us(5);
        gpio_set_level(pinLcdBl_, 1);
        esp_rom_delay_us(30);
    }

    if (brightness > oldBrightness_){
        for (size_t i = 0; i < (brightness - oldBrightness_); ++i){
            gpio_set_level(pinLcdBl_, 0);
            esp_rom_delay_us(2);
            gpio_set_level(pinLcdBl_, 1);
            esp_rom_delay_us(2);
        }
    }
    else if (brightness < oldBrightness_){
        for (size_t i = 0; i < (16 - (oldBrightness_ - brightness)); ++i){
            gpio_set_level(pinLcdBl_, 0);
            esp_rom_delay_us(2);
            gpio_set_level(pinLcdBl_, 1);
            esp_rom_delay_us(2);
        }
    }
    oldBrightness_ = brightness;
}