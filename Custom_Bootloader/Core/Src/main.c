/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

#include <stdint.h>

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define APP_MAX_SIZE    0xE000U
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */

#define FW1_ADDRESS  0x08004000U
#define FW1_END      0x08012000U

#define FW2_ADDRESS  0x08012000U
#define FW2_END      0x08020000U

#define BKP_ACTIVE   BKP->DR1      // backup register   data register 1 (remembers which firmware ran last)

#define REQUEST_FW1  1U     // firmware 1 value,  user defined unique values
#define REQUEST_FW2  2U           // firmware 2 value  ,

#define BUTTON_PRESSED   GPIO_PIN_RESET

uint8_t receivebyte;             // in main,   for u and v

uint8_t rxBuffer[128];                 // in function ReceiveAndProgramFirmware    , for firmware

uint32_t firmwareSize;

volatile uint8_t rxDone = 0;             // flag             new. interrupt sets this to 1 when the requested bytes have arrived
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART2_UART_Init(void);
/* USER CODE BEGIN PFP */

HAL_StatusTypeDef EraseApplicationFlash(uint32_t startAddress, uint32_t firmwareSize);
HAL_StatusTypeDef ReceiveAndProgramFirmware( uint32_t startAddress, uint32_t firmwareSize);
HAL_StatusTypeDef VerifyFirmwareCRC(uint32_t firmwareSize, uint32_t expectedCRC);      // new. checks crc of the incoming firmware
HAL_StatusTypeDef UART_Receive_IT_Wait(uint8_t *buf, uint16_t len, uint32_t timeout);   // new. replacement of HAL_UART_Receive
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)   // new. called by Hal from the interrupt , call back function
{
	if (huart->Instance == USART2)
	    {
	        rxDone = 1;
	    }
}

// new. Replacement for HAL_UART_Receive, the interrupt receives the bytes, we only wait for the flag or timeout
HAL_StatusTypeDef UART_Receive_IT_Wait(uint8_t *buf, uint16_t len, uint32_t timeout)           // returns a HAL status
{
    rxDone = 0;
    HAL_UART_Receive_IT(&huart2, buf, len);          // start interrupt receive

    uint32_t start = HAL_GetTick();                // Stores the current system tick count
    while (rxDone == 0)
    {
        if (HAL_GetTick() - start >= timeout)                 // Checks whether the timeout has expired
        {
            HAL_UART_AbortReceive(&huart2);          // stop the receive on timeout
            return HAL_TIMEOUT;
        }
    }
    return HAL_OK;
}

void BackupRegister_Init(void)                  // Turns on the power and backup clocks and allows access to the backup registers
{
    __HAL_RCC_PWR_CLK_ENABLE();      //   Power clock on karta hai
    __HAL_RCC_BKP_CLK_ENABLE();        //   Backup clock on  karta hai

    HAL_PWR_EnableBkUpAccess();            // Backup access enable karna
}

void JumpToApplication(uint32_t appAddress, uint32_t appEnd)
{
    uint32_t appStack = *(__IO uint32_t *)appAddress;   // initial stack address read karta
    uint32_t appResetHandler = *(__IO uint32_t *)(appAddress + 4);   // Read  the rest Handler address


    if (appStack < 0x20000000U || appStack > 0x20005000U ||                            // validate the application addresses
        appResetHandler < appAddress ||  appResetHandler >= appEnd)                     // 20 kb sram
    {
        return;
    }

    __disable_irq();    // Disables CPU interrupts globally

    HAL_NVIC_DisableIRQ(USART2_IRQn);    // new. otherwise a pending UART interrupt would hit the application's vector table
    HAL_UART_DeInit(&huart2);            // new. switch UART off completely (also clears the receive interrupt enable)

    HAL_RCC_DeInit();    // clock configuration ko default state mein laata hai ,Reset and Clock Control
    HAL_DeInit();        // HAL peripherals ko deinitialize karta hai, both used for hardware configuration clean


    SCB->VTOR = appAddress;  // Application ka vector table address set karta hai

    __DSB();               // pehle ke memory operations complete karwata hai   , data synchronization barrier
    __ISB();               // processor ki instruction synchronize karta hai   , interrupt synchronization barrier

    __set_MSP(appStack);   // Application ka initial stack pointer set karta hai

    void (*AppResetHandler)(void) = (void (*)(void))appResetHandler;  //creates a function pointer that points to the application reset handler
    __enable_irq();        // interrupts enable karta hai

    AppResetHandler();     // Application ka Reset Handler execute karta hai
}




void SelectAndJump(void)                         // Reads DR1 saves the other firmware’s value, and jumps to FW1 or FW2
{
    uint16_t last = (uint16_t)BKP_ACTIVE;              // which firmware ran last

    if (last == REQUEST_FW1)
    {
        BKP_ACTIVE = REQUEST_FW2;                      //  FW2 is now active
        JumpToApplication(FW2_ADDRESS, FW2_END);
    }
    else
    {
        BKP_ACTIVE = REQUEST_FW1;                      // first time or after FW2 then FW1
        JumpToApplication(FW1_ADDRESS, FW1_END);
    }
}

HAL_StatusTypeDef ReceiveFirmwareSize(uint32_t *size, uint32_t *expectedCRC)  // Python se UART ke through firmware ka size receive karta hai
{
    uint8_t sizeBytes[4];
    uint8_t crcBytes[4];

    // receive firmware size
    if (UART_Receive_IT_Wait(sizeBytes, 4, 5000) != HAL_OK)       // new. was HAL_UART_Receive(&huart2, sizeBytes, 4, 5000)
    {
        return HAL_ERROR;
    }

    *size = (uint32_t)sizeBytes[0]
          | ((uint32_t)sizeBytes[1] << 8)
          | ((uint32_t)sizeBytes[2] << 16)
          | ((uint32_t)sizeBytes[3] << 24);

    if (*size == 0 || *size > APP_MAX_SIZE)
    {
        return HAL_ERROR;
    }


    if (UART_Receive_IT_Wait(crcBytes, 4, 5000) != HAL_OK)             // new. was HAL_UART_Receive(&huart2, crcBytes, 4, 5000)
    {
        return HAL_ERROR;
    }

    *expectedCRC = (uint32_t)crcBytes[0]
                 | ((uint32_t)crcBytes[1] << 8)
                 | ((uint32_t)crcBytes[2] << 16)
                 | ((uint32_t)crcBytes[3] << 24);

    return HAL_OK;
}


HAL_StatusTypeDef EraseApplicationFlash(uint32_t startAddress, uint32_t firmwareSize)
{
    FLASH_EraseInitTypeDef eraseInit;     //Erase structure
    uint32_t pageError;

    uint32_t numberOfPages = (firmwareSize + FLASH_PAGE_SIZE - 1) / FLASH_PAGE_SIZE;

    HAL_FLASH_Unlock();

    eraseInit.TypeErase = FLASH_TYPEERASE_PAGES;    // to erase pages
    eraseInit.PageAddress = startAddress;           // tells from where to erase,  means starting address (FW1 or FW2)
    eraseInit.NbPages = numberOfPages;    // how many pages erase

    HAL_StatusTypeDef status = HAL_FLASHEx_Erase(&eraseInit, &pageError);              //  eraseee

    HAL_FLASH_Lock();

    return status;
}

uint32_t CRC32_Update(uint32_t crc, const uint8_t *data, uint32_t length)

{
    while (length--)
    {
        crc ^= *data++;

        for (uint8_t i = 0; i < 8; i++)
        {
            if (crc & 1U)        // Check karta hai ke last bit 1 hai ya 0
            {
                crc = (crc >> 1) ^ 0xEDB88320U;     //xor
            }
            else
            {
                crc >>= 1;
            }
        }
    }

    return crc;
}

uint32_t CalculateFlashCRC(uint32_t address, uint32_t size)
{
    uint32_t crc = 0xFFFFFFFFU;                       // start value
    const uint8_t *flashData = (const uint8_t *)address;
    crc = CRC32_Update(crc, flashData, size);     // final crc of the firmware
    crc ^= 0xFFFFFFFFU;               // final
    return crc;
}


// NEW FUNCTION  (PASS 1)

 // If the CRC matches, only then the old firmware is erased (PASS 2).

HAL_StatusTypeDef VerifyFirmwareCRC(uint32_t firmwareSize, uint32_t expectedCRC)
{
    uint32_t crc = 0xFFFFFFFFU;
    uint32_t received = 0;

    while (received < firmwareSize)
    {
        uint16_t bytes =(firmwareSize - received > 128) ? 128 : (firmwareSize - received);

        // Receive firmware chunk from Python
        if (UART_Receive_IT_Wait(rxBuffer, bytes, 5000) != HAL_OK)       // new.  was HAL_UART_Receive(&huart2, rxBuffer, bytes, 5000)
        {
            return HAL_ERROR;
        }

        // Only calculate CRC, do not write to Flash
        crc = CRC32_Update(crc, rxBuffer, bytes);

        // Tell Python that this chunk was received
        uint8_t ack = 0x06;

        if (HAL_UART_Transmit(&huart2, &ack, 1, 1000) != HAL_OK)
        {
            return HAL_ERROR;
        }

        received += bytes;
    }

    crc ^= 0xFFFFFFFFU;

    // Compare Python CRC with the CRC of the data that really arrived
    if (crc != expectedCRC)
    {
        return HAL_ERROR;
    }

    return HAL_OK;
}


HAL_StatusTypeDef ReceiveAndProgramFirmware( uint32_t startAddress, uint32_t firmwareSize)
{
    uint32_t address = startAddress;
    uint32_t received = 0;

    HAL_StatusTypeDef status = HAL_OK;

    HAL_FLASH_Unlock();

    while (received < firmwareSize)
    {
        uint16_t bytes =
            (firmwareSize - received > 128) ? 128 : (firmwareSize - received);

        // Receive firmware chunk from Python
        if (UART_Receive_IT_Wait(rxBuffer, bytes, 5000) != HAL_OK)       // new. was HAL_UART_Receive(&huart2, rxBuffer, bytes, 5000)
        {
            status = HAL_ERROR;
            break;
        }

        // Program received bytes into Flash, 2 bytes at a time
        for (uint16_t i = 0; i < bytes; i += 2)
        {
            uint16_t data = rxBuffer[i];

            if (i + 1 < bytes)
            {
                data |= (uint16_t)rxBuffer[i + 1] << 8;
            }
            else
            {
                data |= 0xFF00;
            }

            if (HAL_FLASH_Program( FLASH_TYPEPROGRAM_HALFWORD, address,  data) != HAL_OK)
            {
                status = HAL_ERROR;
                break;
            }

            address += 2;
        }

        if (status != HAL_OK)
        {
            break;
        }

        // Tell Python that this chunk was programmed
        uint8_t ack = 0x06;

        if (HAL_UART_Transmit(&huart2, &ack, 1, 1000) != HAL_OK)
        {
            status = HAL_ERROR;
            break;
        }

        received += bytes;
    }

    HAL_FLASH_Lock();

    return status;
}



void UpdateFirmware(uint32_t startAddr)
{
    uint8_t ack = 0x06;
    uint8_t nack = 0x15;

    uint32_t expectedCRC = 0;
    uint32_t actualCRC = 0;

    if (ReceiveFirmwareSize(&firmwareSize, &expectedCRC) != HAL_OK)               // Receive firmware size and expected CRC from Python

    {
        HAL_UART_Transmit(&huart2, &nack, 1, 1000);
        return;
    }

    //  Tell Python size and CRC are received, now send the firmware for checking (PASS 1)
    HAL_UART_Transmit(&huart2, &ack, 1, 1000);

    //  PASS 1, check CRC of the incoming data BEFORE erasing anything.
    // If this fails we return here, so the old firmware in Flash is still safe
    if (VerifyFirmwareCRC(firmwareSize, expectedCRC) != HAL_OK)
    {
        HAL_UART_Transmit(&huart2, &nack, 1, 1000);
        return;
    }

    HAL_UART_Transmit(&huart2, &ack, 1, 1000);            //  CRC is good, tell Python

    // Erase selected Flash area (now it is safe, the new firmware is known to be good)
    if (EraseApplicationFlash(startAddr, firmwareSize) != HAL_OK)
    {
        HAL_UART_Transmit(&huart2, &nack, 1, 1000);
        return;
    }

    // Tell Python Flash is ready, send the firmware again (PASS 2)
    HAL_UART_Transmit(&huart2, &ack, 1, 1000);

    // Receive and program firmware
    if (ReceiveAndProgramFirmware(startAddr, firmwareSize) != HAL_OK)
    {
        HAL_UART_Transmit(&huart2, &nack, 1, 1000);
        return;
    }

    //  Calculate CRC by reading programmed Flash
    actualCRC = CalculateFlashCRC(startAddr, firmwareSize);

    // Compare Python CRC with Flash CRC
    if (actualCRC == expectedCRC)
    {
        HAL_UART_Transmit(&huart2, &ack, 1, 1000);

        // Remember which firmware was successfully updated
        if (startAddr == FW1_ADDRESS)
        {
            BKP_ACTIVE = REQUEST_FW1;
        }
        else if (startAddr == FW2_ADDRESS)
        {
            BKP_ACTIVE = REQUEST_FW2;
        }
    }
    else
    {
        HAL_UART_Transmit(&huart2, &nack, 1, 1000);
    }
}



/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */


  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USART2_UART_Init();
  /* USER CODE BEGIN 2 */

  BackupRegister_Init();                              // must be called before touching BKP

  // If the button is still pressed right after reset (firmware reset us because of the press), load the other firmware now
  if (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_13) == BUTTON_PRESSED)
  {
      SelectAndJump();
  }

  HAL_UART_Receive_IT(&huart2, &receivebyte, 1);       // new. start listening for the command byte

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */

	      if (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_13) == BUTTON_PRESSED)
	      {
	          HAL_Delay(30);                                                    // debounce

	          if (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_13) == BUTTON_PRESSED)
	          {
	              SelectAndJump();                                              // jump to firmware 1 or firmware 2

	              // reached only if that firmware slot is empty: wait for button release
	              while (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_13) == BUTTON_PRESSED)
	              {
	                  HAL_Delay(10);
	              }
	          }
	      }



	      //  Python command: 'U' = update FW1, 'V' = update FW2
	      if (rxDone)                                      // new. command byte arrived by interrupt (was HAL_UART_Receive polling)
	      {
	          rxDone = 0;

	          if (receivebyte == 'U')
	          {
	              UpdateFirmware(FW1_ADDRESS);
	          }
	          else if (receivebyte == 'V')
	          {
	              UpdateFirmware(FW2_ADDRESS);
	          }

	          rxDone = 0;                                  // new. UpdateFirmware leaves rxDone = 1, clear it so it is not taken as a new command
	          HAL_UART_Receive_IT(&huart2, &receivebyte, 1);   // new. listen for the next command
	      }



  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_RESET);

  /*Configure GPIO pin : PC13 */
  GPIO_InitStruct.Pin = GPIO_PIN_13;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pin : PA5 */
  GPIO_InitStruct.Pin = GPIO_PIN_5;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
