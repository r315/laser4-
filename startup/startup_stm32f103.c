#include <stdint.h>
#include "board.h"


#define HSI_VALUE 8000000U /*!< Default value of the Internal oscillator in Hz. */
#define HSE_VALUE 8000000U /*!< Default value of the External oscillator in Hz. */
#define WEAK __attribute__((weak))
#define NAKED __attribute__((naked))
#define ISR __attribute__((section(".isr_vector")))

/* RCC_CR Bit Banding definitions 
    address = bit_banding_peripheral_base + (offset * 32) + (bit * 4)
*/
#define RCC_CR_HSEON_bb (*(uint8_t *)0x42420040UL)
#define RCC_CR_HSERDY_bb *((uint8_t *)0x42420044UL)
#define RCC_CR_PLLON_bb *((uint8_t *)0x42420060UL)
#define RCC_CR_PLLRDY_bb *((uint8_t *)0x42420064UL)

uint32_t SystemCoreClock;
extern unsigned int _sidata, _sdata, _edata, _sbss, _ebss, _stack, _estack;

static void errorHandler(void){
    while (1){
        asm("nop");
    }
}

NAKED void Reset_Handler(void){
    volatile unsigned *src, *dest;

    /* Copy .data section */
    for (src = &_sidata, dest = &_sdata; dest < &_edata; src++, dest++){
        *dest = *src;
    }

    /* Clear .bss */
    dest = &_sbss;

    while (dest < &_ebss)
        *dest++ = 0;

    /* Reset the RCC clock configuration to the default reset state(for debug purpose) */
    /* Set HSION bit */
    RCC->CR |= 0x00000001U;
    /* Reset SW, HPRE, PPRE1, PPRE2, ADCPRE and MCO bits */
    RCC->CFGR &= 0xF8FF0000U;
    /* Reset HSEON, CSSON and PLLON bits */
    RCC->CR &= 0xFEF6FFFFU;
    /* Reset HSEBYP bit */
    RCC->CR &= 0xFFFBFFFFU;
    /* Reset PLLSRC, PLLXTPRE, PLLMUL and USBPRE/OTGFSPRE bits */
    RCC->CFGR &= 0xFF80FFFFU;
    /* Disable all interrupts and clear pending bits  */
    RCC->CIR = 0x009F0000U;

#ifdef VECT_TAB_SRAM
    SCB->VTOR = SRAM_BASE | VECT_TAB_OFFSET; /* Vector Table Relocation in Internal SRAM. */
#else
    /* Vector Table Relocation to startup (g_pfnVectors) vector table  */
    SCB->VTOR = FLASH_BASE;
#endif

    /* ------------- Configure system clock --------------- */
#define CLOCK_CFG_TIMEOUT 0x10000
    uint32_t timeout = CLOCK_CFG_TIMEOUT;

    /* Enable HSE */
    RCC_CR_HSEON_bb = 1;
    while ((RCC->CR & RCC_CR_HSERDY) == 0){
        timeout--;
        if (timeout == 0)
        {
            errorHandler();
        }
    }

    /* Configure and enable PLL oscillator (sysclk = 72Mhz) */
    
    RCC->CFGR = //(4 << 24) |                 // MCO = sysclk
                (7 << 18) |                 // PLLMUL = 9
                (1 << 16) |                 // PLLSRC = PREDIV1
                (2 << 14) |                 // ADCPRE = 6
                (4 << 8);                   // PPRE1 = 2

    timeout = CLOCK_CFG_TIMEOUT;
    RCC_CR_PLLON_bb = 1;
    while (RCC_CR_PLLRDY_bb == 0){
        timeout--;
        if (timeout == 0)
        {
            errorHandler();
        }
    }

    /* Configure flash latency */
    FLASH->ACR |= 2;                        // Two wait states

    /* Switch  sysclk*/
    RCC->CFGR |= (1 << 1);                  // select PLL as system clock
    timeout = CLOCK_CFG_TIMEOUT;
    while ((RCC->CFGR & (1 << 3)) == 0){
        timeout--;
        if (timeout == 0)
        {
            errorHandler();
        }
    }
    //__libc_init_array();

#if defined(RTOS)
    app_setup();
    /* Configure tasks*/
    /* Start scheduler */
#else    
    app_main();
#endif
    /* case returns... */
    asm("b .");
}

void defaultHandler(void){
    while (1){
    }
}

WEAK void NMI_Handler(void);
WEAK void HardFault_Handler(void);
WEAK void MemManage_Handler(void);
WEAK void BusFault_Handler(void);
WEAK void UsageFault_Handler(void);
WEAK void SVC_Handler(void);
WEAK void DebugMon_Handler(void);
WEAK void PendSV_Handler(void);
WEAK void SysTick_Handler(void);
WEAK void WWDG_IRQHandler(void);
WEAK void PVD_IRQHandler(void);
WEAK void TAMPER_IRQHandler(void);
WEAK void RTC_IRQHandler(void);
WEAK void FLASH_IRQHandler(void);
WEAK void RCC_IRQHandler(void);
WEAK void EXTI0_IRQHandler(void);
WEAK void EXTI1_IRQHandler(void);
WEAK void EXTI2_IRQHandler(void);
WEAK void EXTI3_IRQHandler(void);
WEAK void EXTI4_IRQHandler(void);
WEAK void DMA1_Channel1_IRQHandler(void);
WEAK void DMA1_Channel2_IRQHandler(void);
WEAK void DMA1_Channel3_IRQHandler(void);
WEAK void DMA1_Channel4_IRQHandler(void);
WEAK void DMA1_Channel5_IRQHandler(void);
WEAK void DMA1_Channel6_IRQHandler(void);
WEAK void DMA1_Channel7_IRQHandler(void);
WEAK void ADC1_2_IRQHandler(void);
WEAK void USB_HP_CAN1_TX_IRQHandler(void);
WEAK void USB_LP_CAN1_RX0_IRQHandler(void);
WEAK void CAN1_RX1_IRQHandler(void);
WEAK void CAN1_SCE_IRQHandler(void);
WEAK void EXTI9_5_IRQHandler(void);
WEAK void TIM1_BRK_IRQHandler(void);
WEAK void TIM1_UP_IRQHandler(void);
WEAK void TIM1_TRG_COM_IRQHandler(void);
WEAK void TIM1_CC_IRQHandler(void);
WEAK void TIM2_IRQHandler(void);
WEAK void TIM3_IRQHandler(void);
WEAK void TIM4_IRQHandler(void);
WEAK void I2C1_EV_IRQHandler(void);
WEAK void I2C1_ER_IRQHandler(void);
WEAK void I2C2_EV_IRQHandler(void);
WEAK void I2C2_ER_IRQHandler(void);
WEAK void SPI1_IRQHandler(void);
WEAK void SPI2_IRQHandler(void);
WEAK void USART1_IRQHandler(void);
WEAK void USART2_IRQHandler(void);
WEAK void USART3_IRQHandler(void);
WEAK void EXTI15_10_IRQHandler(void);
WEAK void RTC_Alarm_IRQHandler(void);
WEAK void USBWakeUp_IRQHandler(void);

#pragma weak NMI_Handler = defaultHandler
#pragma weak HardFault_Handler = defaultHandler
#pragma weak MemManage_Handler = defaultHandler
#pragma weak BusFault_Handler = defaultHandler
#pragma weak UsageFault_Handler = defaultHandler
#pragma weak SVC_Handler = defaultHandler
#pragma weak DebugMon_Handler = defaultHandler
#pragma weak PendSV_Handler = defaultHandler
#pragma weak SysTick_Handler = defaultHandler
#pragma weak WWDG_IRQHandler = defaultHandler
#pragma weak PVD_IRQHandler = defaultHandler
#pragma weak TAMPER_IRQHandler = defaultHandler
#pragma weak RTC_IRQHandler = defaultHandler
#pragma weak FLASH_IRQHandler = defaultHandler
#pragma weak RCC_IRQHandler = defaultHandler
#pragma weak EXTI0_IRQHandler = defaultHandler
#pragma weak EXTI1_IRQHandler = defaultHandler
#pragma weak EXTI2_IRQHandler = defaultHandler
#pragma weak EXTI3_IRQHandler = defaultHandler
#pragma weak EXTI4_IRQHandler = defaultHandler
#pragma weak DMA1_Channel1_IRQHandler = defaultHandler
#pragma weak DMA1_Channel2_IRQHandler = defaultHandler
#pragma weak DMA1_Channel3_IRQHandler = defaultHandler
#pragma weak DMA1_Channel4_IRQHandler = defaultHandler
#pragma weak DMA1_Channel5_IRQHandler = defaultHandler
#pragma weak DMA1_Channel6_IRQHandler = defaultHandler
#pragma weak DMA1_Channel7_IRQHandler = defaultHandler
#pragma weak ADC1_2_IRQHandler = defaultHandler
#pragma weak USB_HP_CAN1_TX_IRQHandler = defaultHandler
#pragma weak USB_LP_CAN1_RX0_IRQHandler = defaultHandler
#pragma weak CAN1_RX1_IRQHandler = defaultHandler
#pragma weak CAN1_SCE_IRQHandler = defaultHandler
#pragma weak EXTI9_5_IRQHandler = defaultHandler
#pragma weak TIM1_BRK_IRQHandler = defaultHandler
#pragma weak TIM1_UP_IRQHandler = defaultHandler
#pragma weak TIM1_TRG_COM_IRQHandler = defaultHandler
#pragma weak TIM1_CC_IRQHandler = defaultHandler
#pragma weak TIM2_IRQHandler = defaultHandler
#pragma weak TIM3_IRQHandler = defaultHandler
#pragma weak TIM4_IRQHandler = defaultHandler
#pragma weak I2C1_EV_IRQHandler = defaultHandler
#pragma weak I2C1_ER_IRQHandler = defaultHandler
#pragma weak I2C2_EV_IRQHandler = defaultHandler
#pragma weak I2C2_ER_IRQHandler = defaultHandler
#pragma weak SPI1_IRQHandler = defaultHandler
#pragma weak SPI2_IRQHandler = defaultHandler
#pragma weak USART1_IRQHandler = defaultHandler
#pragma weak USART2_IRQHandler = defaultHandler
#pragma weak USART3_IRQHandler = defaultHandler
#pragma weak EXTI15_10_IRQHandler = defaultHandler
#pragma weak RTC_Alarm_IRQHandler = defaultHandler
#pragma weak USBWakeUp_IRQHandler = defaultHandler

ISR void *g_pfnVectors[] = {
    &_estack,
    Reset_Handler,
    NMI_Handler,
    HardFault_Handler,
    MemManage_Handler,
    BusFault_Handler,
    UsageFault_Handler,
    0,
    0,
    0,
    0,
    SVC_Handler,
    DebugMon_Handler,
    0,
    PendSV_Handler,
    SysTick_Handler,
    WWDG_IRQHandler,
    PVD_IRQHandler,
    TAMPER_IRQHandler,
    RTC_IRQHandler,
    FLASH_IRQHandler,
    RCC_IRQHandler,
    EXTI0_IRQHandler,
    EXTI1_IRQHandler,
    EXTI2_IRQHandler,
    EXTI3_IRQHandler,
    EXTI4_IRQHandler,
    DMA1_Channel1_IRQHandler,
    DMA1_Channel2_IRQHandler,
    DMA1_Channel3_IRQHandler,
    DMA1_Channel4_IRQHandler,
    DMA1_Channel5_IRQHandler,
    DMA1_Channel6_IRQHandler,
    DMA1_Channel7_IRQHandler,
    ADC1_2_IRQHandler,
    USB_HP_CAN1_TX_IRQHandler,
    USB_LP_CAN1_RX0_IRQHandler,
    CAN1_RX1_IRQHandler,
    CAN1_SCE_IRQHandler,
    EXTI9_5_IRQHandler,
    TIM1_BRK_IRQHandler,
    TIM1_UP_IRQHandler,
    TIM1_TRG_COM_IRQHandler,
    TIM1_CC_IRQHandler,
    TIM2_IRQHandler,
    TIM3_IRQHandler,
    TIM4_IRQHandler,
    I2C1_EV_IRQHandler,
    I2C1_ER_IRQHandler,
    I2C2_EV_IRQHandler,
    I2C2_ER_IRQHandler,
    SPI1_IRQHandler,
    SPI2_IRQHandler,
    USART1_IRQHandler,
    USART2_IRQHandler,
    USART3_IRQHandler,
    EXTI15_10_IRQHandler,
    RTC_Alarm_IRQHandler,
    USBWakeUp_IRQHandler,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
};