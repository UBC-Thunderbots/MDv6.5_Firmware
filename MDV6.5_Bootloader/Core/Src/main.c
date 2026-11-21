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
#include <string.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
typedef void (*app_entry_t)(void);
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define APP_BASE_ADDRESS	0x08008000
#define MAX_APP_SIZE		(96 * 1024)
#define CAN_ID_BOOT			0x900
#define CAN_ID_START		0x901
#define CAN_ID_DATA			0x902
#define CAN_ID_END			0x903
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
CRC_HandleTypeDef hcrc;

FDCAN_HandleTypeDef hfdcan1;

/* USER CODE BEGIN PV */
uint32_t writeAddress;
uint32_t totalSize;
uint32_t bytesReceived;
uint32_t currentCRC;
uint8_t transferActive = 0;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_FDCAN1_Init(void);
static void MX_CRC_Init(void);
/* USER CODE BEGIN PFP */
void JumpToApplication(void);
void TransferFailed(void);
uint8_t AppIsValid(void);
HAL_StatusTypeDef FlashErase(void);
HAL_StatusTypeDef FlashWrite(uint32_t address, uint8_t *data, uint32_t length);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

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
	MX_FDCAN1_Init();
	MX_CRC_Init();
	/* USER CODE BEGIN 2 */

	// LED on PC13
	GPIO_InitTypeDef GPIO_InitStruct = {0};
	__HAL_RCC_GPIOC_CLK_ENABLE();
	GPIO_InitStruct.Pin = GPIO_PIN_13;
	GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

	// Blink LED 3 times to indicate bootloader mode
	for (int i = 0; i < 6; i++)
	{
		HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
		HAL_Delay(150);
	}
	HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);

	// CAN filters for bootloader IDs
	FDCAN_FilterTypeDef sFilterConfig;
	sFilterConfig.IdType = FDCAN_STANDARD_ID;
	sFilterConfig.FilterType = FDCAN_FILTER_MASK;
	sFilterConfig.FilterConfig = FDCAN_FILTER_TO_RXFIFO0;
	sFilterConfig.FilterID2 = 0x7FF;

	sFilterConfig.FilterIndex = 0;
	sFilterConfig.FilterID1 = CAN_ID_BOOT;
	HAL_FDCAN_ConfigFilter(&hfdcan1, &sFilterConfig);

	sFilterConfig.FilterIndex = 1;
	sFilterConfig.FilterID1 = CAN_ID_START;
	HAL_FDCAN_ConfigFilter(&hfdcan1, &sFilterConfig);

	sFilterConfig.FilterIndex = 2;
	sFilterConfig.FilterID1 = CAN_ID_DATA;
	HAL_FDCAN_ConfigFilter(&hfdcan1, &sFilterConfig);

	sFilterConfig.FilterIndex = 3;
	sFilterConfig.FilterID1 = CAN_ID_END;
	HAL_FDCAN_ConfigFilter(&hfdcan1, &sFilterConfig);

	HAL_FDCAN_ConfigGlobalFilter(&hfdcan1, FDCAN_REJECT, FDCAN_REJECT,
			FDCAN_FILTER_REMOTE, FDCAN_FILTER_REMOTE);

	if (HAL_FDCAN_Start(&hfdcan1) != HAL_OK) {
		Error_Handler();
	}
	/* USER CODE END 2 */

	/* Infinite loop */
	/* USER CODE BEGIN WHILE */
	while (1)
	{
		/* USER CODE END WHILE */
		if (HAL_FDCAN_GetRxFifoFillLevel(&hfdcan1, FDCAN_RX_FIFO0) > 0)
		{
			FDCAN_RxHeaderTypeDef RxHeader;
			uint8_t RxData[8];

			if (HAL_FDCAN_GetRxMessage(&hfdcan1, FDCAN_RX_FIFO0, &RxHeader, RxData) == HAL_OK)
			{
				switch (RxHeader.Identifier)
				{
					case CAN_ID_BOOT:
						HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
						if (AppIsValid()) JumpToApplication();
						break;

					case CAN_ID_START:
						HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
						memcpy(&totalSize, RxData, 4);
						if (totalSize > MAX_APP_SIZE || totalSize % 8 != 0) { TransferFailed(); break; }
						if (FlashErase() != HAL_OK) { TransferFailed(); break; }
						HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);
						currentCRC = 0;
						__HAL_CRC_DR_RESET(&hcrc);
						writeAddress = APP_BASE_ADDRESS;
						bytesReceived = 0;
						transferActive = 1;
						break;

					case CAN_ID_DATA:
						if (!transferActive) break;
						if (bytesReceived + 8 > totalSize) { TransferFailed(); break; }
						if (FlashWrite(writeAddress, RxData, 8) != HAL_OK) { TransferFailed(); break; }
						currentCRC = HAL_CRC_Accumulate(&hcrc, (uint32_t*)RxData, 8);
						writeAddress += 8;
						bytesReceived += 8;
						break;

					case CAN_ID_END:
					{
						uint32_t receivedCRC;
						memcpy(&receivedCRC, RxData, 4);
						if (!transferActive) break;
						if (bytesReceived != totalSize) { TransferFailed(); break; }
						//if (currentCRC == receivedCRC)
							JumpToApplication();
						//else
						//	TransferFailed();
						break;
					}
				}
			}
		}
		/* USER CODE BEGIN 3 */
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

	HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1_BOOST);

	RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
	RCC_OscInitStruct.HSEState = RCC_HSE_ON;
	RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
	RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
	RCC_OscInitStruct.PLL.PLLM = RCC_PLLM_DIV2;
	RCC_OscInitStruct.PLL.PLLN = 85;
	RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
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

/**
  * @brief CRC Initialization Function
  * @param None
  * @retval None
  */
static void MX_CRC_Init(void)
{
	/* USER CODE BEGIN CRC_Init 0 */

	/* USER CODE END CRC_Init 0 */

	/* USER CODE BEGIN CRC_Init 1 */

	/* USER CODE END CRC_Init 1 */
	hcrc.Instance = CRC;
	hcrc.Init.DefaultPolynomialUse = DEFAULT_POLYNOMIAL_ENABLE;
	hcrc.Init.DefaultInitValueUse = DEFAULT_INIT_VALUE_ENABLE;
	hcrc.Init.InputDataInversionMode = CRC_INPUTDATA_INVERSION_NONE;
	hcrc.Init.OutputDataInversionMode = CRC_OUTPUTDATA_INVERSION_DISABLE;
	hcrc.InputDataFormat = CRC_INPUTDATA_FORMAT_BYTES;
	if (HAL_CRC_Init(&hcrc) != HAL_OK)
	{
		Error_Handler();
	}
	/* USER CODE BEGIN CRC_Init 2 */

	/* USER CODE END CRC_Init 2 */
}

/**
  * @brief FDCAN1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_FDCAN1_Init(void)
{
	/* USER CODE BEGIN FDCAN1_Init 0 */

	/* USER CODE END FDCAN1_Init 0 */

	/* USER CODE BEGIN FDCAN1_Init 1 */

	/* USER CODE END FDCAN1_Init 1 */
	hfdcan1.Instance = FDCAN1;
	hfdcan1.Init.ClockDivider = FDCAN_CLOCK_DIV1;
	hfdcan1.Init.FrameFormat = FDCAN_FRAME_CLASSIC;
	hfdcan1.Init.Mode = FDCAN_MODE_NORMAL;
	hfdcan1.Init.AutoRetransmission = ENABLE;
	hfdcan1.Init.TransmitPause = DISABLE;
	hfdcan1.Init.ProtocolException = DISABLE;
	hfdcan1.Init.NominalPrescaler = 10;
	hfdcan1.Init.NominalSyncJumpWidth = 4;
	hfdcan1.Init.NominalTimeSeg1 = 12;
	hfdcan1.Init.NominalTimeSeg2 = 4;
	hfdcan1.Init.DataPrescaler = 1;
	hfdcan1.Init.DataSyncJumpWidth = 1;
	hfdcan1.Init.DataTimeSeg1 = 1;
	hfdcan1.Init.DataTimeSeg2 = 1;
	hfdcan1.Init.StdFiltersNbr = 4;
	hfdcan1.Init.ExtFiltersNbr = 0;
	hfdcan1.Init.TxFifoQueueMode = FDCAN_TX_FIFO_OPERATION;
	if (HAL_FDCAN_Init(&hfdcan1) != HAL_OK)
	{
		Error_Handler();
	}
	/* USER CODE BEGIN FDCAN1_Init 2 */

	/* USER CODE END FDCAN1_Init 2 */
}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
	/* USER CODE BEGIN MX_GPIO_Init_1 */

	/* USER CODE END MX_GPIO_Init_1 */

	/* GPIO Ports Clock Enable */
	__HAL_RCC_GPIOF_CLK_ENABLE();
	__HAL_RCC_GPIOA_CLK_ENABLE();

	/* USER CODE BEGIN MX_GPIO_Init_2 */

	/* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
uint8_t AppIsValid(void)
{
	uint32_t sp = *((volatile uint32_t*)APP_BASE_ADDRESS);
	return (sp >= 0x20000000 && sp <= 0x20008000);
}

void TransferFailed(void)
{
	transferActive = 0;
	for (int i = 0; i < 10; i++)
	{
		HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
		HAL_Delay(100);
	}
	HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);
}

void JumpToApplication(void)
{
	uint32_t app_stack_pointer = *((volatile uint32_t*)APP_BASE_ADDRESS);
	uint32_t app_reset_handler = *((volatile uint32_t*)(APP_BASE_ADDRESS + 4));
	app_entry_t app_entry = (app_entry_t)app_reset_handler;
	__disable_irq();
	SCB->VTOR = APP_BASE_ADDRESS;
	__set_MSP(app_stack_pointer);
	__enable_irq();
	app_entry();
}

HAL_StatusTypeDef FlashErase(void)
{
	HAL_FLASH_Unlock();
	FLASH_EraseInitTypeDef eraseConfig;
	eraseConfig.TypeErase = FLASH_TYPEERASE_PAGES;
	eraseConfig.Banks = FLASH_BANK_1;
	eraseConfig.Page = 16;		// page 16 = 0x08008000
	eraseConfig.NbPages = 48;	// pages 16-63 = 96 KB
	uint32_t pageError;
	HAL_StatusTypeDef status = HAL_FLASHEx_Erase(&eraseConfig, &pageError);
	HAL_FLASH_Lock();
	return status;
}

HAL_StatusTypeDef FlashWrite(uint32_t address, uint8_t *data, uint32_t length)
{
	HAL_FLASH_Unlock();
	HAL_StatusTypeDef status = HAL_OK;
	for (uint32_t i = 0; i < length; i += 8)
	{
		uint64_t chunk;
		memcpy(&chunk, data + i, 8);
		status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, address + i, chunk);
		if (status != HAL_OK) break;
	}
	HAL_FLASH_Lock();
	return status;
}
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
	/* USER CODE BEGIN Error_Handler_Debug */
	__disable_irq();
	while (1)
	{
	}
	/* USER CODE END Error_Handler_Debug */
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
	/* USER CODE BEGIN 6 */
	/* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
