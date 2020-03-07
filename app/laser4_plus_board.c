
#include "board.h"
#include "stm32f1xx_hal.h"
#include <stdout.h>
#include "usbd_conf.h"

typedef struct {
    volatile uint16_t result;
    volatile uint16_t status;
    uint32_t battery_voltage;
    uint32_t calibration_code;
    float resolution;
    float vdiv_racio;
    void (*cb)(uint32_t data);
}adc_t;

typedef struct {
    uint32_t time;
    uint32_t count;
    uint32_t status;
    void (*action)(void);
}swtimer_t;

typedef struct {
    tone_t *ptone;
    tone_t tone;
    volatile uint32_t status;
}sound_t;

#ifdef ENABLE_SERIAL_FIFOS
fifo_t serial_tx_fifo;
fifo_t serial_rx_fifo;
#endif

// Private variables
static SPI_HandleTypeDef hspi;
static volatile uint32_t ticks;
static sound_t hbuz;
static adc_t hadc;
static swtimer_t swtim[SWTIM_NUM];
static void (*pinIntCB)(void);

// Private functions
static void spiInit(void);
static void timInit(void);
static void adcInit(void);
static void encInit(void);
static void crcInit(void);
static void ppmOutInit(void);
static void buzInit(void);

// Functions implemenation
void Error_Handler(char * file, int line){
  while(1){
  }
}

void laser4Init(void){
    GPIO_ENABLE;
    AFIO->MAPR = (2 << 24); // SW-DP Enabled
    DBG_PIN_INIT;
    CC25_CS_INIT;
    LED_INIT;
    HW_SW_INIT;
    HW_TX_35MHZ_EN_INIT;
    
    spiInit();
    timInit();
    adcInit();
    encInit();
    ppmOutInit();
    buzInit();
    crcInit();
#ifdef ENABLE_SERIAL_FIFOS
    fifo_init(&serial_rx_fifo);
    fifo_init(&serial_tx_fifo);
#endif
}

void gpioInit(GPIO_TypeDef *port, uint8_t pin, uint8_t mode) {

    
    if(mode == GPI_PD){
        port->BRR = (1 << pin);
    }

    if(mode == GPI_PU){
        port->BSRR = (1 << pin);
    }    

    mode &= 0x0f;

    if(pin <  8){ 
        port->CRL = (port->CRL & ~(15 << (pin << 2))) | (mode << (pin << 2));
    }else{ 
        port->CRH = (port->CRH & ~(15 << ((pin - 8) << 2))) | (mode << ((pin - 8) << 2)); 
    }
}

void gpioAttachInterrupt(GPIO_TypeDef *port, uint8_t pin, uint8_t edge, void(*cb)(void)){
    if(cb == NULL){
        return;
    }
    pinIntCB = cb;
    AFIO->EXTICR[1] = ( 1 << 4);        // PB5 -> EXTI5
    EXTI->IMR  = ( 1 << 5);             // MR5
    EXTI->FTSR = (1 << 5);
    NVIC_EnableIRQ(EXTI9_5_IRQn);
}

void gpioRemoveInterrupt(GPIO_TypeDef *port, uint8_t pin){
    NVIC_DisableIRQ(EXTI9_5_IRQn);
}


void SPI_Write(uint8_t data){
  HAL_SPI_Transmit(&hspi, &data, 1, 10);
} 

uint8_t SPI_Read(void){
uint8_t data;
    HAL_SPI_Receive(&hspi, &data, 1, 10);
    return data;
}

/**
 * 
 */
static void spiInit(){

    __HAL_RCC_SPI2_CLK_ENABLE();
    hspi.Instance = SPI2;
    hspi.Init.Mode = SPI_MODE_MASTER;
    hspi.Init.Direction = SPI_DIRECTION_2LINES;
    hspi.Init.DataSize = SPI_DATASIZE_8BIT;
    //hspi.Init.CLKPolarity = SPI_POLARITY_HIGH;
    //hspi.Init.CLKPhase = SPI_PHASE_1EDGE;
    hspi.Init.NSS = SPI_NSS_SOFT;
    hspi.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_16;
    hspi.Init.FirstBit = SPI_FIRSTBIT_MSB;
    hspi.Init.TIMode = SPI_TIMODE_DISABLE;
    hspi.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    hspi.Init.CRCPolynomial = 10;
    if (HAL_SPI_Init(&hspi) != HAL_OK){
        Error_Handler(__FILE__, __LINE__);
    }

    SPI_PINS_INIT;
}

/**
 * @brief Initialyze 1ms general purpose time base
 *          using timer4
 * 
 * */
static void timInit(void){

    ticks = 0;
    
    RCC->APB1ENR    |= RCC_APB1ENR_TIM4EN | RCC_APB1ENR_TIM3EN | RCC_APB1ENR_TIM2EN;
    RCC->APB2ENR    |= RCC_APB2ENR_TIM1EN;
    RCC->APB1RSTR   |= RCC_APB1RSTR_TIM4RST | RCC_APB1RSTR_TIM3RST | RCC_APB1RSTR_TIM2RST;
    RCC->APB1RSTR   &= ~(RCC_APB1RSTR_TIM4RST | RCC_APB1RSTR_TIM3RST | RCC_APB1RSTR_TIM2RST);
    RCC->APB2RSTR   |= RCC_APB2RSTR_TIM1RST;
    RCC->APB2RSTR   &= ~RCC_APB2RSTR_TIM1RST;
    /* Configure 1ms timer*/
#if NO_SYS_TICK
    TIM4->PSC = (SystemCoreClock/1000000) - 1; // Set Timer clock
    TIM4->ARR = 1000 - 1;
    TIM4->DIER = TIM_DIER_UIE;
    TIM4->CR1 |= TIM_CR1_CEN;
    NVIC_EnableIRQ(TIM4_IRQn);
#else
    SysTick_Config(SystemCoreClock / 1000);
    NVIC_EnableIRQ(SysTick_IRQn);
#endif   
    /* Configure 0.5us time base for multiprotocol */
    TIMER_BASE->CR1 = 0;                               // Stop counter
    TIMER_BASE->PSC = (SystemCoreClock/2000000) - 1;	// 36-1;for 72 MHZ /0.5sec/(35+1)
    TIMER_BASE->ARR = 0xFFFF;							// Count until 0xFFFF
    TIMER_BASE->CCMR1 = (1<<4);	                    // Main scheduler
	TIMER_BASE->SR = 0x1E5F & ~TIM_SR_CC1IF;			// Clear Timer/Comp2 interrupt flag
    TIMER_BASE->DIER = 0;               				// Disable Timer/Comp2 interrupts
    TIMER_BASE->EGR |= TIM_EGR_UG;					    // Refresh the timer's count, prescale, and overflow
    TIMER_BASE->CR1 |= TIM_CR1_CEN;                    // Enable counter
}

void delayMs(uint32_t ms){
uint32_t timeout = ticks + ms;
    while(ticks < timeout){        
    }
}

uint32_t getTick(void){ return ticks; }
uint32_t HAL_GetTick(void){ return getTick(); }

/**
 * @brief Flash write functions for EEPROM emulation
 */
uint32_t flashWrite(uint8_t *dst, uint8_t *data, uint16_t count){
uint16_t *src = (uint16_t*)data;
uint32_t res, address = (uint32_t)dst;

    res = HAL_FLASH_Unlock();
    if( res == HAL_OK){    
        for (uint16_t i = 0; i < count; i+= 2, src++){
            res = HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, address + i, *src);
            if(res != HAL_OK){
                break; 
            }
        }
    }
    HAL_FLASH_Lock();
    return res;
}

/**
 * @brief Erase 1k selctor on flash
 * 
 * @param address:  start address for erasing
 * @return : 0 on fail
 * */
uint32_t flashPageErase(uint32_t address){
uint32_t res;

    res = HAL_FLASH_Unlock();

    if( res == HAL_OK){
        FLASH_PageErase(address);
    }
    
    HAL_FLASH_Lock();
    return 1;
}

/**
 * @brief Configure watchdog timer according a given interval
 *  in wich the timer will expire and a system reset is performed
 * 
 * @param interval : Interval in wich the watchdog will perform a system reset
 * */
void enableWatchDog(uint32_t interval){
uint32_t timeout = 4096;
uint8_t pres = 0;

    interval *= 10;

    if(interval > 0xFFFF){
        interval = 0xFFFF;
    }    

    while( interval > timeout){
        timeout <<= 1;
        pres++;
    }

    if(IWDG->SR != 0){
        // other update is in progress
        return;
    }

    IWDG->KR = 0x5555; // Enable access to PR and RLR registers
    IWDG->PR = pres;
    IWDG->RLR = (interval * 0xFFFF) / timeout;
    IWDG->KR = 0xAAAA;  // Reload
    IWDG->KR = 0xCCCC;  // Start IWDG
}
/**
 * @brief Watchdog reset that mus be called before the interval
 *          specified on configuration
 * 
 * */
void reloadWatchDog(void){
    IWDG->KR = 0xAAAA; // Reload RLR on counter
}

/**
 * @brief Read digital switches values
 * 
 * @return : bitmask with active switches
 * */
uint32_t readSwitches(void){
uint16_t state = 0;

    state = (HW_SW_AUX1_VAL << 0) | (HW_SW_AUX2_VAL << 1) | (HW_SW_AUX3_VAL << 2);

    return state;
}
/**
 * @brief Configure sample time for one adc channel
 * 
 * */
static void adcSampleTime(uint16_t ch, uint16_t time){
    if(ch > 17){  // Max 17 channels
        return;
    }

    if(ch < 10){
        ADC1->SMPR2 =  (ADC1->SMPR2 & (7 << (3 * ch))) | (time << (3 * ch));   // Sample time for channels AN9-0
    }else{
        ADC1->SMPR1 =  (ADC1->SMPR1 & (7 << (3 * (ch % 10)))) | (time << (3 * (ch % 10)));   // Sample time for channels AN17-10
    }
}

/**
 * @brief calibrate ADC and get resolution based 
 * on 1.20V internal reference
 * */
uint32_t adcCalibrate(void){
    ADC1->CR2 |= ADC_CR2_CAL;                     // Perform ADC calibration
    while(ADC1->CR2 & ADC_CR2_CAL){               
        asm volatile("nop");
    }

    hadc.calibration_code = ADC1->DR;
    // Set calibration flag
    hadc.status |= ADC_CAL;

    adcSampleTime(HW_VREFINT_CHANNEL, 2);         // Sample time = 13.5 cycles.
    // select VREFINT channel
    ADC1->SQR3 = (HW_VREFINT_CHANNEL << 0);
    // wake up Vrefint 
    ADC1->CR2 |= ADC_CR2_TSVREFE;
    delayMs(5);
    // Start conversion
    ADC1->CR2 |= ADC_CR2_SWSTART;
    while(!(ADC1->SR & ADC_SR_EOC)){
        asm volatile("nop");
    }
    // Compute resolution
    hadc.resolution = VREFINT_VALUE / ADC1->DR;
    // power down VREFINT
    ADC1->CR2 &= ~ADC_CR2_TSVREFE;
    // Set resolution flag
    hadc.status |= ADC_RES;
    return 1;
}

/**
 * @brief Set voltage divider racio, used to measure battery voltage
 * 
 * @param r : Racio = R2/(R1+R2)
 * */
void adcSetVdivRacio(float r){
    hadc.vdiv_racio = r;
    hadc.status |= ADC_DIV;
}

/**
 * @brief get current voltage divider racio, used to measure battery voltage
 * 
 * @return : R2/(R1+R2)
 * */
float adcGetVdivRacio(void){
    return hadc.vdiv_racio;
}
/**
 * @brief Configure ADC for a HW_VBAT_CHANNEL channel in interrupt mode and initiates a convertion.
 *  After convertion the result is stored locally through the interrupt
 *  
 * PA0/AN0 is the default channel
 * */
static void adcInit(void){
    HW_VBAT_CH_INIT;

    RCC->APB2ENR  |= RCC_APB2ENR_ADC1EN;          // Enable Adc1
    RCC->APB2RSTR |= RCC_APB2ENR_ADC1EN;
    RCC->APB2RSTR &= ~RCC_APB2ENR_ADC1EN;

    ADC1->CR2 = (15 << 17) |         // trigger by software,
                ADC_CR2_ADON;        // Enable ADC
    delayMs(20);
    adcSampleTime(HW_VBAT_CHANNEL, 6);            // Sample time, 4 => 41.5 cycles.
    adcCalibrate();
    ADC1->SQR3 = (HW_VBAT_CHANNEL << 0);
    ADC1->CR1 = ADC_CR1_EOCIE;                    // Enable end of convertion interrupt
    NVIC_EnableIRQ(ADC1_IRQn);
    hadc.status = 0;
}

/**
 * @brief 
 * */
static void adcStartConversion(uint32_t ch){
    hadc.status &= ~ADC_RDY;
    // Set first conversion channel in regular sequence,
    // ignore all other channnels
    ADC1->SQR3 = (ch << 0);
    // Start
    ADC1->CR2 |= ADC_CR2_SWSTART;
}

/**
 * @brief Get current adc resolution (mV/step)
 * */
float adcGetResolution(void){    
    return hadc.resolution;
}

/**
 * @brief End of conversion callback for battery channel
 * 
 * @param data : RAW data from ADC
 * */
static void batteryEOC(uint32_t data){
    hadc.battery_voltage = (float)(data * hadc.resolution) / hadc.vdiv_racio;
}
/**
 * @brief Get battery voltage by start a convertion and wait for it to end
 * 
 * @return : battery voltage in mV
 * */
uint32_t batteryGetVoltage(void){
    hadc.cb = batteryEOC;
    adcStartConversion(HW_VBAT_CHANNEL);
    while((hadc.status & ADC_RDY) == 0 );
    return  hadc.battery_voltage;
}

/**
 * @brief Read battery voltage, if a battery measurement is ready, starts a new 
 * connversion and return the last measured value. If a measurement is not available return
 * 
 * @param dst : Pointer to place measured value
 * 
 * @return : 0 if no measure is available (don't change dst), != 0 on success
 * */
uint32_t batteryReadVoltage(uint32_t *dst){
    if(hadc.status & ADC_RDY){
        *dst = hadc.battery_voltage;
        hadc.cb = batteryEOC;
        adcStartConversion(HW_VBAT_CHANNEL);
        return 1;
    }
    return 0;
}

/**
 * @brief Get instant current consumption (mA)
 * */
float getInstantCurrent(void){
    return 0.0f;
}

/**
 * @brief Rotary encorder init
 *  Configures a timer as pulse counter, the counter is incremented/decremented
 *  on edges from the two signals from the encoder
 * 
 * Using TIM2 CH1 and CH2 as TI1 and TI2, also filter is configured
 * */
void encInit(void){
    gpioInit(GPIOB, 3, GPI_PU);
    gpioInit(GPIOA, 15, GPI_PU);
    AFIO->MAPR = (AFIO->MAPR & ~(3 << 8)) | (1 << 8);       // Partial remap for TIM2; PA15 -> CH1, PB3 -> CH2 

    ENC_TIM->CR2 = 
    ENC_TIM->SMCR = TIM_SMCR_SMS_1 | TIM_SMCR_SMS_0;        // External clock, Encoder mode 3
    ENC_TIM->CCMR1 = (15 << 12) | (15 << 4)                 // Map TIxFP1 to TIx,
                  | TIM_CCMR1_CC2S_0 | TIM_CCMR1_CC1S_0     // and max length if input filter
                  | TIM_CCMR1_IC2PSC_1 | TIM_CCMR1_IC1PSC_1;
    ENC_TIM->CCER = 0;                                      // Falling polarity
    ENC_TIM->CR1 = TIM_CR1_CEN;
    ENC_TIM->SR = 0;
}

/**
 * @brief Generates PPM signal for 6 channels.
 * One extra channel is required in order to produce the last
 * pulse of the last channel. Another channel is added just to avoid wasting
 * cpu cycles waiting for the transmission of the last channel, this way the DMA
 * transfer complete interrupt doesn't stop the timer in the middle of the transmission.
 * 
 * @param data : pointer to the six channels data
 * 
 * */
void ppmOut(uint16_t *data){
static uint16_t ppm_data[MAX_PPM_CHANNELS + 2];
    // Copy channel data to temp buffer
    for (uint16_t i = 0; i < MAX_PPM_CHANNELS; i++){
        ppm_data[i] = data[i];
    }
    // Set extra channels to end ppm signal
    ppm_data[MAX_PPM_CHANNELS] = PPM_MAX_PERIOD;
    ppm_data[MAX_PPM_CHANNELS + 1] = PPM_MAX_PERIOD;
    // Force counter update and DMA request, setting the period here
    // will produce a rising edge, but as the ppm line should be high
    // we get only the initial ppm low pulse.
    PPM_TIM->ARR = ppm_data[0];
    PPM_TIM->EGR = TIM_EGR_UG;
    // Configure DMA transfer, the first transfer will have the value on ppm_data[0]
    // since this value was transferred to produce initial pulse, it is necessary to send it again
    // to generate the channel time.
    DMA1_Channel7->CMAR = (uint32_t)(ppm_data);
    DMA1_Channel7->CNDTR = MAX_PPM_CHANNELS + 2;
    DMA1_Channel7->CCR |= DMA_CCR_EN;   
    // Resume timer
    PPM_TIM->CR1 |= TIM_CR1_CEN;
    //DBG_PIN_HIGH;
}

/**
 * @brief PPM output generation init
 * */
void ppmOutInit(void){
    gpioInit(GPIOB, 7, GPO_AF | GPO_2MHZ);

     /* Configure DMA Channel1*/
    RCC->AHBENR |= RCC_AHBENR_DMA1EN;               // Enable DMA1
    DMA1_Channel7->CPAR = (uint32_t)&PPM_TIM->ARR;  // Destination peripheral
    DMA1_Channel7->CCR =
            DMA_CCR_PL |                            // Highest priority
            DMA_CCR_MSIZE_0 |                       // 16bit Dst size
            DMA_CCR_PSIZE_0 |                       // 16bit src size
            DMA_CCR_DIR |                           // Read from memory
            DMA_CCR_MINC |                          // increment memory pointer after transference
            DMA_CCR_TCIE;                           // Enable end of transfer interrupt
    NVIC_EnableIRQ(DMA1_Channel7_IRQn);

    PPM_TIM->CR1 =  TIM_CR1_DIR | TIM_CR1_ARPE;
    PPM_TIM->PSC = (SystemCoreClock/2000000) - 1;	// 36-1;for 72 MHZ /0.5sec/(35+1)
    PPM_TIM->CCMR1 = (7 << 12);                     // PWM mode 2
    PPM_TIM->CCER = TIM_CCER_CC2E;                  // Enable channel
    // Force high state
    PPM_TIM->ARR = PPM_MAX_PERIOD;
    PPM_TIM->CNT = PPM_MAX_PERIOD;
    PPM_TIM->CCR2 = PPM_PULSE_WIDTH;
    // Enable DMA Request 
    PPM_TIM->DIER |= TIM_DIER_UDE; 
}

/**
 * @brief Basic tone generation on pin PA8 using TIM1_CH1
 * and DMA 
 * 
 * Buzzer timer is configured as PWM mode1 in downcount mode.
 * The counter starts from ARR register (top) that defines the frequency perioud in us,
 * and counts down, when matches CCR1 the output is set to high.
 * When the counter reaches zero, set the output to low and request a DMA transfer to ARR register.
 * On the last DMA transfer an interrupt is issued, that will configure the next tone periout to be 
 * loaded to ARR or stop the melody. 
 * 
 * */
void buzInit(void){
    gpioInit(GPIOA, 8, GPO_AF | GPO_2MHZ);
    // Configure DMA
    RCC->AHBENR |= RCC_AHBENR_DMA1EN;               // Enable DMA1
    DMA1_Channel5->CPAR = (uint32_t)&BUZ_TIM->ARR;  // Destination peripheral
    DMA1_Channel5->CCR =
            DMA_CCR_MSIZE_0 |                       // 16bit Dst size
            DMA_CCR_PSIZE_0 |                       // 16bit src size
            DMA_CCR_DIR |                           // Read from memory
            DMA_CCR_TCIE;                           // Enable end of transfer interrupt
    NVIC_EnableIRQ(DMA1_Channel5_IRQn);

    // Configure timer    
#ifdef BUZ_IDLE_HIGH
    BUZ_TIM->CR1 = 0; 
    BUZ_TIM->CCMR1 = (7 << 4);                      // PWM mode 2
#else
    BUZ_TIM->CR1 = TIM_CR1_DIR; 
    BUZ_TIM->CCMR1 = (6 << 4);                      // PWM mode 1
#endif
    BUZ_TIM->PSC = (SystemCoreClock/1000000) - 1;	// 72-1;for 72 MHZ (1us clock)
    BUZ_TIM->CCER = TIM_CCER_CC1E;                  // Enable channel
    BUZ_TIM->BDTR |= TIM_BDTR_MOE;                  // Necessary for TIM1
    // Force idle state
    BUZ_TIM->CCR1 = BUZ_DEFAULT_VOLUME;             // Low volume level
    BUZ_TIM->ARR = 0xFFF;
    BUZ_TIM->EGR |= TIM_EGR_UG;
    // Enable DMA Request 
    BUZ_TIM->DIER |= TIM_DIER_UDE;
}

/**
 * @brief Private helper to initiate tone generation
 * 
 * @param tone : pointer to first tone to be played
 * */
static void buzStartTone(tone_t *tone){
    DMA1_Channel5->CMAR = (uint32_t)(&tone->f);
    DMA1_Channel5->CNDTR = tone->t;
    DMA1_Channel5->CCR |= DMA_CCR_EN;
    BUZ_TIM->EGR = TIM_EGR_UG;
    BUZ_TIM->CR1 |=  TIM_CR1_CEN;
    hbuz.status |= BUZ_PLAYING;
}

/**
 * @brief Plays a single tone for a given time
 * 
 * @param freq     : Tone fundamental frequency
 * @param duration : duration of tone in ms
 * */
void buzPlayTone(uint16_t freq, uint16_t duration){
uint32_t d = duration * 1000UL;    // Convert to us
    hbuz.tone.f = FREQ_TO_US(freq) - BUZ_TIM->CCR1; // Subtract volume pulse
    hbuz.tone.t = d / hbuz.tone.f;
    hbuz.ptone = &hbuz.tone;
    buzStartTone(hbuz.ptone);
    hbuz.ptone->t = 0;       //force tone ending
}

/**
 * @brief Plays a melody composed of multiple tones.
 * The last tone on melody must have duration of zero
 * 
 * @param tones : pointer to tones array.
 * */
void buzPlay(tone_t *tones){
tone_t *pt = tones;

    // Convert each tone frequency to time in us
    while(pt->t > 0){
        uint32_t d = pt->t * 1000UL;
        pt->f = FREQ_TO_US(pt->f) - BUZ_TIM->CCR1;  
        pt->t = d / pt->f;
        pt++;
    }

    // Set next tone
    hbuz.ptone = tones + 1;
    // Play first tone
    buzStartTone(tones);   
}

/**
 * @brief Change tone volume by changing 
 * duty cycle
 * 
 * @param level : Tone volume 0 to tone frequency
 *  
 * */
void buzSetLevel(uint16_t level){
    BUZ_TIM->CCR1 = level-1;
}

/**
 * @brief Waits for the end of tone(s)
 * Blocking call duh..
 * */
void buzWaitEnd(void){
    while(hbuz.status & BUZ_PLAYING);
}


/**
 * @brief Enable CRC unit
 * */
void crcInit(void){
    RCC->AHBENR |= RCC_AHBENR_CRCEN;
    CRC->CR = 1;
}

/**
 * @brief Start a software timer
 * 
 * @param time : Timer duration
 * @param flags : Extra flags for continuous mode, 0 for single time
 * @param cb : callback function when timer expires
 * 
 * @return : Assigned timer index
 * */
uint32_t startTimer(uint32_t time, uint32_t flags, void (*cb)(void)){

    for(uint32_t i = 0; i < SWTIM_NUM; i++){
        if((swtim[i].status & SWTIM_RUNNING) == 0){
            swtim[i].time = time;
            swtim[i].count = 0;
            swtim[i].action = cb;
            swtim[i].status = flags | SWTIM_RUNNING;
            return i;
        }
    }
    return SWTIM_NUM;
}

/**
 * */
void stopTimer(uint32_t tim){
    swtim[tim].status = 0;
}

/**
 * @brief Check if timers have expired and execute correspondent action
 * 
 * */
void processTimers(void){
static uint32_t last_ticks = 0;
uint32_t diff = ticks - last_ticks;

    for(uint32_t i = 0; i < SWTIM_NUM; i++){
        if(swtim[i].status & SWTIM_RUNNING){
            swtim[i].count += diff;
            if(swtim[i].count >= swtim[i].time){
                swtim[i].action();
                if(swtim[i].status & SWTIM_AUTO){
                    swtim[i].count = 0;
                }else{
                    swtim[i].status &= ~(SWTIM_RUNNING);
                }
            }
        }
    }

    last_ticks = ticks;
}

/**
 * @brief meh close enougth
 * 
 * @return : CRC'd number with timer
 * */
uint32_t xrand(void){
    CRC->DR = TIMER_BASE->CNT;
    return CRC->DR;
}

/**
 * @brief Interrupts handlers
 * */
/**
  * @brief  USB Interrupt handler
  * @param  none
  * @retval None
  */
void USB_LP_CAN1_RX0_IRQHandler(void)
{
    HAL_PCD_IRQHandler(&hpcd_USB_FS);
}

void EXTI9_5_IRQHandler(void){
uint32_t pr = EXTI->PR;
    if((pr & EXTI_PR_PR5) != 0){        
        pinIntCB();
    }
    EXTI->PR = pr;
}

#ifdef NO_SYS_TICK
void TIM4_IRQHandler(void){
    TIM4->SR = ~TIM4->SR;
    ticks++;
    //DBG_PIN_TOGGLE;
}
#else
void SysTick_Handler(void){
    ticks++;
}
#endif

void ADC1_IRQHandler(void){
    if(hadc.cb != NULL){
        hadc.cb(ADC1->DR);
    }else{
        hadc.result = ADC1->DR;
    }
    hadc.status |= ADC_RDY;
    //ADC1->SR = 0;
}

void DMA1_Channel5_IRQHandler(void){
    if(DMA1->ISR & DMA_ISR_TCIF5){
        DMA1_Channel5->CCR &= ~DMA_CCR_EN;
        if(hbuz.ptone->t != 0){
            // Load next tone
            DMA1_Channel5->CMAR = (uint32_t)(&hbuz.ptone->f);
            DMA1_Channel5->CNDTR = hbuz.ptone->t;
            DMA1_Channel5->CCR |= DMA_CCR_EN;
            hbuz.ptone++;
        }else{
            // Tone ended, stop tone generation
            BUZ_TIM->CR1 &= ~TIM_CR1_CEN;
            BUZ_TIM->ARR = 0xFFF;
            BUZ_TIM->EGR = TIM_EGR_UG;
            hbuz.status &= ~BUZ_PLAYING;
        }
    }
    DMA1->IFCR |= DMA_IFCR_CGIF5;
}

void DMA1_Channel7_IRQHandler(void){
    if(DMA1->ISR & DMA_ISR_TCIF7){
        DMA1_Channel7->CCR &= ~DMA_CCR_EN;
        // As two extra channels were send,
        // we end up here when transfering the first extra channel.
        // As the timer is stoped we get only a rising edge due to update event
        // and cancel the last channel transmission.
        PPM_TIM->CR1 &= ~TIM_CR1_CEN;
        //DBG_PIN_LOW;        
    }
    DMA1->IFCR |= DMA_IFCR_CGIF7;  // Clear DMA Flags TODO: ADD DMA Error handling ?
}
