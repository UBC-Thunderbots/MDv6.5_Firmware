//for CAN communication interface


#include "interface.h"
#include <stdbool.h>
#include <string.h> //for memset

extern FDCAN_HandleTypeDef hfdcan1; //external
extern PID_Handle_t PIDIqHandle_M1;   // torque
extern PID_Handle_t PIDIdHandle_M1;   // flux
extern PID_Handle_t PIDSpeedHandle_M1; // speed

//setting state variables
static ResponseType_t response_type = SPEED_AND_FAULTS; //look in interface.h
static bool motor_enabled = false; 						//initial motor state!

//function prototypes
void setMotorEnabled(bool enabled);
void ProcessRx_SetTargetSpeed(const uint8_t *rx);    //set target speed for motor
void ProcessRx_SetTargetTorque(const uint8_t *rx);	 //set target torque for motor
void ProcessRx_SetResponseType(const uint8_t *rx);	 //set response type of MD for subsequent transfers to master
void ProcessRx_SetPidTorqueKpKi(const uint8_t *rx);  //set kp and ki for TORQUE PID controller
void ProcessRx_SetPidFluxKpKi(const uint8_t *rx);	 //set kp and ki for FLUX PID controller
void ProcessRx_SetPidSpeedKpKi(const uint8_t *rx);   //set kp and ki for SPEED PID controller
void sendResponse(const uint8_t *txData);		 //send response back accorcing to response_type
void configureResponse();



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
	//FDCAN_TxHeaderTypeDef txHeader;
	uint8_t rxData[MSG_DATA_LENGTH]; //array for processing the data portion of the can mesaages


	
	//chekc recevie buffer. if greater than 1, than means message avaiable
	if (HAL_FDCAN_GetRxFifoFillLevel(&hfdcan1, FDCAN_RX_FIFO0) > 0) {
		//retreive from fifo0
		HAL_FDCAN_GetRxMessage(&hfdcan1, FDCAN_RX_FIFO0, &rxHeader, rxData);

		//the first byte of the received payload (8 bytes total) is the opcode of message. the CAN message header is just ID, which determindes which board receives what message.
		//process the message based on OPCODE (do what OPCODE tells us to)
		switch((Opcode_t)rxData[0]){
			case NO_OP:
				break;
			case SET_TARGET_SPEED:
				ProcessRx_SetTargetSpeed(rxData);
				break;
			case SET_TARGET_TORQUE:
				ProcessRx_SetTargetTorque(rxData);
				break;
			case SET_RESPONSE_TYPE:
				ProcessRx_SetResponseType(rxData);
				break;
			case SET_PID_TORQUE_KP_KI:
				ProcessRx_SetPidTorqueKpKi(rxData);
				break;
			case SET_PID_FLUX_KP_KI:
				ProcessRx_SetPidFluxKpKi(rxData);
				break;
			case SET_PID_SPEED_KP_KI:
				ProcessRx_SetPidSpeedKpKi(rxData);
				break;
			default:
				break;
		}

		//now repond..
		configureResponse();


	}
}

//enabled or disable motor
void setMotorEnabled(bool enabled){
	//change motor state based on curent state and input.
	if(enabled && !motor_enabled){
		MC_StartMotor1();
	}else if (!enabled && motor_enabled){
		MC_StopMotor1();
	}
	//update motor state
	motor_enabled = enabled;
}

//set target speed for motor
//pass in pointer to array: beginning address
void ProcessRx_SetTargetSpeed(const uint8_t *rx){

	//WE CAN ENABLE/DISABLED MOTOR, and SET SPEED WITH THIS COMMAND
	const int16_t motor_target_speed = (int16_t)(((uint16_t)rx[2] << 8) | rx[3]);  //bit shift byte 2 (left 8) and OR with byte 3 to create a 16 bit integer. search it up if you want to know how this works
	MC_ProgramSpeedRampMotor1_F((float)motor_target_speed, 0U); //set the motor target speed according to above number
 	setMotorEnabled(rx[1] != 0); //enabled or disabled motor based on first byte.

}

//set target torque for motor
//pass in pointer to array: beginnig address of rxData
//BASICALLY THE SAME AT THE set target speed function.
void ProcessRx_SetTargetTorque(const uint8_t *rx){
	const int16_t motor_target_torque = (int16_t)(((uint16_t)rx[2] << 8) | rx[3]);
	MC_ProgramTorqueRampMotor1_F((float)motor_target_torque, 0U);
	setMotorEnabled(rx[1] != 0);
}

//sets reponse type 
void ProcessRx_SetResponseType(const uint8_t *rx)
{
  response_type = (ResponseType_t)rx[1];
}

//set kp ki for TORQUE PID CONTROLLER
void ProcessRx_SetPidTorqueKpKi(const uint8_t *rx)
{
  const int16_t kp = (int16_t)(((uint16_t)rx[1] << 8) | rx[2]);
  const int16_t ki = (int16_t)(((uint16_t)rx[3] << 8) | rx[4]);

  PID_SetKP(&PIDIqHandle_M1, kp);
  PID_SetKI(&PIDIqHandle_M1, ki);
}

//set kp ki for FLUX PID CONTROLLER
void ProcessRx_SetPidFluxKpKi(const uint8_t *rx)
{
  const int16_t kp = (int16_t)(((uint16_t)rx[1] << 8) | rx[2]);
  const int16_t ki = (int16_t)(((uint16_t)rx[3] << 8) | rx[4]);

  PID_SetKP(&PIDIdHandle_M1, kp);
  PID_SetKI(&PIDIdHandle_M1, ki);
}

//set kp ki for SPEED PID CONTROLLER
void ProcessRx_SetPidSpeedKpKi(const uint8_t *rx)
{
  const int16_t kp = (int16_t)(((uint16_t)rx[1] << 8) | rx[2]);
  const int16_t ki = (int16_t)(((uint16_t)rx[3] << 8) | rx[4]);

  PID_SetKP(&PIDSpeedHandle_M1, kp);
  PID_SetKI(&PIDSpeedHandle_M1, ki);
}

//sends response acording to the response_type ! needs to be loaded with the txData payload..
void sendResponse(const uint8_t *txData) {
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

	HAL_FDCAN_AddMessageToTxFifoQ(&hfdcan1, &txHeader, (uint8_t *)txData);
  }

//configure the response message
void configureResponse(void){
	uint8_t txData[MSG_DATA_LENGTH];
	memset(txData, 0, MSG_DATA_LENGTH);
	txData[0] = (uint8_t)response_type;

	switch (response_type) {
      	case SPEED_AND_FAULTS: {
    		int16_t speed = MC_GetMecSpeedAverageMotor1();
			int16_t faults = MC_GetOccurredFaultsMotor1();
			txData[1] = (uint8_t)((speed >> 8) & 0xFF);
			txData[2] = (uint8_t)(speed & 0xFF);
			txData[3] = (uint8_t)((faults >> 8) & 0xFF);
			txData[4] = (uint8_t)(faults & 0xFF);
			break;
      	}
      	default:
        	break;
 	}

	//now send reponse:
	sendResponse(txData);

	
}