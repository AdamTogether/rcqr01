// Turning Technologies RCQR-01 demo app
// Jens Jensen 2017

// SDK 12.3.0 docs link: http://infocenter.nordicsemi.com/topic/com.nordic.infocenter.sdk5.v12.3.0/index.html

#include <stdbool.h>
#include <stdint.h>
#include "nrf.h"
#include "nrf_delay.h"
#include "nrf_gpio.h"
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "SEGGER_RTT.h"

// eeprom pins
#define EEP_CS  12
#define EEP_SO  16
#define EEP_SI  30
#define EEP_CK   0

#define LCD_BACKLIGHT 13

// LCD Boost converter/regulator control pins
#define MCP1256_PGOOD 14
#define MCP1256_SLEEP 15
#define MCP1256_ENABLE  27 // shutdown\

#include "radio.h"
#include "apptimers.h"
//#include "rtc.h"    // experimentation w/ rtc - using app_timers instead for now
#include "gfx.h"
#include "ST7586.h"
#include "adc_hal.h"
#include "keypad.h"
#include "sleep.h"

#include "nrf_ic_info.h"
nrf_ic_info_t *nrf_info;

uint32_t elapsed, prev_draw_time = 0;

uint8_t i, c, prev_keyscan, last_press_seconds = 0;

sleep_mode_t sleep_mode = MODE_ACTIVE;

systemticks_t gfxSystemTicks(void)
{
	return tick;
}

systemticks_t gfxMillisecondsToTicks(delaytime_t ms)
{
	return ms;
}

font_t font_small, font_med;


int main(void)
{

  // initially tristate all col/row pins
  nrf_gpio_range_cfg_input(0, 31, NRF_GPIO_PIN_NOPULL);

  // setup led/lcd regulator
  nrf_gpio_cfg_output(MCP1256_SLEEP);
  nrf_gpio_cfg_output(MCP1256_ENABLE);
  nrf_gpio_pin_write(MCP1256_SLEEP, 1);
  nrf_gpio_pin_write(MCP1256_ENABLE, 1);  // SHUTDOWN pin, active low
  //nrf_gpio_cfg_output(LCD_BACKLIGHT);
  //nrf_gpio_pin_write(LCD_BACKLIGHT, 1);

  NRF_LOG_INIT(NULL);
  apptimers_init();
  //rtc_init();

  gfxInit();
  font_small = gdispOpenFont("fixed_7x14");
  font_med = gdispOpenFont("fixed_10x20");

  //gdispControl(GDISP_CONTROL_ALL_PIXELS, 1);
  //gdispSetPowerMode(powerOn);
  
  char sbuf[128];
  
  sprintf(sbuf, "RFCH:%02d", get_channel());
  gdispDrawString(0,0, sbuf, font_small, White);

  nrf_ic_info_get(nrf_info);
  sprintf(sbuf, "did1: 0x%x, did0: 0x%x, fl: %d, rb: %d", NRF_FICR->DEVICEID[1], NRF_FICR->DEVICEID[0], nrf_info->flash_size,  NRF_FICR->NUMRAMBLOCK);
  gdispDrawString(0,20, sbuf, font_small, White);   
   
   /*
   // some test graphics...
   gdispFillCircle(80, 80, 10, White);
   gdispDrawBox(0, 00, 10, 10, White);
   gdispDrawBox(330, 00, 10, 10, White);
   gdispDrawBox(0, 140, 20, 20, White);
  */      

  gdispDrawString(0,40, "Hello lcd", font_med, White);
  gdispFlush();
                                                                                                                                                                     
  radio_init();
  adc_init();



  /// LOOP
  while (true)
  {
    // only run loop every 100 ms
    if (tick % 100 == 0)
    {
      NRF_LOG_DEBUG("tick: %d\n", tick);
      // playing with screen pixel inversion
      //gdispControl(GDISP_CONTROL_INVERSE, 1);
      //nrf_delay_ms(500);
      //gdispControl(GDISP_CONTROL_INVERSE, 0);
  
      if (sleep_mode == MODE_WAKEUP)
      {
        sleep_timer = seconds;    // reset sleep timer
        nrf_gpio_pin_write(MCP1256_ENABLE, 1);  // turn on lcd/led regulator
        //nrf_gpio_pin_write(LCD_BACKLIGHT, 1);
        low_power_pwm_duty_set(&display_pwm, DISPLAY_MEDIUM);
        sleep_mode = MODE_ACTIVE;
        continue;
      }

      if (sleep_mode == MODE_ACTIVE &&
            (seconds - sleep_timer) > SLEEP_BACKLIGHT_DELAY)
      {
        // transition from ACTIVE to backlight off
        //nrf_gpio_pin_write(LCD_BACKLIGHT, 0);  // turn off lcd/led regulator
        low_power_pwm_duty_set(&display_pwm, DISPLAY_OFF);
        sleep_mode = MODE_SLEEP_BACKLIGHT;
      }

      if (sleep_mode == MODE_SLEEP_BACKLIGHT &&
            (seconds - sleep_timer) > SLEEP_DISPLAY_DELAY)
      {
        // transition from backlight to display off & sleep
        nrf_gpio_pin_write(MCP1256_ENABLE, 0);  // turn off lcd/led regulator
        sleep_mode = MODE_SLEEP_DISPLAY;
        __WFE();
        __SEV();
        __WFE();
      }

      c = scan_keypad();
      if (c)
      {
        if (sleep_mode == MODE_ACTIVE)
        {
            sleep_timer = seconds;    // reset sleep timer
            elapsed = tick;
            sprintf(sbuf, "char: %c", c);
            gdispFillString(0,130, sbuf, font_med, White, Black);
            // send char over radio
            send_char(c);
            // testing some power modes:
            switch (c) {
               case '+':
                low_power_pwm_duty_set(&display_pwm, DISPLAY_BRIGHT);
                break;
               case 0x08:
                // backspace
                low_power_pwm_duty_set(&display_pwm, DISPLAY_DIM);
                break;
               case 's':
                // shutdown regulator to turn off lcd & led
                nrf_gpio_pin_toggle(MCP1256_ENABLE);
                break;
               case 0xd1:
                // left arrow, lower contrast
                gdispSetContrast(gdispGetContrast() - 2);
                break;
               case 0xd2:
                // right arrow, raise contrast
                gdispSetContrast(gdispGetContrast() + 2);
                break;
            }
  
            elapsed = tick - elapsed;

            sprintf(sbuf, "hello lcd land, tick: %8d", tick);
            gdispFillString(0, 40, sbuf, font_med, White, Black);

            sprintf(sbuf, "elapsed: %8d, secs: %4d", elapsed, seconds);
            gdispFillString(0, 60, sbuf, font_med, White, Black);

            sprintf(sbuf, "batt: %3d", get_vcc());
            gdispFillString(0, 80, sbuf, font_med, White, Black);

            sprintf(sbuf, "prev draw delay: %d", prev_draw_time);
            gdispFillString(0,110, sbuf, font_med, White, Black);

            prev_draw_time = tick;
            gdispFlush();
            prev_draw_time = tick - prev_draw_time;

        } 
        else if (sleep_mode < MODE_WAKEUP)
        {
            sleep_mode = MODE_WAKEUP;
        }
      }
   }
  // sleep - end of loop
  __WFE();
  __SEV();
  __WFE();
  }
}

/**
 *@}
 **/
