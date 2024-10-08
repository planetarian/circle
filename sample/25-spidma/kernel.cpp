//
// kernel.cpp
//
// Circle - A C++ bare metal environment for Raspberry Pi
// Copyright (C) 2016-2020  R. Stange <rsta2@o2online.de>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
#include "kernel.h"
#include <circle/synchronize.h>
#include <circle/debug.h>
#include <assert.h>
#include <string.h>

#define SERIAL_BAUD     3000000
#define SPI_CLOCK_SPEED	16000000		// Hz
#define SPI_CPOL		0
#define SPI_CPHA		0

#define SPI_CHIP_SELECT		0		// 0 or 1

#define TEST_DATA_LENGTH	2000		// number of data bytes transfered

static const char FromKernel[] = "kernel";

CKernel::CKernel (void)
:	m_Screen (m_Options.GetWidth (), m_Options.GetHeight ()),
	m_Timer (&m_Interrupt),
	m_Logger (m_Options.GetLogLevel (), &m_Timer),
	m_SPIMaster (&m_Interrupt, SPI_CLOCK_SPEED, SPI_CPOL, SPI_CPHA)
{
	m_ActLED.Blink (5);	// show we are alive
}

CKernel::~CKernel (void)
{
}

boolean CKernel::Initialize (void)
{
	boolean bOK = TRUE;

	if (bOK)
	{
		bOK = m_Screen.Initialize ();
	}

	if (bOK)
	{
		bOK = m_Serial.Initialize (SERIAL_BAUD);
	}

	if (bOK)
	{
		CDevice *pTarget = m_DeviceNameService.GetDevice (m_Options.GetLogDevice (), FALSE);
		if (pTarget == 0)
		{
			pTarget = &m_Screen;
		}

		bOK = m_Logger.Initialize (pTarget);
	}

	if (bOK)
	{
		bOK = m_Interrupt.Initialize ();
	}

	if (bOK)
	{
		bOK = m_Timer.Initialize ();
	}

	if (bOK)
	{
		bOK = m_SPIMaster.Initialize ();
	}

	return bOK;
}

TShutdownMode CKernel::Run (void)
{
	m_Logger.Write (FromKernel, LogNotice, "Compile time: " __DATE__ " " __TIME__);

	while (true) {

		m_Timer.MsDelay(1000);
		char rebootMagic[] = "tAgHQP3Lw2NZcW8Uru7jnf";
		char serialBuf[100];
		u32 serialBytes = m_Serial.Read(serialBuf, sizeof serialBuf);
		if (serialBytes > 0) {
			m_Logger.Write (FromKernel, LogNotice, "Data received");
			m_Logger.Write (FromKernel, LogNotice, serialBuf);
			if (strcmp(rebootMagic, serialBuf) == 0)
				break;
			m_Timer.MsDelay(4000);
		}

		m_Logger.Write (FromKernel, LogNotice, "Transfering %u bytes over SPI0", TEST_DATA_LENGTH);

		// prepare some data
		assert (TEST_DATA_LENGTH <= 50000);	// will not fit on the stack otherwise
		DMA_BUFFER (u8, TxData, TEST_DATA_LENGTH);
		DMA_BUFFER (u8, RxBuffer, TEST_DATA_LENGTH);
		for (unsigned i = 0; i < TEST_DATA_LENGTH; i++)
		{
			TxData[i] = (u8) i;
			RxBuffer[i] = 0x55;		// will be overwritten
		}

		// start the SPI transfer
		m_bTransferRunning = TRUE;

		m_SPIMaster.SetCompletionRoutine (SPICompletionRoutine, this);
		m_SPIMaster.StartWriteRead (SPI_CHIP_SELECT, TxData, RxBuffer, TEST_DATA_LENGTH);

		// just do something while the transfer is running
		for (unsigned nCount = 0; m_bTransferRunning; nCount++)
		{
			m_Screen.Rotor (0, nCount);
		}

		// transfer complete here
		CLogger::Get ()->Write (FromKernel, LogNotice, "%u bytes transfered", TEST_DATA_LENGTH);

#ifndef NDEBUG
		CLogger::Get ()->Write (FromKernel, LogDebug, "Dumping begin of received data:");

		debug_hexdump (RxBuffer, TEST_DATA_LENGTH > 128 ? 128 : TEST_DATA_LENGTH, FromKernel);
#endif
	}

	return ShutdownReboot;
}

void CKernel::SPICompletionRoutine (boolean bStatus, void *pParam)
{
	CKernel *pThis = (CKernel *) pParam;
	assert (pThis != 0);

	if (!bStatus)
	{
		CLogger::Get ()->Write (FromKernel, LogPanic, "SPI transfer error");
	}

	pThis->m_bTransferRunning = FALSE;
}
