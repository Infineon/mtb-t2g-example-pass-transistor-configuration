/**********************************************************************************************************************
 * \file main.c
 * \copyright Copyright (C) Infineon Technologies AG 2019
 *
 * Use of this file is subject to the terms of use agreed between (i) you or the company in which ordinary course of
 * business you are acting and (ii) Infineon Technologies AG or its licensees. If and as long as no such terms of use
 * are agreed, use of this file is subject to following:
 *
 * Boost Software License - Version 1.0 - August 17th, 2003
 *
 * Permission is hereby granted, free of charge, to any person or organization obtaining a copy of the software and
 * accompanying documentation covered by this license (the "Software") to use, reproduce, display, distribute, execute,
 * and transmit the Software, and to prepare derivative works of the Software, and to permit third-parties to whom the
 * Software is furnished to do so, all subject to the following:
 *
 * The copyright notices in the Software and this entire statement, including the above license grant, this restriction
 * and the following disclaimer, must be included in all copies of the Software, in whole or in part, and all
 * derivative works of the Software, unless such copies or derivative works are solely in the form of
 * machine-executable object code generated by a source language processor.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
 * WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *********************************************************************************************************************/
/*********************************************************************************************************************/
/*-----------------------------------------------------Includes------------------------------------------------------*/
/*********************************************************************************************************************/
#include "cybsp.h"
#include "cy_retarget_io.h"
#include "cyhal_gpio.h"
#include "cy_flash_srom.h"

/*********************************************************************************************************************/
/*-------------------------------------------------Data Structures---------------------------------------------------*/
/*********************************************************************************************************************/
/* CE state */
typedef enum
{
    ST_LDO,                 /* Internal regulator (LDO) */
    ST_PASSTR,              /* Pass Transistor (Active/DeepSleep) */
} StateType;

/*********************************************************************************************************************/
/*------------------------------------------------------Macros-------------------------------------------------------*/
/*********************************************************************************************************************/
/* The priority for interrupt */
#define GPIO_INTERRUPT_PRIORITY (7u)

/* Delay interval */
#define DELAY_MS (1000U)

/*********************************************************************************************************************/
/*-------------------------------------------------Global variables--------------------------------------------------*/
/*********************************************************************************************************************/
/* Argument1 of SROM API Configure Regulator */
const un_srom_api_args_t CONFIGURE_REGULATOR_ARG1 =
{
    /* Operating mode: External transistor */
    .ConfigureRegulator2.Mode                   = CY_SROM_REGULATOR_MODE_TRANSISTOR,
    /* Enable Polarity: Logic high is used for enable */
    .ConfigureRegulator2.EnablePolarity         = CY_SROM_REGULATOR_ENABLE_HIGH,
    /* Reset Polarity: Logic low is used for enable */
    .ConfigureRegulator2.StatusAbnormalPolarity = CY_SROM_REGULATOR_STATUS_ABNORMAL_LOW,
    /* PMIC behavior in DeepSleep mode: Allow HC regulator to operate in Normal mode */
    .ConfigureRegulator2.DeepSleep              = 0,
    /* UseLinReg: Internal Active Linear Regulator is disabled after PMIC is enabled. OCD is disabled */
    .ConfigureRegulator2.UseLinReg              = 0,
    /* Use Radj to generate a reset threshold for the PMIC: no Radj */
    .ConfigureRegulator2.UseRadj                = 0,
    /* REGHC_PMIC_VADJ_DIS: Device does not generate VADJ, and it must not be part of the PMIC feedback loop */
    .ConfigureRegulator2.VadjOption             = 1,
    /* VADJ trim value (VadjTrim) used in the regulator output trim: should be fixed to 0x10 */
    .ConfigureRegulator2.VoltageAdjust          = 0x10,
    /* RADJ reset threshold value */
    .ConfigureRegulator2.RAdjust                = 0,
    /* Opcode */
    .ConfigureRegulator2.Opcode                 = CY_SROM_OP_CONFIGURE_REGULATOR,
};

/* Argument2 of SROM API Configure Regulator */
const un_srom_api_args_2_t CONFIGURE_REGULATOR_ARG2 =
{
    /* Wait Count:
     * Wait count in steps of 4 us after PMIC status is OK.
     * This is used by the hardware sequencer to allow additional settling time before disabling the internal regulator */
    .ConfigureRegulator2.WaitCountIn1us         = 0x1FF,
};

/* Argument of SROM API SwitchOverRegulators: Switch over to REGHC (PMIC without REGHC) */
const un_srom_api_args_t SWITCH_REGULATOR_ARG_TARGET_EXT =
{
    /* Operating mode: External transistor */
    .SwitchRegulator.Mode                       = CY_SROM_REGULATOR_MODE_TRANSISTOR,
    /* Regulator: Switch over to REGHC (PMIC without REGHC) */
    .SwitchRegulator.SwitchTarget               = CY_SROM_REGULATOR_SWITCH_TARGET_EXT,
    /* Blocking: Blocks CM0+ until the transition completes */
    .SwitchRegulator.Blocking                   = 1,
    /* Opcode */
    .SwitchRegulator.Opcode                     = CY_SROM_OP_SWITCH_REGULATOR,
};

/* Argument of SROM API SwitchOverRegulators: Switch over to linear regulator */
const un_srom_api_args_t SWITCH_REGULATOR_ARG_TARGET_INT =
{
    /* Operating mode: External transistor */
    .SwitchRegulator.Mode                       = CY_SROM_REGULATOR_MODE_TRANSISTOR,
    /* Regulator: Switch over to linear regulator */
    .SwitchRegulator.SwitchTarget               = CY_SROM_REGULATOR_SWITCH_TARGET_INT,
    /* Blocking: Blocks CM0+ until the transition completes */
    .SwitchRegulator.Blocking                   = 1,
    /* Opcode */
    .SwitchRegulator.Opcode                     = CY_SROM_OP_SWITCH_REGULATOR,
};

/** The status of SW1 interrupt **/
volatile bool g_isInterrupt_SW1 = false;

/** The status of SW1 interrupt **/
volatile bool g_isInterrupt_SW2 = false;

/**********************************************************************************************************************
 * Function Name: gpio_interrupt_handler_SW1
 * Summary:
 *  GPIO interrupt handler for SW1.
 * Parameters:
 *  void *handler_arg (unused)
 *  cyhal_gpio_event_t (unused)
 * Return:
 *  none
 **********************************************************************************************************************
 */
static void gpio_interrupt_handler_SW1(void *handler_arg, cyhal_gpio_event_t event)
{
    g_isInterrupt_SW1 = true;
}

/**********************************************************************************************************************
 * Function Name: gpio_interrupt_handler_SW2
 * Summary:
 *  GPIO interrupt handler for SW2.
 * Parameters:
 *  void *handler_arg (unused)
 *  cyhal_gpio_event_t (unused)
 * Return:
 *  none
 **********************************************************************************************************************
 */
static void gpio_interrupt_handler_SW2(void *handler_arg, cyhal_gpio_event_t event)
{
    g_isInterrupt_SW2 = true;
}

/**********************************************************************************************************************
 * Function Name: configureRegulator
 * Summary:
 *  This API is configures high current regulator (REGHC) for the devices.
 *  It should be called to configure the desired regulator only once prior to
 *  switching to the regulator using switchOverRegulators() syscall.
 * Parameters:
 *  arg1: this pointer should contain command parameter of ConfigureRegulator (IPC_DATA0)
 *  arg2: this pointer should contain command parameter of ConfigureRegulator (IPC_DATA1)
 * Return:
 *  cy_en_srom_driver_status_t
 **********************************************************************************************************************
 */
static cy_en_srom_driver_status_t configureRegulator(const un_srom_api_args_t *arg1, const un_srom_api_args_2_t *arg2)
{
    return (Cy_Srom_CallApi_2(arg1, arg2, NULL));
}

/**********************************************************************************************************************
 * Function Name: switchOverRegulators
 * Summary:
 *  This API is used to switch between high current regulator(REGHC or PMIC without REGHC)
 *  required for running CM7 and linear regulator(LDO).
 *  This should be called to switch from LDO to REGHC before enabling CM7.
 *  The ConfigureRegulator() should be called prior using this function.
 * Parameters:
 *  arg: this pointer should contain command parameter of SwitchOverRegulators (IPC_DATA0)
 * Return:
 *  cy_en_srom_driver_status_t
 **********************************************************************************************************************
 */
static cy_en_srom_driver_status_t switchOverRegulators(const un_srom_api_args_t *arg)
{
    return (Cy_Srom_CallApi(arg, NULL));
}

/**********************************************************************************************************************
 * Function Name: main
 * Summary:
 *  This is the main function.
 *  This code example shows how to switch the power supply source from an internal regulator(LDO) to an external pass transistor.
 *  It also shows the transition to and return from DeepSleep mode and the switch from the external pass transistor to the internal
 *  regulator during external power supply.
 *
 *  After executing the example code, the MCU is operating with the internal regulator by default,
 *  and can be switched between the internal regulator and the external pass transistor by pressing USER_BTN1.
 *  USER_LED1 blinks when supplied by the internal regulator and lights up when supplied by the external pass transistor.
 *
 *  USER_LED1 blink - LDO is working.
 *  USER_LED1 light up - PassTr is working.
 *
 *  In addition, while the external pass transistor is being supplied,
 *  pressing USER_BTN2 allows a transition to DeepSleep mode and a return to ACTIVE mode.
 *
 *  In DeepSleep mode (only for passTr supply),
 *  USER_LED1 is off, while in ACTIVE mode (external pass transistor supply) it is on.
 *  Note that power switching is performed by CM0+ using the SROM API.
 *  Note that run the program without debug.
 * Parameters:
 *  none
 * Return:
 *  int
 **********************************************************************************************************************
 */
int main(void)
{
    StateType currentState = ST_LDO;
    StateType oldState = ST_PASSTR;
    cyhal_gpio_callback_data_t gpio_btn_callback_data_sw1;
    cyhal_gpio_callback_data_t gpio_btn_callback_data_sw2;
    cy_en_srom_driver_status_t api_result;

    /* Initialize the device and board peripherals */
    CY_ASSERT(cybsp_init() == CY_RSLT_SUCCESS);

    /* Initialize retarget-io to use the debug UART port */
    CY_ASSERT(cy_retarget_io_init(CYBSP_DEBUG_UART_TX, CYBSP_DEBUG_UART_RX,
                                  CY_RETARGET_IO_BAUDRATE) == CY_RSLT_SUCCESS);

    /* Initialize the SW1 */
    CY_ASSERT(cyhal_gpio_init(CYBSP_USER_BTN1, CYHAL_GPIO_DIR_INPUT,CYHAL_GPIO_DRIVE_NONE, CYBSP_BTN_OFF) == CY_RSLT_SUCCESS);

    /* Configure GPIO interrupt for SW1 */
    gpio_btn_callback_data_sw1.callback = gpio_interrupt_handler_SW1;
    cyhal_gpio_register_callback(CYBSP_USER_BTN1,  &gpio_btn_callback_data_sw1);
    cyhal_gpio_enable_event(CYBSP_USER_BTN1, CYHAL_GPIO_IRQ_FALL, GPIO_INTERRUPT_PRIORITY, true);

    /* Initialize the SW2 */
    CY_ASSERT(cyhal_gpio_init(CYBSP_USER_BTN2, CYHAL_GPIO_DIR_INPUT, CYHAL_GPIO_DRIVE_NONE, CYBSP_BTN_OFF) == CY_RSLT_SUCCESS);

    /* Configure GPIO interrupt for SW2 */
    gpio_btn_callback_data_sw2.callback = gpio_interrupt_handler_SW2;
    cyhal_gpio_register_callback(CYBSP_USER_BTN2, &gpio_btn_callback_data_sw2);
    cyhal_gpio_enable_event(CYBSP_USER_BTN2, CYHAL_GPIO_IRQ_FALL, GPIO_INTERRUPT_PRIORITY, true);

    /* LED Initialization */
    CY_ASSERT(cyhal_gpio_init(CYBSP_USER_LED1, CYHAL_GPIO_DIR_OUTPUT,
                              CYHAL_GPIO_DRIVE_STRONG, CYBSP_LED_STATE_OFF) == CY_RSLT_SUCCESS);

    /* enable interrupts */
    __enable_irq();

    /* Enable CM7_0/1. CY_CORTEX_M7_APPL_ADDR is calculated in linker script, check it in case of problems. */
    Cy_SysEnableCM7(CORE_CM7_0, CY_CORTEX_M7_0_APPL_ADDR);
    Cy_SysEnableCM7(CORE_CM7_1, CY_CORTEX_M7_1_APPL_ADDR);

    /* \x1b[2J\x1b[;H - ANSI ESC sequence for clear screen */
    printf("\x1b[2J\x1b[;H");

    printf("***********************************************************\r\n"
           "REGHC Pass Transistor Configuration\r\n"
           "***********************************************************\r\n");

    for (;;)
    {
        if (currentState == ST_LDO)
        {
            if (oldState != ST_LDO)
            {
                printf("\r\n## Internal Regulator (LDO) mode ##\r\n"
                       "operation:\r\n"
                       "- push USER_BTN1 to switch to Pass Transistor mode\r\n");
                oldState = ST_LDO;
            }
            if (g_isInterrupt_SW1 == true)
            {
                /* Power Handover from internal(LDO) to Pass Transistor */
                api_result = configureRegulator(&CONFIGURE_REGULATOR_ARG1, &CONFIGURE_REGULATOR_ARG2);
                if (api_result != CY_SROM_DR_SUCCEEDED)
                {
                    printf("configureRegulator() returns 0x%08lx\r\n", (uint32_t)api_result);
                    CY_ASSERT(0);
                }

                Cy_SysLib_DelayUs(1000U);

                api_result = switchOverRegulators(&SWITCH_REGULATOR_ARG_TARGET_EXT);
                if (api_result != CY_SROM_DR_SUCCEEDED)
                {
                    printf("switchOverRegulators() returns 0x%08lx\r\n", (uint32_t)api_result);
                    CY_ASSERT(0);
                }

                /* lights up when supplied by the external pass transistor */
                cyhal_gpio_write(CYBSP_USER_LED1, CYBSP_LED_STATE_ON);

                currentState = ST_PASSTR;
            }
            else
            {
                /* USER_LED1 blinks with a 1s cycle when supplied by the internal regulator, wait for SW interrupt */
                cyhal_gpio_toggle(CYBSP_USER_LED1);
            }
        }
        else
        {
            if (oldState != ST_PASSTR)
            {
                printf("\r\n## Pass Transistor mode ##\r\n"
                       "operation:\r\n"
                       "- push USER_BTN1 to switch to Internal Regulator (LDO) mode\r\n"
                       "- push USER_BTN2 to transit to/from DeepSleep\r\n");
                oldState = ST_PASSTR;
            }
            if (g_isInterrupt_SW1 == true)
            {
                /* PassTr to internal regulator */
                api_result = switchOverRegulators(&SWITCH_REGULATOR_ARG_TARGET_INT);
                if (api_result != CY_SROM_DR_SUCCEEDED)
                {
                    printf("switchOverRegulators() returns 0x%08lx\r\n", (uint32_t)api_result);
                    CY_ASSERT(0);
                }

                /* flag clear */
                currentState = ST_LDO;
            }
            else if (g_isInterrupt_SW2 == true)
            {
                printf("going to DeepSleep...\r\n");

                /* LED off */
                cyhal_gpio_write(CYBSP_USER_LED1, CYBSP_LED_STATE_OFF);

                /* Enter deepsleep mode*/
                cy_en_syspm_status_t ret;
                do
                {
                    /* Terminate retarget-io to ensure entry into deepsleep */
                    cy_retarget_io_deinit();

                    ret = Cy_SysPm_CpuEnterDeepSleep(CY_SYSPM_WAIT_FOR_INTERRUPT);

                    if (ret != CY_SYSPM_SUCCESS)
                    {
                        /* Re-initialize retarget-io to use the debug UART port */
                        CY_ASSERT(cy_retarget_io_init(CYBSP_DEBUG_UART_TX, CYBSP_DEBUG_UART_RX,
                                                      CY_RETARGET_IO_BAUDRATE) == CY_RSLT_SUCCESS);

                        printf("Cy_SysPm_CpuEnterDeepSleep() returns 0x%08lx\r\n", (uint32_t)ret);
                        Cy_SysLib_Delay(100);
                    }
                } while(ret != CY_SYSPM_SUCCESS);

                CY_ASSERT(cy_retarget_io_init(CYBSP_DEBUG_UART_TX, CYBSP_DEBUG_UART_RX,
                                                  CY_RETARGET_IO_BAUDRATE) == CY_RSLT_SUCCESS);

                /* Woke up from deepsleep mode */
                printf("Woke up from deepleep mode!\r\n");

                /* wake up to active mode and LED ON */
                cyhal_gpio_write(CYBSP_USER_LED1, CYBSP_LED_STATE_ON);
            }
        }
        /* flag clear */
        g_isInterrupt_SW1 = false;
        g_isInterrupt_SW2 = false;

        Cy_SysLib_Delay(DELAY_MS);
    }
}

/* [] END OF FILE */
