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
void ProcessRx_ClearFaults(void);\t\t\t\t\t //acknowledge/clear motor faults
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
			case CLEAR_FAULTS:
				ProcessRx_ClearFaults();
				break;
			default:
				break;
		}

		//now repond..
		configureResponse();


	}
}

