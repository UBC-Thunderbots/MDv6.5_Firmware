//for CAN communication interface


#include "interface.h"
#include <stdbool.h>

extern FDCAN_HandleTypeDef hfdcan1;


//initialize the communicaiton interface
void Interface_Init(void){

	//configure a filter
	//create a filter instance
	FDCAN_FilterTypeDef canFilter;
	canFilter.IdType = FDCAN_STANDARD_ID; //use standard message id protocll (11bits)
	canFilter.FilterIndex = 0;
	canFilter.FilterType = FDCAN_FILTER_DUAL;		//only accept exact message ids. not bit masking
	canFilter.FilterConfig = FDCAN_FILTER_TO_RXFIFO0;//put messafges into fifo0
	canFilter.FilterID1 = CAN_RX_ID;//asks for 2 arguements, whatever.
	canFilter.FilterID2 = CAN_RX_ID;
	//pass into filter configuring function
	HAL_FDCAN_ConfigFilter(&hfdcan1, &canFilter);

	//tells hardawre what to do with rejected can messages.
	//ignore everything not meant for this board.
	HAL_FDCAN_ConfigGlobalFilter(&hfdcan1, FDCAN_REJECT, FDCAN_REJECT, FDCAN_REJECT_REMOTE, FDCAN_REJECT_REMOTE);

	//start the CAN peripherla
	HAL_FDCAN_Start(&hfdcan1);


}


//polling goes here
void Interface_Poll(void){

	FDCAN_RxHeaderTypeDef rxHeader;  //create a header instance for message
	uint8_t rxData[MSG_DATA_LENGTH]; //array for processing the data portion of the can mesaages

	//chekc recevie buffer. if greater than 1, than means message avaiable
	if (HAL_FDCAN_GetRxFifoFillLevel(&hfdcan1, FDCAN_RX_FIFO0) > 0) {
		//retreive from fifo0
		HAL_FDCAN_GetRxMessage(&hfdcan1, FDCAN_RX_FIFO0, &rxHeader, rxData);

		// toggle LED on PC13 to confirm reception
		HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);

		//ECHO TEST!
		FDCAN_TxHeaderTypeDef txHeader;
	    txHeader.Identifier = CAN_TX_ID;
	    txHeader.IdType = FDCAN_STANDARD_ID;
	    txHeader.TxFrameType = FDCAN_DATA_FRAME;
	    txHeader.DataLength = FDCAN_DLC_BYTES_8;
	    txHeader.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
	    txHeader.BitRateSwitch = FDCAN_BRS_OFF;
	    txHeader.FDFormat = FDCAN_CLASSIC_CAN;
	    txHeader.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
	    txHeader.MessageMarker = 0;

	    // send same data back
	    HAL_FDCAN_AddMessageToTxFifoQ(&hfdcan1, &txHeader, rxData);


	}




}
