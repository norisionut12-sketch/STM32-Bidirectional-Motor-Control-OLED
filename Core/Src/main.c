#include "main.h"

/* libraria pentru display-ul OLED */
#include "ssd1306.h"  
 /* pentru functia sprintf (sa formatez textul pe display) */
#include <stdio.h>
 /* definitiile de caractere necesare pentru generarea textului pe ecranul OLED pixel cu pixel */
#include "ssd1306_fonts.h"

ADC_HandleTypeDef hadc1;

I2C_HandleTypeDef hi2c1;

TIM_HandleTypeDef htim1;

UART_HandleTypeDef huart2;

void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_ADC1_Init(void);
static void MX_I2C1_Init(void);
static void MX_TIM1_Init(void);
static void MX_USART2_UART_Init(void);

/* Valoarea maxima pe care o poate da ADC-ul (12 biti = 2^12 - 1 = 4095) */
#define VALOARE_MAX_ADC 4095

/* Valoarea maxima a registrului CCR1 pentru PWM.
 * Am setat ARR = 999 in CubeMX, deci 1000 = 100% duty cycle */
#define VALOARE_MAX_PWM 1000

/* Pragul de mijloc al potentiometrului ( pe la jumatatea lui 4095 ) */
#define PRAG_DE_MIJLOC 2047

/* Zona neutra ( STOP ) : intre aceste doua valori ADC, motorul NU se misca */
#define LIMITA_ZONA_NEUTRA_JOS   2000
#define LIMITA_ZONA_NEUTRA_SUS   2100


 /* Directie motor */
typedef struct {
    GPIO_TypeDef* port;      /* portul GPIO */
    uint16_t      pin_in1;   /* pinul IN1 al L293D -> inainte */
    uint16_t      pin_in2;   /* pinul IN2 al L293D -> inapoi  */
} PiniDirectieMotor;

PiniDirectieMotor motor_dc = {
    .port    = GPIOA,
    .pin_in1 = GPIO_PIN_9,
    .pin_in2 = GPIO_PIN_10
};

/* Declararea externa a perifericelor */
extern ADC_HandleTypeDef  hadc1;   /* ADC1 - citeste potentiometrul */
extern TIM_HandleTypeDef  htim1;   /* TIM1 - genereaza semnalul PWM */
extern I2C_HandleTypeDef  hi2c1;   /* I2C1 - comunica cu display-ul */

/* Valoarea citita de la ADC (de la 0 pana la 4095) */
uint32_t valoare_adc_bruta = 0;

/* Valoarea PWM calculata si trimisa la motor (de la 0 pana la 1000) */
uint32_t valoare_pwm = 0;

/* Viteza exprimata in procente, pentru afisare pe display (de la 0 pana la 100 %) */
uint32_t procent_viteza = 0;

/* Un sir de caractere care va contine directia: "INAINTE", "INAPOI", "STOP" */
char text_directie[12] = "STOP";

/* Buffer pentru a construi textele afisate pe OLED */
char buffer_display[32];

/*   IN1=1, IN2=0 -> motorul se roteste intr-un sens ( "inainte" ) */
void seteaza_directia_inainte(void)
{
    HAL_GPIO_WritePin(motor_dc.port, motor_dc.pin_in1, GPIO_PIN_SET);    /* IN1 = 1 */
    HAL_GPIO_WritePin(motor_dc.port, motor_dc.pin_in2, GPIO_PIN_RESET);  /* IN2 = 0 */
}

/*   IN1=1, IN2=0 -> motorul se roteste intr-un sens ( "inapoi" ) */
void seteaza_directia_inapoi(void)
{
    HAL_GPIO_WritePin(motor_dc.port, motor_dc.pin_in1, GPIO_PIN_RESET);  /* IN1 = 0 */
    HAL_GPIO_WritePin(motor_dc.port, motor_dc.pin_in2, GPIO_PIN_SET);    /* IN2 = 1 */
}

/* Punem ambii pini IN1 si IN2 pe LOW */
void opreste_motorul(void)
{
    HAL_GPIO_WritePin(motor_dc.port, motor_dc.pin_in1, GPIO_PIN_RESET);  /* IN1 = 0 */
    HAL_GPIO_WritePin(motor_dc.port, motor_dc.pin_in2, GPIO_PIN_RESET);  /* IN2 = 0 */
}

/*
 Scrie valoarea in registrul CCR1 al TIM1, care controleaza
 cat timp pe perioada semnalului PWM pinul PA8 sta pe HIGH.
     valoare_ccr = 0    -> PA8 mereu LOW -> motorul stat
     valoare_ccr = 500  -> PA8 HIGH 50% din timp -> viteza medie
     valoare_ccr = 1000 -> PA8 mereu HIGH -> viteza maxima
 */
void seteaza_viteza_pwm(uint32_t valoare_ccr)
{
    if (valoare_ccr > VALOARE_MAX_PWM)
    {
        valoare_ccr = VALOARE_MAX_PWM;
    }
    /* Scriem valoarea in registrul de comparare al timerului */
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, valoare_ccr);
}


int main(void)
{
  HAL_Init();
  /* Configuram clock-ul */
  SystemClock_Config();

  MX_GPIO_Init();
  MX_ADC1_Init();
  MX_I2C1_Init();
  MX_TIM1_Init();
  MX_USART2_UART_Init();

  /* Initializam display-ul OLED */
     ssd1306_Init();

     /* Stergem orice ar fi pe display (il facem full negru) */
     ssd1306_Fill(Black);

     /* Afisam un mesaj, cat timp se initializeaza restul */
     ssd1306_SetCursor(40, 2);
     ssd1306_WriteString("Control", Font_7x10, White);
     ssd1306_SetCursor(18, 14);
     ssd1306_WriteString("Bidirectional", Font_7x10, White);
     ssd1306_SetCursor(15, 28);
     ssd1306_WriteString("Motor DC + PWM", Font_7x10, White);
     ssd1306_SetCursor(46, 44);
     ssd1306_WriteString("STM32", Font_7x10, White);
     ssd1306_UpdateScreen();

     /* Asteptam 4 secunde sa se vada mesajul */
     HAL_Delay(4000);

     /* Pornim PWM-ul pe TIM1, canalul 1
      * Daca nu apelam aceasta functie, pinul ramane simplu GPIO
      * si nu se genereaza semnal PWM */
     HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);

     /* La pornire, motorul trebuie sa stea pe loc */
     opreste_motorul();
     seteaza_viteza_pwm(0);

     /* Pornim conversia ADC in mod continuu
      * Dupa HAL_ADC_Start, ADC-ul incepe sa converteasca repetat */
     HAL_ADC_Start(&hadc1);

  while (1)
     {
         HAL_ADC_Start(&hadc1);


         /*  Citim valoarea de la potentiometru prin ADC
          * HAL_ADC_PollForConversion asteapta ca ADC-ul sa termine conversia curenta */
         if (HAL_ADC_PollForConversion(&hadc1, 10) == HAL_OK)
         {
             /* Conversia s-a terminat, citim valoarea */
             valoare_adc_bruta = HAL_ADC_GetValue(&hadc1);
         }
         /* Daca nu am primit raspuns OK, valoare_adc_bruta ramane ce era inainte */


         /*  Decidem ce face motorul in functie de pozitia potentiometrului
          * Impartim intervalul 0-4095 in trei zone:
          *
          *   ZONA INAPOI  : valoare_adc_bruta < LIMITA_ZONA_NEUTRA_JOS (sub 2000)
          *   ZONA NEUTRA  : 2000 <= valoare_adc_bruta <= 2100  (STOP!)
          *   ZONA INAINTE : valoare_adc_bruta > LIMITA_ZONA_NEUTRA_SUS (peste 2100) */

         if (valoare_adc_bruta > LIMITA_ZONA_NEUTRA_SUS)
         {
             /*  ZONA INAINTE: potentiometrul e in jumatatea dreapta */
             valoare_pwm = (valoare_adc_bruta - LIMITA_ZONA_NEUTRA_SUS)
                           * VALOARE_MAX_PWM
                           / (VALOARE_MAX_ADC - LIMITA_ZONA_NEUTRA_SUS);

             /* Calculam procentul de viteza pentru display (0-100%) */
             procent_viteza = valoare_pwm * 100 / VALOARE_MAX_PWM;

             /* Setam directia si viteza */
             seteaza_directia_inainte();
             seteaza_viteza_pwm(valoare_pwm);

             /* textul pentru display */
             sprintf(text_directie, ">> INAINTE");
         }
         else if (valoare_adc_bruta < LIMITA_ZONA_NEUTRA_JOS)
         {
             /* ZONA INAPOI: potentiometrul e in jumatatea stanga */
             valoare_pwm = (LIMITA_ZONA_NEUTRA_JOS - 1 - valoare_adc_bruta)
                           * VALOARE_MAX_PWM
                           / (LIMITA_ZONA_NEUTRA_JOS - 1);

             /* Calculam procentul de viteza pentru display */
             procent_viteza = valoare_pwm * 100 / VALOARE_MAX_PWM;

             /* Setam directia si viteza */
             seteaza_directia_inapoi();
             seteaza_viteza_pwm(valoare_pwm);

             /* Pregatim textul pentru display */
             sprintf(text_directie, "<< INAPOI");
         }
         else
         {
             /* ZONA NEUTRA (deadzone): potentiometrul e la mijloc */
             valoare_pwm    = 0;
             procent_viteza = 0;

             opreste_motorul();
             seteaza_viteza_pwm(0);

             sprintf(text_directie, "-- STOP --");
         }


         /*  Afisam informatiile pe display-ul OLED
          * Stergem display-ul si redesenam totul */

         /* Stergem tot display-ul (fundal full negru) */
         ssd1306_Fill(Black);

         /* Linia 1: Procentul de viteza */
         ssd1306_SetCursor(0, 0);
         ssd1306_WriteString("Viteza:", Font_7x10, White);

         sprintf(buffer_display, "%3lu%%", procent_viteza);
         ssd1306_SetCursor(54, 0);
         ssd1306_WriteString(buffer_display, Font_11x18, White);

         /* Linia 2: Bara de progres vizuala  */
         uint32_t latime_bara = procent_viteza * 124 / 100;

         /* Chenarul barei (dreptunghi gol) */
         for (uint8_t x = 0; x < 128; x++)
         {
             ssd1306_DrawPixel(x, 22, White);  /* linia de sus */
             ssd1306_DrawPixel(x, 30, White);  /* linia de jos */
         }

         /* marginea stanga */
         ssd1306_DrawPixel(0,   23, White);
         ssd1306_DrawPixel(0,   24, White);
         ssd1306_DrawPixel(0,   25, White);
         ssd1306_DrawPixel(0,   26, White);
         ssd1306_DrawPixel(0,   27, White);
         ssd1306_DrawPixel(0,   28, White);
         ssd1306_DrawPixel(0,   29, White);
         /* marginea dreapta */
         ssd1306_DrawPixel(127, 23, White);
         ssd1306_DrawPixel(127, 24, White);
         ssd1306_DrawPixel(127, 25, White);
         ssd1306_DrawPixel(127, 26, White);
         ssd1306_DrawPixel(127, 27, White);
         ssd1306_DrawPixel(127, 28, White);
         ssd1306_DrawPixel(127, 29, White);

         /* Umplem bara cu pixeli albi pana la latime_bara */
         for (uint32_t x = 2; x < latime_bara + 2; x++)
         {
             for (uint8_t y = 23; y <= 29; y++)
             {
                 ssd1306_DrawPixel((uint8_t)x, y, White);
             }
         }

         /* Linia 3: Directia motorului */
         ssd1306_SetCursor(0, 36);
         ssd1306_WriteString(text_directie, Font_7x10, White);

         /* Linia 4: Valoarea ADC bruta */
         ssd1306_SetCursor(0, 52);
         sprintf(buffer_display, "ADC:%4lu PWM:%4lu", valoare_adc_bruta, valoare_pwm);
         ssd1306_WriteString(buffer_display, Font_6x8, White);

         /* Trimitem tot buffer-ul la display prin I2C */
         ssd1306_UpdateScreen();

         HAL_Delay(100);

     }

}

void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK)
  {
    Error_Handler();
  }

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 1;
  RCC_OscInitStruct.PLL.PLLN = 10;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV7;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}

static void MX_ADC1_Init(void)
{
  ADC_MultiModeTypeDef multimode = {0};
  ADC_ChannelConfTypeDef sConfig = {0};

  hadc1.Instance = ADC1;
  hadc1.Init.ClockPrescaler = ADC_CLOCK_ASYNC_DIV1;
  hadc1.Init.Resolution = ADC_RESOLUTION_12B;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
  hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  hadc1.Init.LowPowerAutoWait = DISABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.NbrOfConversion = 1;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc1.Init.DMAContinuousRequests = DISABLE;
  hadc1.Init.Overrun = ADC_OVR_DATA_PRESERVED;
  hadc1.Init.OversamplingMode = DISABLE;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  multimode.Mode = ADC_MODE_INDEPENDENT;
  if (HAL_ADCEx_MultiModeConfigChannel(&hadc1, &multimode) != HAL_OK)
  {
    Error_Handler();
  }

  sConfig.Channel = ADC_CHANNEL_5;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_2CYCLES_5;
  sConfig.SingleDiff = ADC_SINGLE_ENDED;
  sConfig.OffsetNumber = ADC_OFFSET_NONE;
  sConfig.Offset = 0;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

}

static void MX_I2C1_Init(void)
{
  hi2c1.Instance = I2C1;
  hi2c1.Init.Timing = 0x10D19CE4;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }

  if (HAL_I2CEx_ConfigAnalogFilter(&hi2c1, I2C_ANALOGFILTER_ENABLE) != HAL_OK)
  {
    Error_Handler();
  }

  if (HAL_I2CEx_ConfigDigitalFilter(&hi2c1, 0) != HAL_OK)
  {
    Error_Handler();
  }

}

static void MX_TIM1_Init(void)
{

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};
  TIM_BreakDeadTimeConfigTypeDef sBreakDeadTimeConfig = {0};

  htim1.Instance = TIM1;
  htim1.Init.Prescaler = 79;
  htim1.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim1.Init.Period = 999;
  htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim1.Init.RepetitionCounter = 0;
  htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim1) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim1, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_Init(&htim1) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterOutputTrigger2 = TIM_TRGO2_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim1, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCNPolarity = TIM_OCNPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  sConfigOC.OCIdleState = TIM_OCIDLESTATE_RESET;
  sConfigOC.OCNIdleState = TIM_OCNIDLESTATE_RESET;
  if (HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  sBreakDeadTimeConfig.OffStateRunMode = TIM_OSSR_DISABLE;
  sBreakDeadTimeConfig.OffStateIDLEMode = TIM_OSSI_DISABLE;
  sBreakDeadTimeConfig.LockLevel = TIM_LOCKLEVEL_OFF;
  sBreakDeadTimeConfig.DeadTime = 0;
  sBreakDeadTimeConfig.BreakState = TIM_BREAK_DISABLE;
  sBreakDeadTimeConfig.BreakPolarity = TIM_BREAKPOLARITY_HIGH;
  sBreakDeadTimeConfig.BreakFilter = 0;
  sBreakDeadTimeConfig.Break2State = TIM_BREAK2_DISABLE;
  sBreakDeadTimeConfig.Break2Polarity = TIM_BREAK2POLARITY_HIGH;
  sBreakDeadTimeConfig.Break2Filter = 0;
  sBreakDeadTimeConfig.AutomaticOutput = TIM_AUTOMATICOUTPUT_DISABLE;
  if (HAL_TIMEx_ConfigBreakDeadTime(&htim1, &sBreakDeadTimeConfig) != HAL_OK)
  {
    Error_Handler();
  }

  HAL_TIM_MspPostInit(&htim1);

}

static void MX_USART2_UART_Init(void)
{

  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  huart2.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }

}

static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  HAL_GPIO_WritePin(GPIOA, LD2_Pin|GPIO_PIN_9|GPIO_PIN_10, GPIO_PIN_RESET);

  GPIO_InitStruct.Pin = B1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(B1_GPIO_Port, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = LD2_Pin|GPIO_PIN_9|GPIO_PIN_10;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

}

void Error_Handler(void)
{
  __disable_irq();
  while (1)
  {
  }

}
#ifdef USE_FULL_ASSERT

void assert_failed(uint8_t *file, uint32_t line)
{

}
#endif
