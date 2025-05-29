#pragma once
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h> // 添加这行
#include <freertos/queue.h>    // 添加这行
#include <mutex>
#include "board.h"
class BacklightController : public Backlight
{
public:
    BacklightController(gpio_num_t lcdBlPin);
    ~BacklightController();

    void SetBrightnessImpl(uint8_t brightness) override;
    void loop();

private:
    uint8_t brightness_ = 80;
    uint8_t oldBrightness_ = 15;
    gpio_num_t pinLcdBl_;
    QueueHandle_t xQueue = NULL;
    void setBrightnessLedIcMode(uint8_t brightness);
};