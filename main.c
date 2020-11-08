/* Firm_Keychecker main.c 
 * 鍵の情報を送信する(基本的に子機)
 */

#include <AppHardwareApi.h>		// NXPペリフェラルAPI用
#include <sprintf.h>
#include "utils.h"				// ペリフェラルAPIのラッパなど
#include "ToCoNet.h"
#include "ToCoNet_mod_prototype.h"
#include "sprintf.h"
#include "serial.h"
#include "ToConet_event.h"

#define DIO5 5
#define DIO17 17
#define DIO16 16

/**************************************/
/* ToCoNet Definitions                */
/**************************************/
#define APP_ID 0x01010101
#define CHANNEL 18

#define UART_BAUD 115200

/**************************************/
/* Global Variable                    */
/**************************************/
typedef enum{
	E_STATE_APP_BASE = ToCoNet_STATE_APP_BASE,
	E_EVENT_APP_TX_COMPLETE
} teStateApp;
static uint32 u32Seq;

static tsFILE sSerStream;		// シリアル用ストリーム
static tsSerialPortSetup sSerPort;	// シリアルポート用ディスクリプタ

// デバッグメッセージ出力用
#define DBG
#ifdef DBG
#define dbg(...) vfPrintf(&sSerStream, LB __VA_ARGS__)
#else
#define dbg(...)
#endif

//#define MASTER
//#define MONOSTICK

// デバッグ用にUARTを初期化
static void vSerialInit(){
	static uint8 au8SerialTxBuffer[96];
	static uint8 au8SerialRxBuffer[32];

	sSerPort.pu8SerialRxQueueBuffer = au8SerialRxBuffer;
	sSerPort.pu8SerialTxQueueBuffer = au8SerialTxBuffer;
	sSerPort.u32BaudRate = UART_BAUD;
	sSerPort.u16AHI_UART_RTS_LOW = 0xffff;
	sSerPort.u16AHI_UART_RTS_HIGH = 0xffff;
	sSerPort.u16SerialRxQueueSize = sizeof(au8SerialRxBuffer);
	sSerPort.u16SerialTxQueueSize = sizeof(au8SerialTxBuffer);
	sSerPort.u8SerialPort = E_AHI_UART_0;
	sSerPort.u8RX_FIFO_LEVEL = E_AHI_UART_FIFO_LEVEL_1;
	SERIAL_vInit(&sSerPort);

	sSerStream.bPutChar = SERIAL_bTxChar;
	sSerStream.u8Device = E_AHI_UART_0;
}

// Hardware Initialization
static void vInitHardware(int f_warm_start)
{
	vSerialInit();
	ToCoNet_vDebugInit(&sSerStream);
	ToCoNet_vDebugLevel(0);

	#ifndef MONOSTICK
	vPortAsOutput(DIO5);
	vPortAsInput(DIO16);
	vPortAsInput(DIO17);
	#endif
}

// Transmit
static bool_t sendParent()
{
	tsTxDataApp sTx;
	memset(&sTx, 0, sizeof(sTx));		// ゼロクリア
	uint8 *q = sTx.auData;

	// ペイロードを構成
	if(bPortRead(DIO16)){
		S_OCTET('X');
	}else{
		S_OCTET('Y');
	}

	// 送信準備
	sTx.u8Len = q - sTx.auData;		// パケット長さ
	sTx.u8Cmd = 1;					// パケット種別

	// 送信する
	sTx.u32DstAddr = 0;				// 親機に送信
	sTx.u8Retry = 0x00;				// 再送しない
	sTx.u32SrcAddr = sToCoNet_AppContext.u16ShortAddress;

	// フレームカントとコールバック識別子の指定
	sTx.u8Seq = u32Seq;
	sTx.u8CbId = sTx.u8Seq;
	u32Seq++;

	return ToCoNet_bMacTxReq(&sTx);
}

// ユーザ定義のイベントハンドラ
static void vProcessEvCore(tsEvent *pEv, teEvent eEvent, uint32 u32evarg)
{
	// 1秒周期のシステムタイマ通知
	if (eEvent == E_EVENT_START_UP){
		#ifdef MASTER
		dbg("Key Checker Master Startup...");
		#else
		dbg("Key Checker Slave Startup...");
		#endif
	}else if (eEvent == E_EVENT_TICK_SECOND){
		// DO1 の Lo Hi をトグル
		#ifndef MONOSTICK
		bPortRead(DIO5) ? vPortSetHi(DIO5) : vPortSetLo(DIO5);
		#endif

		#ifndef MASTER
		sendParent();
		dbg("Transmit to Master");
		#endif
	}else if (eEvent == E_EVENT_APP_TX_COMPLETE){
		ToCoNet_vSleep(E_AHI_WAKE_TIMER_0, 10000, FALSE, FALSE);
	}
}

// 以下、ToCoNet既定のイベントハンドラ群

// 割り込み発生後に随時呼び出される
void cbToCoNet_vMain(void)
{
	return;
}

// パケット受信時
void cbToCoNet_vRxEvent(tsRxDataApp *pRx)
{
	#ifdef MASTER
	static uint32 u32SrcAddrPrev = 0;
	static uint8 u8seqPrev = 0xff;

	// 受信出来たらデバッグ出力する
	char buf[64];
	int len = (pRx->u8Len < sizeof(buf)) ? pRx->u8Len : sizeof(buf)-1;
	memcpy(buf, pRx->auData, len);
	buf[len] = '\0';
	dbg("RECV << [%s] from %d", buf, pRx->u32SrcAddr);
	#endif

	return;
}

// パケット送信完了時
void cbToCoNet_vTxEvent(uint8 u8CbId, uint8 bStatus)
{
	dbg(">> SEND %s seq=%u", bStatus ? "OK" : "NG", u32Seq);
	ToCoNet_Event_Process(E_EVENT_APP_TX_COMPLETE, u8CbId, vProcessEvCore);
	return;
}

// ネットワークイベント発生時
void cbToCoNet_vNwkEvent(teEvent eEvent, uint32 u32arg)
{
	return;
}

// ハードウェア割り込み発生後(遅延呼び出し)
void cbToCoNet_vHwEvent(uint32 u32DeviceId, uint32 u32ItemBitmap)
{
	return;
}

// ハードウェア割り込み発生時
uint8 cbToCoNet_u8HwInt(uint32 u32DeviceId, uint32 u32ItemBitmap)
{
	return FALSE;
}

// コールドスタート時
void cbAppColdStart(bool_t bAfterAhiInit)
{
	if (!bAfterAhiInit){

		// Register modules
		ToCoNet_REG_MOD_ALL();
	}else{
		// disable brown out detect
		vAHI_BrownOutConfigure(0,
			FALSE,
			FALSE,
			FALSE,
			FALSE);
		
		// ToCoNet configuration
		sToCoNet_AppContext.u32AppId = APP_ID;
		sToCoNet_AppContext.u8Channel = CHANNEL;
		sToCoNet_AppContext.u8TxMacRetry = 0;		// no retry
		sToCoNet_AppContext.bRxOnIdle = TRUE;		// 最終的にはFALSEにするが、まずは動作確認のためTRUEにする
		u32Seq = 0;
		#ifdef MASTER
		sToCoNet_AppContext.u16ShortAddress = 0;	// 親機はショートアドレスは0
		#endif

		// sprintfの初期化(128バイトの領域確保と初期化)
		SPRINTF_vInit128();
		
		// Hardware initialize
		vInitHardware(FALSE);

		// ユーザ定義のイベントハンドラを登録
		ToCoNet_Event_Register_State_Machine(vProcessEvCore);

		// MAC層開始
		ToCoNet_vMacStart();
	}
}

// ウォームスタート
void cbAppWarmStart(bool_t bAfterAhiInit)
{
	if(!bAfterAhiInit){
		// before AHI init, very first of code

	}else{		
		// disable brown out detect
		vAHI_BrownOutConfigure(0,
			FALSE,
			FALSE,
			FALSE,
			FALSE);

		// Initialize Hardware
		vInitHardware(TRUE);

		// MAC Start
		ToCoNet_vMacStart();
	}

	return;
}
