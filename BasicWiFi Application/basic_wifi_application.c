/*****************************************************************************
 *
 *  basic_wifi_application.c - CC3000 Slim Driver Implementation.
 *  Copyright (C) 2011 Texas Instruments Incorporated - http://www.ti.com/
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *    Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 *    Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the
 *    distribution.
 *
 *    Neither the name of Texas Instruments Incorporated nor the names of
 *    its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *****************************************************************************/

#include "wlan.h" 
#include "evnt_handler.h"    
#include "nvmem.h"
#include "socket.h"
#include "netapp.h"
#include "spi.h"

#include "dispatcher.h"
#include "spi_version.h"
#include "board.h"
#include "application_version.h"
#include "host_driver_version.h"

#include <msp430.h>
#include <stdbool.h>
#include "coap.h"
//#include "security.h"

#define PALTFORM_VERSION                                        (2)

extern int uart_have_cmd;

#define UART_COMMAND_CC3000_SIMPLE_CONFIG_START                 (0x31)

#define UART_COMMAND_CC3000_CONNECT                             (0x32)

#define UART_COMMAND_SOCKET_OPEN                                (0x33)

#define UART_COMMAND_SEND_DATA                                  (0x34)

#define UART_COMMAND_RCV_DATA                                   (0x35)

#define UART_COMMAND_BSD_BIND                                   (0x36)

#define UART_COMMAND_SOCKET_CLOSE                               (0x37)

#define UART_COMMAND_IP_CONFIG                                  (0x38)

#define UART_COMMAND_CC3000_DISCONNECT                          (0x39)

#define UART_COMMAND_CC3000_DEL_POLICY                          (0x61)

#define UART_COMMAND_SEND_DNS_ADVERTIZE                         (0x62)

#define CC3000_APP_BUFFER_SIZE                                  (8)
#define CC3000_RX_BUFFER_OVERHEAD_SIZE			        (20)

#define DISABLE                                                 (0)
#define ENABLE                                                  (1)

#define SL_VERSION_LENGTH                                       (11)

#define NETAPP_IPCONFIG_MAC_OFFSET				(20)

volatile bool ulCC3000Connected, ulCC3000DHCP, OkToDoShutDown,
		ulCC3000DHCP_configured;

volatile long ulSocket;

// Indications that UART command has finished etc
const unsigned char pucUARTCommandDoneString[] = { '\n', '\r', 'D', 'O', 'N',
		'E', '\n', '\r' };
const unsigned char pucUARTExampleAppString[] = { '\n', '\r', 'E', 'x', 'a',
		'm', 'p', 'l', 'e', ' ', 'A', 'p', 'p', ' ' };
const unsigned char pucUARTNoDataString[] = { '\n', '\r', 'N', 'o', ' ', 'd',
		'a', 't', 'a', ' ', 'r', 'e', 'c', 'e', 'i', 'v', 'e', 'd', '\n', '\r' };
const unsigned char pucUARTIllegalCommandString[] = { '\n', '\r', 'I', 'l', 'l',
		'e', 'g', 'a', 'l', ' ', 'c', 'o', 'm', 'm', 'a', 'n', 'd', '\n', '\r' };

#ifndef CC3000_UNENCRYPTED_SMART_CONFIG
//AES key "smartconfigAES16"
const unsigned char smartconfigkey[] = {0x73,0x6d,0x61,0x72,0x74,0x63,0x6f,0x6e,0x66,0x69,0x67,0x41,0x45,0x53,0x31,0x36};
#endif

unsigned char printOnce = 1;

const char digits[] = "0123456789";
/////////////////////////////////////////////////////////////////////////////////////////////////////////////    
//__no_init is used to prevent the buffer initialization in order to prevent hardware WDT expiration    ///
// before entering to 'main()'.                                                                         ///
//for every IDE, different syntax exists :          1.   __CCS__ for CCS v5                             ///
//                                                  2.  __IAR_SYSTEMS_ICC__ for IAR Embedded Workbench  ///
// *CCS does not initialize variables - therefore, __no_init is not needed.                             ///
///////////////////////////////////////////////////////////////////////////////////////////////////////////
// Reception from the air, buffer - the max data length  + headers
//
#ifdef __CCS__

unsigned char pucCC3000_Rx_Buffer[CC3000_APP_BUFFER_SIZE
		+ CC3000_RX_BUFFER_OVERHEAD_SIZE];

#elif __IAR_SYSTEMS_ICC__

__no_init unsigned char pucCC3000_Rx_Buffer[CC3000_APP_BUFFER_SIZE + CC3000_RX_BUFFER_OVERHEAD_SIZE];

#endif

//*****************************************************************************
//
//! itoa
//!
//! @param[in]  integer number to convert
//!
//! @param[in/out]  output string
//!
//! @return number of ASCII parameters
//!
//! @brief  Convert integer to ASCII in decimal base
//
//*****************************************************************************
unsigned short itoa(char cNum, char *cString) {
	char* ptr;
	char uTemp = cNum;
	unsigned short length;

	// value 0 is a special case
	if (cNum == 0) {
		length = 1;
		*cString = '0';

		return length;
	}

	// Find out the length of the number, in decimal base
	length = 0;
	while (uTemp > 0) {
		uTemp /= 10;
		length++;
	}

	// Do the actual formatting, right to left
	uTemp = cNum;
	ptr = cString + length;
	while (uTemp > 0) {
		--ptr;
		*ptr = digits[uTemp % 10];
		uTemp /= 10;
	}

	return length;
}
//*****************************************************************************
//
//! atoc
//!
//! @param  none
//!
//! @return none
//!
//! @brief  Convert nibble to hexdecimal from ASCII
//
//*****************************************************************************
unsigned char atoc(char data) {
	unsigned char ucRes;

	if ((data >= 0x30) && (data <= 0x39)) {
		ucRes = data - 0x30;
	} else {
		if (data == 'a') {
			ucRes = 0x0a;
			;
		} else if (data == 'b') {
			ucRes = 0x0b;
		} else if (data == 'c') {
			ucRes = 0x0c;
		} else if (data == 'd') {
			ucRes = 0x0d;
		} else if (data == 'e') {
			ucRes = 0x0e;
		} else if (data == 'f') {
			ucRes = 0x0f;
		}
	}

	return ucRes;
}

//*****************************************************************************
//
//! atoshort
//!
//! @param  b1 first nibble
//! @param  b2 second nibble
//!
//! @return short number
//!
//! @brief  Convert 2 nibbles in ASCII into a short number
//
//*****************************************************************************

unsigned short atoshort(char b1, char b2) {
	unsigned short usRes;

	usRes = (atoc(b1)) * 16 | atoc(b2);

	return usRes;
}

//*****************************************************************************
//
//! ascii_to_char
//!
//! @param  b1 first byte
//! @param  b2 second byte
//!
//! @return The converted character
//!
//! @brief  Convert 2 bytes in ASCII into one character
//
//*****************************************************************************

unsigned char ascii_to_char(char b1, char b2) {
	unsigned char ucRes;

	ucRes = (atoc(b1)) << 4 | (atoc(b2));

	return ucRes;
}

//*****************************************************************************
//
//! sendDriverPatch
//!
//! @param  Length   pointer to the length
//!
//! @return none
//!
//! @brief  The function returns a pointer to the driver patch: since there is  
//!				  no patch (patches are taken from the EEPROM and not from the host
//!         - it returns NULL
//
//*****************************************************************************
char *sendDriverPatch(unsigned long *Length) {
	*Length = 0;
	return NULL;
}

//*****************************************************************************
//
//! sendBootLoaderPatch
//!
//! @param  pointer to the length
//!
//! @return none
//!
//! @brief  The function returns a pointer to the bootloader patch: since there   
//!				  is no patch (patches are taken from the EEPROM and not from the host
//!         - it returns NULL
//
//*****************************************************************************
char *sendBootLoaderPatch(unsigned long *Length) {
	*Length = 0;
	return NULL;
}

//*****************************************************************************
//
//! sendWLFWPatch
//!
//! @param  pointer to the length
//!
//! @return none
//!
//! @brief  The function returns a pointer to the driver patch: since there is  
//!				  no patch (patches are taken from the EEPROM and not from the host
//!         - it returns NULL
//
//*****************************************************************************

char *sendWLFWPatch(unsigned long *Length) {
	*Length = 0;
	return NULL;
}

char *sendNullPatch(unsigned long *Length) {
	*Length = 0;
	return NULL;
}

//*****************************************************************************
//
//! CC3000_UsynchCallback
//!
//! @param  lEventType   Event type
//! @param  data   
//! @param  length   
//!
//! @return none
//!
//! @brief  The function handles asynchronous events that come from CC3000  
//!		      device and operates a LED1 to have an on-board indication
//
//*****************************************************************************

void CC3000_UsynchCallback(long lEventType, char * data, unsigned char length) {

	if (lEventType == HCI_EVNT_WLAN_UNSOL_CONNECT) {
		ulCC3000Connected = 1;

		// Turn on the LED7
		turnLedOn(7);
	}

	if (lEventType == HCI_EVNT_WLAN_UNSOL_DISCONNECT) {
		ulCC3000Connected = 0;
		ulCC3000DHCP = 0;
		ulCC3000DHCP_configured = 0;
		printOnce = 1;

		// Turn off the LED7
		turnLedOff(7);

		// Turn off LED5
		turnLedOff(8);
	}

	if (lEventType == HCI_EVNT_WLAN_UNSOL_DHCP) {
		// Notes: 
		// 1) IP config parameters are received swapped
		// 2) IP config parameters are valid only if status is OK, i.e. ulCC3000DHCP becomes 1

		// only if status is OK, the flag is set to 1 and the addresses are valid
		if (*(data + NETAPP_IPCONFIG_MAC_OFFSET) == 0) {
			sprintf((char*) pucCC3000_Rx_Buffer, "IP:%d.%d.%d.%d\n\r", data[3],
					data[2], data[1], data[0]);

//			int i, ptr = 3;
//			strcpy(pucCC3000_Rx_Buffer, "IP:");
//			i = itoa(data[3], &pucCC3000_Rx_Buffer[ptr]);
//			ptr += i;
//			pucCC3000_Rx_Buffer[ptr++] = '.';
//			i = itoa(data[2], &pucCC3000_Rx_Buffer[ptr]);
//			ptr += i;
//			pucCC3000_Rx_Buffer[ptr++] = '.';
//			i = itoa(data[1], &pucCC3000_Rx_Buffer[ptr]);
//			ptr += i;
//			pucCC3000_Rx_Buffer[ptr++] = '.';
//			i = itoa(data[0], &pucCC3000_Rx_Buffer[ptr]);
//			strcpy(pucCC3000_Rx_Buffer + ptr + i, "\n\r");

			ulCC3000DHCP = 1;

			turnLedOn(8);
		} else {
			ulCC3000DHCP = 0;

			turnLedOff(8);
		}
	}

	if (lEventType == HCI_EVENT_CC3000_CAN_SHUT_DOWN) {
		OkToDoShutDown = 1;
	}

}

//*****************************************************************************
//
//! initDriver
//!
//!  @param  None
//!
//!  @return none
//!
//!  @brief  The function initializes a CC3000 device and triggers it to 
//!          start operation
//
//*****************************************************************************
int initDriver(void) {

	// Init GPIO's
	pio_init();

	//Init Spi
	init_spi();

	DispatcherUARTConfigure();

	// WLAN On API Implementation sendNullPatch
//	wlan_init( CC3000_UsynchCallback, sendWLFWPatch, sendDriverPatch, sendBootLoaderPatch, ReadWlanInterruptPin, WlanInterruptEnable, WlanInterruptDisable, WriteWlanPin);
	wlan_init(CC3000_UsynchCallback, sendNullPatch, sendNullPatch,
			sendNullPatch, ReadWlanInterruptPin, WlanInterruptEnable,
			WlanInterruptDisable, WriteWlanPin);

	// Trigger a WLAN device
	wlan_start(0);

	// Turn on the LED 5 to indicate that we are active and initiated WLAN successfully
	turnLedOn(5);

	// Mask out all non-required events from CC3000
	wlan_set_event_mask(
			HCI_EVNT_WLAN_KEEPALIVE | HCI_EVNT_WLAN_UNSOL_INIT
					| HCI_EVNT_WLAN_ASYNC_PING_REPORT);

	// Generate the event to CLI: send a version string
	{
		char cc3000IP[50];
		char *ccPtr;
		unsigned short ccLen;

		DispatcherUartSendPacket((unsigned char*) pucUARTExampleAppString,
				sizeof(pucUARTExampleAppString));

		ccPtr = &cc3000IP[0];
		ccLen = itoa(PALTFORM_VERSION, ccPtr);
		ccPtr += ccLen;
		*ccPtr++ = '.';
		ccLen = itoa(APPLICATION_VERSION, ccPtr);
		ccPtr += ccLen;
		*ccPtr++ = '.';
		ccLen = itoa(SPI_VERSION_NUMBER, ccPtr);
		ccPtr += ccLen;
		*ccPtr++ = '.';
		ccLen = itoa(DRIVER_VERSION_NUMBER, ccPtr);
		ccPtr += ccLen;
		*ccPtr++ = '\n';
		*ccPtr++ = '\r';
		*ccPtr++ = '\0';

		DispatcherUartSendPacket((unsigned char*) cc3000IP, strlen(cc3000IP));
	}

	wakeup_timer_init();

	return (0);
}

//*****************************************************************************
//
//! DemoHandleUartCommand
//!
//!  @param  usBuffer
//!
//!  @return none
//!
//!  @brief  The function handles commands arrived from CLI
//
//*****************************************************************************
sockaddr tSocketAddr;
char *rsp;

static void serverListen(uint8_t *buf) {
	INT16 iReturnValue;
	socklen_t tRxPacketLength;
	uint8_t buflen = 0;

	// Open socket
	if (ulSocket == -1) {
		ulSocket = socket((INT32) AF_INET, (INT32) SOCK_DGRAM,
				(INT32) IPPROTO_UDP);

		// bind to port
		tSocketAddr.sa_family = AF_INET;
		// the source port
		tSocketAddr.sa_data[0] = 22; // translates to 5683 from sendto example
		tSocketAddr.sa_data[1] = 51;
		// all 0 IP address
		memset(&tSocketAddr.sa_data[2], 0, 4);
		bind(ulSocket, &tSocketAddr, sizeof(sockaddr));
	}

	fd_set readSet;
	struct timeval tv;
	/* Wait up to five seconds. */
	tv.tv_sec = 5;
	tv.tv_usec = 0;
	do {
		FD_ZERO(&readSet);
		FD_SET(ulSocket, &readSet);
		iReturnValue = select(ulSocket + 1, &readSet, NULL,
		NULL, &tv);

		// perform read on read socket if data available
		if (FD_ISSET(ulSocket, &readSet)) {
			// perform receive
			if (buflen == 0)
				// Only first read gives correct socket address.
				// Buffer length of 10 doesn't work
				iReturnValue = recvfrom(ulSocket, pucCC3000_Rx_Buffer + buflen,
						5, 0, &tSocketAddr, &tRxPacketLength);
			else
				iReturnValue = recvfrom(ulSocket, pucCC3000_Rx_Buffer + buflen,
						5, 0, NULL, NULL);
			//iReturnValue might be -1
			if (iReturnValue > 0)
				buflen += iReturnValue;

		}
	} while (iReturnValue > 0);

	// perform send on the write socket if it is ready to receive next chunk of data
	if (buflen > 0) {
		DispatcherUartSendPacket(pucCC3000_Rx_Buffer, buflen);
		{
			coap_packet_t in;
			coap_parse(&in, pucCC3000_Rx_Buffer, buflen);

//			in.hdr.code = COAP_RSPCODE_CONTENT;
//			in.numopts = 1;
//			in.opts[0].num = COAP_OPTION_URI_PATH;
//			in.opts[0].buf.p = "light";
//			in.opts[0].buf.len = 5;
//			in.payload.len = 0;

			rsp = g_ucUARTBuffer;
			memset(rsp, 0, UART_IF_BUFFER);
			coap_handle_req(&in, &in);

			buflen = 0;
			buf = pucCC3000_Rx_Buffer;
			coap_build(buf, &buflen, &in);
		}
		if (buflen > 0) {
			sendto(ulSocket, buf, buflen, 0, &tSocketAddr, sizeof(sockaddr));
		}

	} else {
		// No data received by device
		DispatcherUartSendPacket((unsigned char*) pucUARTNoDataString,
				sizeof(pucUARTNoDataString));
		closesocket(ulSocket);
		ulSocket = -1;
	}
}

void DemoHandleUartCommand(unsigned char *usBuffer) {
	char *pcSsid;
//	, *pcData, *pcSockAddrAscii;
	UINT8 *key;
	unsigned long ulSsidLen;
//	, ulDataLength;
	volatile signed long iReturnValue;
//	sockaddr tSocketAddr;
//	socklen_t tRxPacketLength;
//	unsigned char pucIP_Addr[4];
//	unsigned char pucIP_DefaultGWAddr[4];
//	unsigned char pucSubnetMask[4];
//	unsigned char pucDNS[4];

	// usBuffer[0] contains always 0
	// usBuffer[1] maps the command
	// usBuffer[2..end] optional parameters
	switch (usBuffer[1]) {
	// Start a WLAN Connect process
	case UART_COMMAND_CC3000_CONNECT: {
		ulSsidLen = atoc(usBuffer[2]);
		pcSsid = (char *) &usBuffer[3];

		INT32 key_len = ulSsidLen + 3;
		UINT32 ulSecType = atoc(usBuffer[key_len]);
		key = 0;
		if (key_len != 0) {
			key_len = atoc(usBuffer[key_len + 1]);
			key = (UINT8 *) &usBuffer[key_len + 2];
		}
		//wlan_connect(WLAN_SEC_UNSEC, pcSsid, ulSsidLen,NULL, NULL, 0);
		// default ulSecType=0, key_len=0
		wlan_connect1(ulSecType, pcSsid, ulSsidLen, NULL, key, key_len);

		uint32_t pairwisecipher_or_keylen = 0;
		uint32_t groupcipher_or_keyindex = 0;
		uint32_t key_mgmt = 0;
		if ((WLAN_SEC_WPA == ulSecType) || (WLAN_SEC_WPA2 == ulSecType)) {
			pairwisecipher_or_keylen = WPA_CIPHER_TKIP | WPA_CIPHER_CCMP;
			groupcipher_or_keyindex = WPA_CIPHER_WEP40 | WPA_CIPHER_WEP104
					| WPA_CIPHER_TKIP | WPA_CIPHER_CCMP;
			key_mgmt = WPA_DRIVER_CAPA_KEY_MGMT_WPA2;
		} else if (WLAN_SEC_WEP == ulSecType) {
			pairwisecipher_or_keylen = key_len;
			groupcipher_or_keyindex = 0;
			key_mgmt = 0;
		} else if (WLAN_SEC_UNSEC == ulSecType) {
			pairwisecipher_or_keylen = 0;
			groupcipher_or_keyindex = 0;
			key_mgmt = 0;
		}
		long rc = wlan_add_profile(ulSecType, (uint8_t*) pcSsid, ulSsidLen,
		NULL, /* bssid is inferred */
		1, pairwisecipher_or_keylen, groupcipher_or_keyindex, key_mgmt, key,
				key_len);
		wlan_ioctl_set_connection_policy(DISABLE, DISABLE, ENABLE);
	}
		break;

		// Handle open socket command
//	case UART_COMMAND_SOCKET_OPEN:
//		//wait for DHCP process to finish. if you are using a static IP address
//		//please delete the wait for DHCP event - ulCC3000DHCP
//		while ((ulCC3000DHCP == 0) || (ulCC3000Connected == 0)) {
//			__delay_cycles(1000);
//		}
//		ulSocket = socket((INT32) AF_INET, (INT32) SOCK_DGRAM,
//				(INT32) IPPROTO_UDP);
//		break;
//
//		// Handle close socket command
//	case UART_COMMAND_SOCKET_CLOSE:
//		closesocket(ulSocket);
//		ulSocket = 0xFFFFFFFF;
//		break;
//
//		// Handle receive data command
//	case UART_COMMAND_RCV_DATA:
//		iReturnValue = recvfrom(ulSocket, pucCC3000_Rx_Buffer,
//				CC3000_APP_BUFFER_SIZE, 0, &tSocketAddr, &tRxPacketLength);
//		if (iReturnValue <= 0) {
//			// No data received by device
//			DispatcherUartSendPacket((unsigned char*) pucUARTNoDataString,
//					sizeof(pucUARTNoDataString));
//		} else {
//			// Send data to UART...
//			//DispatcherUartSendPacket(pucCC3000_Rx_Buffer, CC3000_APP_BUFFER_SIZE);
//			DispatcherUartSendPacket(pucCC3000_Rx_Buffer, iReturnValue);
//		}
//		break;
//
//		// Handle send data command
//	case UART_COMMAND_SEND_DATA:
//		ulSocket = socket((INT32) AF_INET, (INT32) SOCK_DGRAM,
//				(INT32) IPPROTO_UDP);
//		// data pointer
//		pcData = (char *) &usBuffer[4];
//
//		// data length to send
//		ulDataLength = atoshort(usBuffer[2], usBuffer[3]);
//
//#ifdef CC3000_TINY_DRIVER
//		if(ulDataLength > CC3000_APP_BUFFER_SIZE)
//		{
//			ulDataLength = CC3000_APP_BUFFER_SIZE;
//		}
//#endif
//
//		pcSockAddrAscii = (pcData + ulDataLength);
//
//		// the family is always AF_INET
//		tSocketAddr.sa_family = AF_INET;//atoshort(pcSockAddrAscii[0], pcSockAddrAscii[1]);
//
//		// the destination port
//		tSocketAddr.sa_data[0] = ascii_to_char(pcSockAddrAscii[0],
//				pcSockAddrAscii[1]);
//		tSocketAddr.sa_data[1] = ascii_to_char(pcSockAddrAscii[2],
//				pcSockAddrAscii[3]);
//
//		// the destination IP address
//		tSocketAddr.sa_data[2] = ascii_to_char(pcSockAddrAscii[4],
//				pcSockAddrAscii[5]);
//		tSocketAddr.sa_data[3] = ascii_to_char(pcSockAddrAscii[6],
//				pcSockAddrAscii[7]);
//		tSocketAddr.sa_data[4] = ascii_to_char(pcSockAddrAscii[8],
//				pcSockAddrAscii[9]);
//		tSocketAddr.sa_data[5] = ascii_to_char(pcSockAddrAscii[10],
//				pcSockAddrAscii[11]);
//
//		sendto(ulSocket, pcData, ulDataLength, 0, &tSocketAddr,
//				sizeof(sockaddr));
//		break;
//
//		// Handle bind command
//	case UART_COMMAND_BSD_BIND:
//		tSocketAddr.sa_family = AF_INET;
//
//		// the source port
//		tSocketAddr.sa_data[0] = ascii_to_char(usBuffer[2], usBuffer[3]);
//		tSocketAddr.sa_data[1] = ascii_to_char(usBuffer[4], usBuffer[5]);
//
//		// all 0 IP address
//		memset(&tSocketAddr.sa_data[2], 0, 4);
//
//		bind(ulSocket, &tSocketAddr, sizeof(sockaddr));
//
//		break;

		// Handle IP configuration command
	case UART_COMMAND_IP_CONFIG:
		// Open socket
//		serverInit(ulSocket, iReturnValue, pucUARTNoDataString,
//				pucCC3000_Rx_Buffer, tRxPacketLength, buflen, buf,
//				&tSocketAddr);
		break;

		// Handle WLAN disconnect command
	case UART_COMMAND_CC3000_DISCONNECT:
		// This section works for sending CoAP packet
//		wlan_disconnect();
//		tSocketAddr.sa_family = AF_INET;
//
//		// the destination port
//		tSocketAddr.sa_data[0] = 22; // translates to 5683 from sendto example
//		tSocketAddr.sa_data[1] = 51;
//
//		// the destination IP address
//		tSocketAddr.sa_data[2] = 192;
//		tSocketAddr.sa_data[3] = 168;
//		tSocketAddr.sa_data[4] = 1;
//		tSocketAddr.sa_data[5] = 10;
//
//		uint8_t buf[25];
//		coap_packet_t in;
//		size_t buflen;
//		in.hdr.ver = COAP_VERSION;
//		in.hdr.t = COAP_TYPE_NON;
//		in.hdr.tkl = 0;
//		in.hdr.code = COAP_METHOD_GET;
//		in.numopts = 1;
//		in.opts[0].num = COAP_OPTION_URI_PATH;
//		in.opts[0].buf.p = "light";
//		in.opts[0].buf.len = 5;
//		in.payload.len = 0;
//
//		coap_build(buf, &buflen, &in);
//
//		ulSocket = socket((INT32) AF_INET, (INT32) SOCK_DGRAM,
//				(INT32) IPPROTO_UDP);
//		sendto(ulSocket, buf, buflen, 0, &tSocketAddr, sizeof(sockaddr));
		break;

	default:
		DispatcherUartSendPacket((unsigned char*) pucUARTIllegalCommandString,
				sizeof(pucUARTIllegalCommandString));
		break;

	}

	// Send a response - the command handling has finished
	DispatcherUartSendPacket((unsigned char *) (pucUARTCommandDoneString),
			sizeof(pucUARTCommandDoneString));
}

//*****************************************************************************
//
//! main
//!
//!  @param  None
//!
//!  @return none
//!
//!  @brief  The main loop is executed here
//
//*****************************************************************************

main(void) {
	ulCC3000DHCP = 0;
	ulCC3000Connected = 0;
	ulSocket = -1;

	WDTCTL = WDTPW + WDTHOLD;

	//  Board Initialization start
	initDriver();

	// Initialize the UART RX Buffer   
	memset(g_ucUARTBuffer, 0xFF, UART_IF_BUFFER);
	uart_have_cmd = 0;

//	nvmem_read_sp_version(pucCC3000_Rx_Buffer);
//	DispatcherUartSendPacket((unsigned char*)pucCC3000_Rx_Buffer, strlen((char const*)pucCC3000_Rx_Buffer));

	// Loop forever waiting  for commands from PC...
	while (1) {
		__bis_SR_register(LPM2_bits + GIE);
		__no_operation();

		if (uart_have_cmd == 1) {
			wakeup_timer_disable();
			//Process the cmd in RX buffer
			DemoHandleUartCommand(g_ucUARTBuffer);
			uart_have_cmd = 0;
			memset(g_ucUARTBuffer, 0xFF, UART_IF_BUFFER);
			wakeup_timer_init();
		}

		if ((ulCC3000DHCP == 1) && (ulCC3000Connected == 1)) {
			if (printOnce == 1) {
				printOnce = 0;
				DispatcherUartSendPacket((unsigned char*) pucCC3000_Rx_Buffer,
						strlen((char const*) pucCC3000_Rx_Buffer));
			}
			wakeup_timer_disable();
			serverListen(g_ucUARTBuffer);
			wakeup_timer_init();
		}

	}
}

