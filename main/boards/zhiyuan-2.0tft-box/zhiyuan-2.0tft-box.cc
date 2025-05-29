#include "wifi_board.h"
#include "audio_codec.h"
#include "audio_codecs/es8311_audio_codec.h"
#include "display/lcd_display.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "led/single_led.h"
#include "iot/thing_manager.h"
#include "i2c_device.h"
#include "power_manager.h"
#include "no_audio_codec.h"
#include "settings.h"
#include "backlight_controller.h"

#include <wifi_station.h>
#include <esp_log.h>
#include <driver/i2c_master.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/timers.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_panel_ops.h>

#define TAG "zhiyuan_2_0tft_box"

LV_FONT_DECLARE(font_puhui_20_4);
LV_FONT_DECLARE(font_awesome_20_4);

class zhiyuan_2_0tft_box : public WifiBoard
{
private:
    i2c_master_bus_handle_t i2c_bus_;
    Button boot_button_;
    LcdDisplay* display_ = nullptr;
    PowerManager* power_manager_ = nullptr;
    BacklightController* backlight_controller_ = nullptr;
    void InitializePowerManager() {
        power_manager_ = new PowerManager(BAT_CHECK, CHARGE_CHECK);
    }
    void InitializeI2c(){
        // Initialize I2C peripheral
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = I2C_NUM_0,
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus_));
    }

    void InitializeATK_ST7789_80_Display() {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;
        /* 配置RD引脚 */
        gpio_config_t gpio_init_struct;
        gpio_init_struct.intr_type = GPIO_INTR_DISABLE;
        gpio_init_struct.mode = GPIO_MODE_INPUT_OUTPUT;
        gpio_init_struct.pin_bit_mask = 1ull << LCD_NUM_RD;
        gpio_init_struct.pull_down_en = GPIO_PULLDOWN_DISABLE;
        gpio_init_struct.pull_up_en = GPIO_PULLUP_ENABLE;
        gpio_config(&gpio_init_struct);
        gpio_set_level(LCD_NUM_RD, 1);

        esp_lcd_i80_bus_handle_t i80_bus = NULL;
        esp_lcd_i80_bus_config_t bus_config = {
            .dc_gpio_num = LCD_NUM_DC,
            .wr_gpio_num = LCD_NUM_WR,
            .clk_src = LCD_CLK_SRC_DEFAULT,
            .data_gpio_nums = {
                GPIO_LCD_D0,
                GPIO_LCD_D1,
                GPIO_LCD_D2,
                GPIO_LCD_D3,
                GPIO_LCD_D4,
                GPIO_LCD_D5,
                GPIO_LCD_D6,
                GPIO_LCD_D7,
            },
            .bus_width = 8,
            .max_transfer_bytes = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t),
            .psram_trans_align = 64,
            .sram_trans_align = 4,
        };
        ESP_ERROR_CHECK(esp_lcd_new_i80_bus(&bus_config, &i80_bus));

        esp_lcd_panel_io_i80_config_t io_config = {
            .cs_gpio_num = LCD_NUM_CS,
            .pclk_hz = (10 * 1000 * 1000),
            .trans_queue_depth = 10,
            .on_color_trans_done = nullptr,
            .user_ctx = nullptr,
            .lcd_cmd_bits = 8,
            .lcd_param_bits = 8,
            .dc_levels = {
                .dc_idle_level = 0,
                .dc_cmd_level = 0,
                .dc_dummy_level = 0,
                .dc_data_level = 1,
            },
            .flags = {
                .swap_color_bytes = 0,
            },
        };
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_i80(i80_bus, &io_config, &panel_io));

        esp_lcd_panel_dev_config_t panel_config = {
            .reset_gpio_num = LCD_NUM_RST,
            .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
            .bits_per_pixel = 16,
        };
        ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io, &panel_config, &panel));

        esp_lcd_panel_reset(panel);
        esp_lcd_panel_init(panel);
        esp_lcd_panel_invert_color(panel, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        esp_lcd_panel_set_gap(panel, 0, 0);
        uint8_t data0[] = { 0x00 };
        uint8_t data1[] = { 0x65 };
        esp_lcd_panel_io_tx_param(panel_io, 0x36, data0, 1);
        esp_lcd_panel_io_tx_param(panel_io, 0x3A, data1, 1);
        esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY);
        esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));

        display_ = new SpiLcdDisplay(panel_io, panel,
            DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY,
            {
                .text_font = &font_puhui_20_4,
                .icon_font = &font_awesome_20_4,
                #if CONFIG_USE_WECHAT_MESSAGE_STYLE
                    .emoji_font = font_emoji_32_init(),
                #else
                    .emoji_font = DISPLAY_HEIGHT >= 240 ? font_emoji_64_init() : font_emoji_32_init(),
                #endif
            });
    }

    void InitializeButtons(){
        boot_button_.OnClick([this](){
                auto& app = Application::GetInstance();
                if (app.GetDeviceState() == kDeviceStateStarting && !WifiStation::GetInstance().IsConnected()) {
                    ResetWifiConfiguration();
                }
                app.ToggleChatState();
            });
    }

    // 物联网初始化，添加对 AI 可见设备
    void InitializeIot(){
        auto& thing_manager = iot::ThingManager::GetInstance();
        thing_manager.AddThing(iot::CreateThing("Speaker"));
        thing_manager.AddThing(iot::CreateThing("Battery"));
        thing_manager.AddThing(iot::CreateThing("Screen"));
    }

    // 初始化背光
    void InitializeBacklight(){
        backlight_controller_ = new BacklightController(DISPLAY_BACKLIGHT_PIN);
    }
    static void DeviceManagerTask(void* arg) {
        auto* self = static_cast<zhiyuan_2_0tft_box*>(arg);
        gpio_config_t io_conf;
        io_conf.intr_type = GPIO_INTR_DISABLE;
        io_conf.mode = GPIO_MODE_INPUT;
        io_conf.pin_bit_mask = (1ULL << AUDIO_HP_PIN);
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
        gpio_config(&io_conf);
        io_conf.intr_type = GPIO_INTR_DISABLE;               // 禁用中断
        io_conf.mode = GPIO_MODE_OUTPUT;                     // 设置为输出模式
        io_conf.pin_bit_mask = (1ULL << AUDIO_CODEC_PA_PIN); // 设置引脚掩码
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;        // 禁用下拉电阻
        io_conf.pull_up_en = GPIO_PULLUP_DISABLE;            // 禁用上拉电阻
        gpio_config(&io_conf);                               // 应用配置
        for (;;)
        {
            if (gpio_get_level(AUDIO_HP_PIN)){
                gpio_set_level(AUDIO_CODEC_PA_PIN, 1);                // 断开耳机
            }
            else{
                gpio_set_level(AUDIO_CODEC_PA_PIN, 0);                // 插入耳机
            }
            vTaskDelay(4 / portTICK_PERIOD_MS);
            if (self->backlight_controller_ != nullptr) {
                self->backlight_controller_->loop();
            }
        }
    }

    void InitializeDeviceManager() {
        xTaskCreate(DeviceManagerTask, "DeviceManagerTask", 2048, this, 5, NULL);
    }

public:
    zhiyuan_2_0tft_box() : boot_button_(BOOT_BUTTON_GPIO){
        InitializePowerManager();
        InitializeI2c();
        InitializeDeviceManager();
        InitializeBacklight();
        InitializeATK_ST7789_80_Display();
        InitializeButtons();
        InitializeIot();
    }

    virtual AudioCodec* GetAudioCodec() override{
        static Es8311AudioCodec audio_codec(i2c_bus_, I2C_NUM_0, AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_MCLK, AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN,
            AUDIO_CODEC_PA_PIN, AUDIO_CODEC_ES8311_ADDR);
        return &audio_codec;
    }

    virtual Display* GetDisplay() override{
        return display_;
    }

    virtual Backlight* GetBacklight() override {
        return backlight_controller_;
    }

    virtual bool GetBatteryLevel(int& level, bool& charging, bool& discharging) override{
        charging = power_manager_->IsCharging();
        discharging = power_manager_->IsDischarging();
        level = power_manager_->GetBatteryLevel();
        return true;
    }
};

DECLARE_BOARD(zhiyuan_2_0tft_box);
