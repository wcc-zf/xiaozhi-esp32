#pragma once
#include <driver/gpio.h>
#include <esp_adc/adc_cali_scheme.h>
#include <esp_adc/adc_oneshot.h>
#include <esp_timer.h>

#include <functional>
#include <vector>

class PowerManager {
private:
    esp_timer_handle_t timer_handle_;
    gpio_num_t charging_pin_ = GPIO_NUM_NC;
    std::vector<uint16_t> adc_values_;
    uint32_t battery_level_ = 0;
    const int kBatteryAdcInterval = 60;
    const int kBatteryAdcDataCount = 10;
    bool is_charging_ = false;
    adc_oneshot_unit_handle_t adc_handle_;
    adc_channel_t battery_level_channel;
    adc_cali_handle_t adc_cali_handle;

    void CheckBatteryStatus() {
        if (charging_pin_ != GPIO_NUM_NC) {
            is_charging_ = gpio_get_level(charging_pin_) == 0;
        }
        HandleBatteryData();
    }

    void HandleBatteryData() {
        int adc_value = ReadBatteryData();
        // 将 ADC 值添加到队列中
        adc_values_.push_back(adc_value);
        if (adc_values_.size() > kBatteryAdcDataCount) {
            adc_values_.erase(adc_values_.begin());
        }
        uint32_t average_adc = 0;
        for (auto value : adc_values_) {
            average_adc += value;
        }
        average_adc /= adc_values_.size();

        // 定义电池电量区间
        const struct {
            uint16_t adc;
            uint8_t level;
        } levels[] = {
            {3100, 0},
            {3359, 20},
            {3540, 40},
            {3756, 60},
            {3942, 80},
            {4120, 100}
        };

        // 低于最低值时
        if (average_adc < levels[0].adc) {
            battery_level_ = 0;
        }
        // 高于最高值时
        else if (average_adc >= levels[5].adc) {
            battery_level_ = 100;
        }
        else {
            // 线性插值计算中间值
            for (int i = 0; i < 5; i++) {
                if (average_adc >= levels[i].adc && average_adc < levels[i + 1].adc) {
                    float ratio = static_cast<float>(average_adc - levels[i].adc) / (levels[i + 1].adc - levels[i].adc);
                    battery_level_ = levels[i].level + ratio * (levels[i + 1].level - levels[i].level);
                    break;
                }
            }
        }
        // ESP_LOGI("PowerManager", "ADC value: %d average: %ld level: %ld",
        // adc_value,average_adc, battery_level_);
    }

    int ReadBatteryData() {
        int adc_value;
        ESP_ERROR_CHECK(adc_oneshot_read(adc_handle_, battery_level_channel, &adc_value));
        // 转换为电压值（单位为毫伏）
        int voltage_mv = 0;
        ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc_cali_handle, adc_value, &voltage_mv));
        // 放大两倍，外部电阻分压
        voltage_mv *= 2;
        if (!is_charging_) {
            return voltage_mv;
        }
        static const uint16_t table[][2] = {
            {3352, 3442}, {3362, 3428}, {3372, 3482}, {3390, 3504}, {3400, 3492},
            {3404, 3514}, {3416, 3518}, {3428, 3532}, {3432, 3522}, {3436, 3528},
            {3442, 3540}, {3448, 3544}, {3450, 3564}, {3454, 3564}, {3458, 3562},
            {3460, 3550}, {3464, 3556}, {3466, 3564}, {3468, 3562}, {3476, 3562},
            {3478, 3576}, {3484, 3578}, {3486, 3570}, {3490, 3588}, {3494, 3588},
            {3500, 3590}, {3504, 3590}, {3506, 3610}, {3508, 3612}, {3510, 3606},
            {3512, 3606}, {3514, 3584}, {3518, 3588}, {3522, 3610}, {3526, 3600},
            {3530, 3614}, {3532, 3626}, {3536, 3632}, {3538, 3592}, {3540, 3616},
            {3544, 3618}, {3548, 3634}, {3550, 3644}, {3554, 3632}, {3556, 3632},
            {3562, 3650}, {3564, 3650}, {3570, 3648}, {3572, 3660}, {3574, 3668},
            {3576, 3650}, {3578, 3652}, {3580, 3662}, {3582, 3634}, {3588, 3658},
            {3590, 3676}, {3592, 3680}, {3596, 3676}, {3600, 3690}, {3602, 3710},
            {3606, 3670}, {3610, 3676}, {3612, 3668}, {3614, 3674}, {3618, 3680},
            {3622, 3644}, {3626, 3662}, {3630, 3688}, {3632, 3704}, {3634, 3706},
            {3638, 3692}, {3640, 3720}, {3642, 3726}, {3644, 3716}, {3646, 3736},
            {3650, 3736}, {3652, 3740}, {3656, 3712}, {3658, 3692}, {3660, 3740},
            {3662, 3756}, {3666, 3754}, {3668, 3748}, {3670, 3756}, {3674, 3766},
            {3676, 3756}, {3684, 3772}, {3686, 3764}, {3688, 3784}, {3690, 3762},
            {3692, 3782}, {3694, 3782}, {3696, 3784}, {3700, 3792}, {3704, 3788},
            {3710, 3804}, {3712, 3790}, {3716, 3808}, {3720, 3782}, {3726, 3812},
            {3728, 3766}, {3730, 3808}, {3738, 3816}, {3740, 3818}, {3746, 3836},
            {3748, 3826}, {3754, 3840}, {3756, 3836}, {3762, 3816}, {3764, 3846},
            {3766, 3854}, {3768, 3850}, {3772, 3858}, {3782, 3858}, {3784, 3862},
            {3788, 3858}, {3790, 3872}, {3792, 3866}, {3798, 3886}, {3808, 3878},
            {3810, 3890}, {3812, 3836}, {3814, 3888}, {3816, 3886}, {3818, 3896},
            {3820, 3898}, {3824, 3886}, {3826, 3908}, {3828, 3906}, {3836, 3910},
            {3840, 3914}, {3842, 3922}, {3846, 3918}, {3848, 3922}, {3850, 3922},
            {3854, 3920}, {3858, 3868}, {3862, 3934}, {3866, 3946}, {3868, 3954},
            {3870, 3946}, {3872, 3946}, {3874, 3946}, {3878, 3918}, {3882, 3956},
            {3886, 3950}, {3888, 3960}, {3890, 3966}, {3892, 3966}, {3896, 3962},
            {3898, 3974}, {3906, 3974}, {3908, 3974}, {3910, 3988}, {3914, 3988},
            {3918, 3980}, {3920, 3994}, {3922, 3980}, {3926, 3998}, {3930, 3992},
            {3932, 4016}, {3934, 4008}, {3936, 4008}, {3938, 3994}, {3940, 4008},
            {3942, 4010}, {3946, 4014}, {3948, 4020}, {3954, 4028}, {3956, 4024},
            {3958, 4028}, {3960, 4028}, {3962, 4028}, {3964, 4030}, {3966, 4036},
            {3970, 4042}, {3974, 4038}, {3978, 4048}, {3980, 4042}, {3986, 4048},
            {3988, 4060}, {3992, 4060}, {3994, 4070}, {3998, 4068}, {4008, 4070},
            {4010, 4068}, {4014, 4070}, {4016, 4074}, {4018, 4074}, {4020, 4082},
            {4024, 4090}, {4028, 4094}, {4032, 4096}, {4034, 4094}, {4036, 4090},
            {4038, 4114}, {4040, 4094}, {4042, 4114}, {4046, 4112}, {4048, 4114},
            {4052, 4116}, {4056, 4124}, {4060, 4132}, {4064, 4126}, {4068, 4136},
            {4070, 4136}, {4074, 4146}, {4078, 4048}, {4082, 4152}, {4086, 4150},
            {4090, 4156}, {4094, 4164}, {4108, 4164}, {4110, 4172}, {4112, 4166},
            {4114, 4170}, {4116, 4172}, {4120, 4186}, {4124, 4144}, {4126, 4182},
            {4130, 4192}, {4132, 4192}, {4136, 4192}, {4138, 4192}, {4140, 4200},
            {4142, 4196}, {4144, 4214}, {4146, 4208}, {4148, 4212}, {4150, 4210},
            {4152, 4216}, {4156, 4216}, {4158, 4196}, {4160, 4216}, {4164, 4214},
            {4166, 4222}, {4168, 4220}, {4170, 4216}, {4172, 4222}, {4174, 4230},
            {4178, 4232}, {4182, 4226}, {4186, 4232}, {4190, 4236}, {4192, 4230},
            {4196, 4233} };
        static const uint16_t table_len = sizeof(table) / sizeof(table[0]);
        int left = 0;
        int right = table_len - 1;
        int closestRightIndex = -1;

        while (left <= right) {
            int mid = left + (right - left) / 2;

            if (table[mid][1] == voltage_mv) {
                // 如果找到目标值，返回其索引
                return table[mid][0];
            }
            else if (table[mid][1] < voltage_mv) {
                // 如果中间值小于目标值，移动左边界
                closestRightIndex = mid;
                left = mid + 1;
            }
            else {
                // 如果中间值大于目标值，移动右边界
                closestRightIndex = mid;
                right = mid - 1;
            }
        }
        // 如果没有找到精确匹配，返回最接近的右值对应的左值
        if (closestRightIndex != -1) {
            return table[closestRightIndex][0];
        }
        return voltage_mv;
    }

public:
    PowerManager(gpio_num_t battery_level_pin,
        gpio_num_t charging_status_pin = GPIO_NUM_NC)
        : charging_pin_(charging_status_pin) {
        // 初始化充电引脚
        gpio_config_t io_conf = {};
        io_conf.intr_type = GPIO_INTR_DISABLE;
        io_conf.mode = GPIO_MODE_INPUT;
        io_conf.pin_bit_mask = (1ULL << charging_pin_);
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
        gpio_config(&io_conf);

        // 创建电池电量检查定时器
        esp_timer_create_args_t timer_args = {
            .callback =
                [](void* arg) {
                  PowerManager* self = static_cast<PowerManager*>(arg);
                  self->CheckBatteryStatus();
                },
            .arg = this,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "battery_check_timer",
            .skip_unhandled_events = true,
        };
        ESP_ERROR_CHECK(esp_timer_create(&timer_args, &timer_handle_));
        ESP_ERROR_CHECK(esp_timer_start_periodic(timer_handle_, 1000000));

        // 根据 GPIO 引脚号获取对应的 ADC 通道号和单元号
        adc_unit_t adc_unit;
        ESP_ERROR_CHECK(adc_oneshot_io_to_channel(battery_level_pin, &adc_unit,
            &battery_level_channel));

        // 初始化 ADC
        adc_oneshot_unit_init_cfg_t init_config = {
            .unit_id = adc_unit,
            .ulp_mode = ADC_ULP_MODE_DISABLE,
        };
        ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc_handle_));

        adc_oneshot_chan_cfg_t chan_config = {
            .atten = ADC_ATTEN_DB_12,
            .bitwidth = ADC_BITWIDTH_12,
        };
        ESP_ERROR_CHECK(adc_oneshot_config_channel(
            adc_handle_, battery_level_channel, &chan_config));

        adc_cali_curve_fitting_config_t cali_config = {
            .unit_id = adc_unit,
            .atten = ADC_ATTEN_DB_12,
            .bitwidth = ADC_BITWIDTH_12,
        };
        ESP_ERROR_CHECK(
            adc_cali_create_scheme_curve_fitting(&cali_config, &adc_cali_handle));
    }

    ~PowerManager() {
        if (timer_handle_) {
            esp_timer_stop(timer_handle_);
            esp_timer_delete(timer_handle_);
        }
        if (adc_handle_) {
            adc_oneshot_del_unit(adc_handle_);
        }
        if (adc_cali_handle) {
            adc_cali_delete_scheme_curve_fitting(adc_cali_handle);
        }
    }

    bool IsCharging() {
        // 如果电量已经满了，则不再显示充电中
        if (battery_level_ == 100) {
            return false;
        }
        return is_charging_;
    }

    bool IsDischarging() {
        // 没有区分充电和放电，所以直接返回相反状态
        return !is_charging_;
    }

    uint8_t GetBatteryLevel() {
        return battery_level_;
    }
};
