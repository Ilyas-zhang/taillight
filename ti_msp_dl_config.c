/*
 *  ============ ti_msp_dl_config.c =============
 *  MSPM0G3507 + WS2815 LED controller - device initialization
 *
 *  Hand-written (no SysConfig). Configures:
 *    - 80 MHz MCLK from SYSPLL (with FCC lock check workaround)
 *    - TIMA0 (BUSCLK = 80 MHz) in PWM edge-align-up mode, CC2 (PB4/STOP) +
 *      CC3 (PA28/RIGHT)
 *    - TIMG0 (ULPCLK = 40 MHz) in PWM edge-align-up mode, CC0 (PA12/LIFT)
 *    - TIMA0 ZERO event -> event-bus channel 1 -> DMA GENERIC_SUB0
 *    - TIMG0 ZERO event -> event-bus channel 2 -> DMA GENERIC_SUB1
 *    - 3 DMA byte channels writing timer CC registers each bit period
 */
#include "ti_msp_dl_config.h"

/*
 *  ======== SYSCFG_DL_init ========
 *  Perform all required MSP DL initialization.
 */
SYSCONFIG_WEAK void SYSCFG_DL_init(void)
{
    SYSCFG_DL_initPower();
    SYSCFG_DL_GPIO_init();
    SYSCFG_DL_SYSCTL_init();
    SYSCFG_DL_TIMA_init();
    SYSCFG_DL_TIMG_init();
    SYSCFG_DL_DMA_init();
    SYSCFG_DL_UART_init();
}

SYSCONFIG_WEAK void SYSCFG_DL_initPower(void)
{
    DL_GPIO_reset(GPIOA);
    DL_GPIO_reset(GPIOB);
    DL_TimerA_reset(WS2815_TIMA_INST);
    DL_TimerG_reset(WS2815_TIMG_INST);
    DL_UART_Main_reset(UART_COMM_INST);

    DL_GPIO_enablePower(GPIOA);
    DL_GPIO_enablePower(GPIOB);
    DL_TimerA_enablePower(WS2815_TIMA_INST);
    DL_TimerG_enablePower(WS2815_TIMG_INST);
    DL_UART_Main_enablePower(UART_COMM_INST);
    delay_cycles(POWER_STARTUP_DELAY);
}

SYSCONFIG_WEAK void SYSCFG_DL_GPIO_init(void)
{
    /* PA12 -> TIMG0_CCP0 (LIFT chain) */
    DL_GPIO_initPeripheralOutputFunctionFeatures(WS2815_LIFT_IOMUX,
        WS2815_LIFT_IOMUX_FUNC, DL_GPIO_INVERSION_DISABLE,
        DL_GPIO_RESISTOR_NONE, DL_GPIO_DRIVE_STRENGTH_HIGH,
        DL_GPIO_HIZ_DISABLE);
    DL_GPIO_enableOutput(WS2815_LIFT_PORT, WS2815_LIFT_PIN);

    /* PA28 -> TIMA0_CCP3 (RIGHT chain) */
    DL_GPIO_initPeripheralOutputFunctionFeatures(WS2815_RIGHT_IOMUX,
        WS2815_RIGHT_IOMUX_FUNC, DL_GPIO_INVERSION_DISABLE,
        DL_GPIO_RESISTOR_NONE, DL_GPIO_DRIVE_STRENGTH_HIGH,
        DL_GPIO_HIZ_DISABLE);
    DL_GPIO_enableOutput(WS2815_RIGHT_PORT, WS2815_RIGHT_PIN);

    /* PB4 -> TIMA0_CCP2 (STOP chain) */
    DL_GPIO_initPeripheralOutputFunctionFeatures(WS2815_STOP_IOMUX,
        WS2815_STOP_IOMUX_FUNC, DL_GPIO_INVERSION_DISABLE,
        DL_GPIO_RESISTOR_NONE, DL_GPIO_DRIVE_STRENGTH_HIGH,
        DL_GPIO_HIZ_DISABLE);
    DL_GPIO_enableOutput(WS2815_STOP_PORT, WS2815_STOP_PIN);

    /* PA10 -> UART0_TX (PINCM21) — 串口通信发送 / UART0 TX */
    DL_GPIO_initPeripheralOutputFunction(UART_COMM_TX_IOMUX,
        UART_COMM_TX_IOMUX_FUNC);
    /* PA11 -> UART0_RX (PINCM22) — 串口通信接收 / UART0 RX */
    DL_GPIO_initPeripheralInputFunction(UART_COMM_RX_IOMUX,
        UART_COMM_RX_IOMUX_FUNC);
}

/*
 * 80 MHz SYSPLL configuration:
 *   SYSOSC(32 MHz) / PDIV(2) * QDIV(9) = 144 MHz VCO
 *   PLLCLK2X = 144 MHz / (rDivClk2x * 2)... yields 80 MHz MCLK
 */
static const DL_SYSCTL_SYSPLLConfig gSYSPLLConfig = {
    .inputFreq  = DL_SYSCTL_SYSPLL_INPUT_FREQ_16_32_MHZ,
    .rDivClk2x  = 3,
    .rDivClk1   = 1,
    .rDivClk0   = 0,
    .enableCLK2x = DL_SYSCTL_SYSPLL_CLK2X_ENABLE,
    .enableCLK1 = DL_SYSCTL_SYSPLL_CLK1_ENABLE,
    .enableCLK0 = DL_SYSCTL_SYSPLL_CLK0_DISABLE,
    .sysPLLMCLK = DL_SYSCTL_SYSPLL_MCLK_CLK2X,
    .sysPLLRef  = DL_SYSCTL_SYSPLL_REF_SYSOSC,
    .qDiv       = 9,
    .pDiv       = DL_SYSCTL_SYSPLL_PDIV_2
};

/* [SYSPLL_ERR_01] workaround: verify PLL locked to the expected ratio. */
SYSCONFIG_WEAK bool SYSCFG_DL_SYSCTL_SYSPLL_init(void)
{
    bool fFCCRatioStatus = false;
    uint32_t fFCCSysoscCount;
    uint32_t fFCCPllCount;
    uint32_t fFCCRatio;
    uint32_t fccTimeOutCounter;

    DL_SYSCTL_setFCCPeriods(DL_SYSCTL_FCC_TRIG_CNT_01);

    /* Measuring PLL. */
    DL_SYSCTL_configFCC(DL_SYSCTL_FCC_TRIG_TYPE_RISE_RISE,
        DL_SYSCTL_FCC_TRIG_SOURCE_LFCLK, DL_SYSCTL_FCC_CLOCK_SOURCE_SYSPLLCLK1);
    /* Get SYSPLL frequency using FCC */
    fccTimeOutCounter = 0;
    DL_SYSCTL_startFCC();
    while (DL_SYSCTL_isFCCDone() == 0) {
        delay_cycles(977); /* 1x LFCLK cycle = 32MHz/32.768kHz = 977, 30.5us */
        fccTimeOutCounter++;
        if (fccTimeOutCounter > 65) {
            /* Timeout set to approximately 2ms (user-customizable) */
            break;
        }
    }

    /* get measA = SYSPLLCLK1 freq wrt LFOSC */
    fFCCPllCount = DL_SYSCTL_readFCC();

    /* Measuring SYSPLL Source */
    DL_SYSCTL_configFCC(DL_SYSCTL_FCC_TRIG_TYPE_RISE_RISE,
        DL_SYSCTL_FCC_TRIG_SOURCE_LFCLK, DL_SYSCTL_FCC_CLOCK_SOURCE_SYSOSC);
    /* Get SYSPLL frequency using FCC */
    fccTimeOutCounter = 0;
    DL_SYSCTL_startFCC();
    while (DL_SYSCTL_isFCCDone() == 0) {
        delay_cycles(977); /* 1x LFCLK cycle = 32MHz/32.768kHz = 977, 30.5us */
        fccTimeOutCounter++;
        if (fccTimeOutCounter > 65) {
            /* Timeout set to approximately 2ms (user-customizable) */
            break;
        }
    }

    /* get measB = SYSOSC freq wrt LFOSC */
    fFCCSysoscCount = DL_SYSCTL_readFCC();

    /* Get ratio of both measurements */
    fFCCRatio = (fFCCPllCount * FLOAT_TO_INT_SCALE) / fFCCSysoscCount;
    /* Check ratio is within bounds */
    if ((FCC_LOWER_BOUND < fFCCRatio) && (fFCCRatio < FCC_UPPER_BOUND)) {
        /* ratio is good for proceeding into application code. */
        fFCCRatioStatus = true;
    }

    return fFCCRatioStatus;
}

SYSCONFIG_WEAK void SYSCFG_DL_SYSCTL_init(void)
{
    /* Low Power Mode is configured to be SLEEP0 */
    DL_SYSCTL_setBORThreshold(DL_SYSCTL_BOR_THRESHOLD_LEVEL_0);
    DL_SYSCTL_setFlashWaitState(DL_SYSCTL_FLASH_WAIT_STATE_2);

    DL_SYSCTL_setSYSOSCFreq(DL_SYSCTL_SYSOSC_FREQ_BASE);
    /* Set default configuration */
    DL_SYSCTL_disableHFXT();
    DL_SYSCTL_disableSYSPLL();
    DL_SYSCTL_configSYSPLL((DL_SYSCTL_SYSPLLConfig *) &gSYSPLLConfig);

    /* [SYSPLL_ERR_01] PLL incorrect locking workaround. */
    while (SYSCFG_DL_SYSCTL_SYSPLL_init() == false) {
        /* Toggle SYSPLL enable to re-enable and re-check incorrect locking */
        DL_SYSCTL_disableSYSPLL();
        DL_SYSCTL_enableSYSPLL();

        /* Wait until SYSPLL startup is stabilized */
        while ((DL_SYSCTL_getClockStatus() & SYSCTL_CLKSTATUS_SYSPLLGOOD_MASK) !=
               DL_SYSCTL_CLK_STATUS_SYSPLL_GOOD) {
        }
    }
    DL_SYSCTL_setULPCLKDivider(DL_SYSCTL_ULPCLK_DIV_2);
    /* MCLK source = SYSPLL (HSCLK) */
    DL_SYSCTL_setMCLKSource(SYSOSC, HSCLK, DL_SYSCTL_HSCLK_SOURCE_SYSPLL);
}

/*
 * TIMA0 PWM @ 80 MHz BUSCLK. Period = WS2815_TIMA_PERIOD_TICKS (1.25 us).
 * CC2 drives PB4 (STOP), CC3 drives PA28 (RIGHT).
 */
static const DL_TimerA_ClockConfig gWS2815_TIMA_ClockConfig = {
    .clockSel    = DL_TIMER_CLOCK_BUSCLK,
    .divideRatio = DL_TIMER_CLOCK_DIVIDE_1,
    .prescale    = 0U,
};

static const DL_TimerA_PWMConfig gWS2815_TIMA_PWMConfig = {
    .period           = WS2815_TIMA_PERIOD_TICKS,
    .pwmMode          = DL_TIMER_PWM_MODE_EDGE_ALIGN_UP,
    .isTimerWithFourCC = true,
    .startTimer       = DL_TIMER_STOP,
};

SYSCONFIG_WEAK void SYSCFG_DL_TIMA_init(void)
{
    DL_TimerA_setClockConfig(
        WS2815_TIMA_INST, (DL_TimerA_ClockConfig *) &gWS2815_TIMA_ClockConfig);

    DL_TimerA_initPWMMode(
        WS2815_TIMA_INST, (DL_TimerA_PWMConfig *) &gWS2815_TIMA_PWMConfig);

    /* Edge-align-up: make the counter start at 0 after the timer is enabled */
    DL_Timer_setCounterValueAfterEnable(WS2815_TIMA_INST, DL_TIMER_COUNT_AFTER_EN_ZERO);

    /* CC2 (PB4, STOP): compare value written by DMA is applied at the next ZERO event */
    DL_Timer_setCaptCompUpdateMethod(
        WS2815_TIMA_INST, DL_TIMER_CC_UPDATE_METHOD_ZERO_EVT, DL_TIMER_CC_2_INDEX);
    DL_Timer_setCaptureCompareValue(WS2815_TIMA_INST, 0, DL_TIMER_CC_2_INDEX);

    /* CC3 (PA28, RIGHT): same update scheme */
    DL_Timer_setCaptCompUpdateMethod(
        WS2815_TIMA_INST, DL_TIMER_CC_UPDATE_METHOD_ZERO_EVT, DL_TIMER_CC_3_INDEX);
    DL_Timer_setCaptureCompareValue(WS2815_TIMA_INST, 0, DL_TIMER_CC_3_INDEX);

    /* Enable both CCP pins as outputs */
    DL_TimerA_setCCPDirection(
        WS2815_TIMA_INST, DL_TIMER_CC2_OUTPUT | DL_TIMER_CC3_OUTPUT);

    DL_TimerA_enableClock(WS2815_TIMA_INST);

    /* Publish ZERO event on event-bus channel 1 (feeds DMA GENERIC_SUB0) */
    DL_TimerA_enableEvent(
        WS2815_TIMA_INST, DL_TIMER_EVENT_ROUTE_1, DL_TIMER_EVENT_ZERO_EVENT);
    DL_TimerA_setPublisherChanID(
        WS2815_TIMA_INST, DL_TIMER_PUBLISHER_INDEX_0, WS2815_TIMA_EVENT_CH);
}

/*
 * TIMG0 PWM @ ULPCLK = 40 MHz (PD0 / ULP domain). IMPORTANT: TIMG0 is NOT on
 * BUSCLK -- it is clocked by ULPCLK = MCLK/2 = 40 MHz (ULPCLK divider = DIV_2).
 * Period = WS2815_TIMG_PERIOD_TICKS (50) so the bit rate is still 800 kHz.
 * CC0 drives PA12 (LIFT chain).
 */
static const DL_TimerG_ClockConfig gWS2815_TIMG_ClockConfig = {
    .clockSel    = DL_TIMER_CLOCK_BUSCLK,
    .divideRatio = DL_TIMER_CLOCK_DIVIDE_1,
    .prescale    = 0U,
};

static const DL_TimerG_PWMConfig gWS2815_TIMG_PWMConfig = {
    .period           = WS2815_TIMG_PERIOD_TICKS,
    .pwmMode          = DL_TIMER_PWM_MODE_EDGE_ALIGN_UP,
    .isTimerWithFourCC = false,
    .startTimer       = DL_TIMER_STOP,
};

SYSCONFIG_WEAK void SYSCFG_DL_TIMG_init(void)
{
    DL_TimerG_setClockConfig(
        WS2815_TIMG_INST, (DL_TimerG_ClockConfig *) &gWS2815_TIMG_ClockConfig);

    DL_TimerG_initPWMMode(
        WS2815_TIMG_INST, (DL_TimerG_PWMConfig *) &gWS2815_TIMG_PWMConfig);

    /* Edge-align-up: make the counter start at 0 after the timer is enabled */
    DL_Timer_setCounterValueAfterEnable(WS2815_TIMG_INST, DL_TIMER_COUNT_AFTER_EN_ZERO);

    /* CC0 (PA12, LIFT): compare value written by DMA is applied at the next ZERO event */
    DL_Timer_setCaptCompUpdateMethod(
        WS2815_TIMG_INST, DL_TIMER_CC_UPDATE_METHOD_ZERO_EVT, DL_TIMER_CC_0_INDEX);
    DL_Timer_setCaptureCompareValue(WS2815_TIMG_INST, 0, DL_TIMER_CC_0_INDEX);

    /* Enable CCP0 pin as output */
    DL_TimerG_setCCPDirection(WS2815_TIMG_INST, DL_TIMER_CC0_OUTPUT);

    DL_TimerG_enableClock(WS2815_TIMG_INST);

    /* Publish ZERO event on event-bus channel 2 (feeds DMA GENERIC_SUB1) */
    DL_TimerG_enableEvent(
        WS2815_TIMG_INST, DL_TIMER_EVENT_ROUTE_1, DL_TIMER_EVENT_ZERO_EVENT);
    DL_TimerG_setPublisherChanID(
        WS2815_TIMG_INST, DL_TIMER_PUBLISHER_INDEX_0, WS2815_TIMG_EVENT_CH);
}

/*
 * DMA channels. Each ZERO event advances one byte from the chain's compare
 * buffer into the timer CC register (8-bit writes to the 32-bit CC registers).
 * The channel number is fixed by the trigger wiring (SUB0 = TIMA0 ZERO,
 * SUB1 = TIMG0 ZERO):
 *   CH0: STOP  -> &TIMA0.CC_23[0]  (CC2/PB4),  trigger GENERIC_SUB0
 *   CH1: RIGHT -> &TIMA0.CC_23[1]  (CC3/PA28), trigger GENERIC_SUB0
 *   CH2: LIFT  -> &TIMG0.CC_01[0]  (CC0/PA12), trigger GENERIC_SUB1
 */
static const DL_DMA_Config gWS2815_DMA_CH0_Config = {
    .transferMode  = DL_DMA_SINGLE_TRANSFER_MODE,
    .extendedMode  = DL_DMA_NORMAL_MODE,
    .destIncrement = DL_DMA_ADDR_UNCHANGED,
    .srcIncrement  = DL_DMA_ADDR_INCREMENT,
    .destWidth     = DL_DMA_WIDTH_BYTE,
    .srcWidth      = DL_DMA_WIDTH_BYTE,
    .trigger       = DMA_GENERIC_SUB0_TRIG,
    .triggerType   = DL_DMA_TRIGGER_TYPE_EXTERNAL,
};

static const DL_DMA_Config gWS2815_DMA_CH1_Config = {
    .transferMode  = DL_DMA_SINGLE_TRANSFER_MODE,
    .extendedMode  = DL_DMA_NORMAL_MODE,
    .destIncrement = DL_DMA_ADDR_UNCHANGED,
    .srcIncrement  = DL_DMA_ADDR_INCREMENT,
    .destWidth     = DL_DMA_WIDTH_BYTE,
    .srcWidth      = DL_DMA_WIDTH_BYTE,
    .trigger       = DMA_GENERIC_SUB0_TRIG,
    .triggerType   = DL_DMA_TRIGGER_TYPE_EXTERNAL,
};

static const DL_DMA_Config gWS2815_DMA_CH2_Config = {
    .transferMode  = DL_DMA_SINGLE_TRANSFER_MODE,
    .extendedMode  = DL_DMA_NORMAL_MODE,
    .destIncrement = DL_DMA_ADDR_UNCHANGED,
    .srcIncrement  = DL_DMA_ADDR_INCREMENT,
    .destWidth     = DL_DMA_WIDTH_BYTE,
    .srcWidth      = DL_DMA_WIDTH_BYTE,
    .trigger       = DMA_GENERIC_SUB1_TRIG,
    .triggerType   = DL_DMA_TRIGGER_TYPE_EXTERNAL,
};

SYSCONFIG_WEAK void SYSCFG_DL_DMA_init(void)
{
    /* CH0 = STOP (TIMA0 CC2 / PB4) */
    DL_DMA_initChannel(DMA, 0U, (DL_DMA_Config *) &gWS2815_DMA_CH0_Config);
    /* CH1 = RIGHT (TIMA0 CC3 / PA28) */
    DL_DMA_initChannel(DMA, 1U, (DL_DMA_Config *) &gWS2815_DMA_CH1_Config);
    /* CH2 = LIFT (TIMG0 CC0 / PA12) */
    DL_DMA_initChannel(DMA, 2U, (DL_DMA_Config *) &gWS2815_DMA_CH2_Config);

    /* Event-bus wiring: TIMA0 pub(1) -> DMA FSUB0, TIMG0 pub(2) -> DMA FSUB1 */
    DL_DMA_setSubscriberChanID(
        DMA, DL_DMA_SUBSCRIBER_INDEX_0, WS2815_TIMA_EVENT_CH);
    DL_DMA_setSubscriberChanID(
        DMA, DL_DMA_SUBSCRIBER_INDEX_1, WS2815_TIMG_EVENT_CH);

    /* DMA channel-done interrupts (frame complete) */
    DL_DMA_clearInterruptStatus(DMA, DL_DMA_INTERRUPT_CHANNEL0 |
                                         DL_DMA_INTERRUPT_CHANNEL1 |
                                         DL_DMA_INTERRUPT_CHANNEL2);
    DL_DMA_enableInterrupt(DMA, DL_DMA_INTERRUPT_CHANNEL0 |
                                    DL_DMA_INTERRUPT_CHANNEL1 |
                                    DL_DMA_INTERRUPT_CHANNEL2);
}

/*
 *  UART0 @ 115200 baud, 8N1, BUSCLK = 80 MHz。
 *  PA10 = TX (PINCM21), PA11 = RX (PINCM22)。
 *  RX 中断使能；TX 用于发送 ACK 字符 '#'。
 *
 *  UART0 @ 115200 baud, 8N1, BUSCLK = 80 MHz.
 *  PA10 = TX (PINCM21), PA11 = RX (PINCM22).
 *  RX interrupt enabled; TX used for ACK character '#'.
 */
static const DL_UART_Main_ClockConfig gUART_ClockConfig = {
    .clockSel    = DL_UART_MAIN_CLOCK_BUSCLK,
    .divideRatio = DL_UART_MAIN_CLOCK_DIVIDE_RATIO_1,
};

static const DL_UART_Main_Config gUART_Config = {
    .mode        = DL_UART_MAIN_MODE_NORMAL,
    .direction   = DL_UART_MAIN_DIRECTION_TX_RX,
    .flowControl = DL_UART_MAIN_FLOW_CONTROL_NONE,
    .parity      = DL_UART_MAIN_PARITY_NONE,
    .wordLength  = DL_UART_MAIN_WORD_LENGTH_8_BITS,
    .stopBits    = DL_UART_MAIN_STOP_BITS_ONE,
};

SYSCONFIG_WEAK void SYSCFG_DL_UART_init(void)
{
    DL_UART_Main_setClockConfig(UART_COMM_INST,
        (DL_UART_Main_ClockConfig *) &gUART_ClockConfig);
    DL_UART_Main_init(UART_COMM_INST,
        (DL_UART_Main_Config *) &gUART_Config);

    /* 自动计算 OVS + IBRD/FBRD 以达到目标波特率
     * Auto-calculate OVS + IBRD/FBRD for target baud rate */
    DL_UART_Main_configBaudRate(UART_COMM_INST,
        UART_COMM_CLOCK_FREQ, UART_COMM_BAUD_RATE);

    /* 使能 RX 中断 / Enable RX interrupt */
    DL_UART_Main_enableInterrupt(UART_COMM_INST,
                                 DL_UART_MAIN_INTERRUPT_RX);

    DL_UART_Main_enable(UART_COMM_INST);
}
