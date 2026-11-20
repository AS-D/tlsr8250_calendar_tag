/**
 * @file    batt.c
 * @brief   电池电压读取（TLSR8250 / 825x）
 *
 * ============================================================================
 *  2026-11-19 现场结论：内部 VBAT 通道在这块价签上读不到东西
 *  固件已经在跑（灯能闪、屏能收到初始化命令），但内部 VBAT 通道（MISC P=0xF）
 *  采样值仍然是 0 ——说明这颗片子的 VBAT 采样脚在这块 PCB 上并没有接到电池那根
 *  电源轨（可能是经二极管/负载开关过的），或者 8250 这颗料根本没引出该通道。
 *
 *  所以主用 SDK 自己给 825x 推荐的做法（battery_check.c 的注释）：
 *      「you must choose one gpio with adc function to output high level
 *        (voltage will equal to vbat), then use adc to measure high level voltage」
 *  即把任意一个 ADC 能测的 GPIO 驱动成高电平，它的电压就等于 VDD，直接量它。
 *  这样就绕开了内部 VBAT 通道，而且不需要板上有分压电阻。
 *
 *  借用 PB7（= EPD 的 SDA）：CS 空闲为高时 SDA 本来就是高电平空闲态，
 *  把脚驱动成高再采样，对面板没有任何副作用；测完保持高电平即可，SPI 照常工作。
 *
 *  内部 VBAT 通道仍然保留，只用来做诊断记录（写进 flash 日志，见 main_app.c）。
 * ============================================================================
 */
#include "batt.h"
#include "adc.h"

#define BATT_SENSE_PIN   GPIO_PB7      /* 借用 SDA，ADC_GPIO_tab 里 gpio_no = 8 */
#define BATT_MIN_MV      1700u         /* 芯片工作电压下限 */
#define BATT_MAX_MV      3800u         /* 芯片工作电压上限 3.6V，留余量 */

/* 上电采一次并断电：第一轮丢掉，通道需要稳定 */
static unsigned int adc_sample_once(void)
{
    unsigned int v;

    (void)adc_sample_and_get_result();
    v = adc_sample_and_get_result();
    adc_power_on_sar_adc(0);           /* 采完立刻断电，电池供电不能常开 */
    return v;
}

/* ------------------------------------------------------------------------- */
/* 方法 A（主用）：GPIO 驱动高电平 → 它的电压就是 VDD                        */
/* ------------------------------------------------------------------------- */
unsigned int batt_pin_mv(void)
{
    adc_init();
    adc_vbat_init(BATT_SENSE_PIN);      /* 通道/参考/分频/引脚/分辨率一次配好 */
    adc_power_on_sar_adc(1);            /* 必须最后上电 */

    /* 测完把脚还原成"GPIO 输出高"（= SPI 空闲态），保证后面刷屏不受影响 */
    gpio_set_func(BATT_SENSE_PIN, AS_GPIO);
    gpio_set_input_en(BATT_SENSE_PIN, 0);
    gpio_set_output_en(BATT_SENSE_PIN, 1);
    gpio_write(BATT_SENSE_PIN, 1);

    return adc_sample_once();
}

/* ------------------------------------------------------------------------- */
/* 方法 B（诊断用）：内部 VBAT 通道，返回未乘系数的原始 mV                    */
/* ------------------------------------------------------------------------- */
unsigned int batt_vbat_ch_raw(void)
{
    adc_init();
    adc_set_chn_enable_and_max_state_cnt(ADC_MISC_CHN, 2);
    adc_set_ref_voltage(ADC_MISC_CHN, ADC_VREF_1P2V);
    adc_set_vref_vbat_divider(ADC_VBAT_DIVIDER_OFF);
    adc_set_ain_pre_scaler(ADC_PRESCALER_1F8);
    adc_set_resolution(ADC_MISC_CHN, RES14);
    adc_set_tsample_cycle(ADC_MISC_CHN, SAMPLING_CYCLES_6);
    adc_set_ain_channel_differential_mode(ADC_MISC_CHN, VBAT, GND);
    adc_set_mode(ADC_NORMAL_MODE);
    adc_power_on_sar_adc(1);

    return adc_sample_once();
}

/* ------------------------------------------------------------------------- */
/* 对外：一次把两种方法的原始值都采出来，顺带给出"有效电压"                   */
/* 优先引脚法；引脚法无效再退回内部 VBAT 通道（自动判 1/3 分压）              */
/* pin_out / ch_out 可以为 NULL                                             */
/* ------------------------------------------------------------------------- */
unsigned int batt_measure(unsigned int *pin_out, unsigned int *ch_out)
{
    unsigned int pin, ch, mv;

    pin = batt_pin_mv();
    ch  = batt_vbat_ch_raw();

    if (pin_out) *pin_out = pin;
    if (ch_out)  *ch_out  = ch;

    mv = 0;
    if (pin >= BATT_MIN_MV && pin <= BATT_MAX_MV)                 mv = pin;
    else if (ch * 3u >= BATT_MIN_MV && ch * 3u <= BATT_MAX_MV)    mv = ch * 3u;
    else if (ch      >= BATT_MIN_MV && ch      <= BATT_MAX_MV)    mv = ch;

    return mv;                         /* 0 -> 界面显示 --.-V */
}

unsigned int batt_mv(void)
{
    return batt_measure(0, 0);
}
