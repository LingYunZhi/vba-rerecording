#include <cstdio>
#include <cstring>

#include "Flash.h"
#include "GBA.h"
#include "GBAGlobals.h"
#include "Sram.h"
#include "../common/System.h"
#include "../common/Util.h"

#define FLASH_READ_ARRAY         0
#define FLASH_CMD_1              1
#define FLASH_CMD_2              2
#define FLASH_AUTOSELECT         3
#define FLASH_CMD_3              4
#define FLASH_CMD_4              5
#define FLASH_CMD_5              6
#define FLASH_ERASE_COMPLETE     7
#define FLASH_PROGRAM            8
#define FLASH_SETBANK            9

u8	  flashSaveMemory[0x20000];
int32 flashState		  = FLASH_READ_ARRAY;
int32 flashReadState	  = FLASH_READ_ARRAY;
int32 flashSize			  = 0x10000;
int32 flashDeviceID		  = 0x1b;
int32 flashManufacturerID = 0x32;
int32 flashBank			  = 0;

static variable_desc flashSaveData[] = {
	{ &flashState,		   sizeof(int32) },
	{ &flashReadState,	   sizeof(int32) },
	{ &flashSaveMemory[0], 0x10000		 },
	{ NULL,				   0			 }
};

static variable_desc flashSaveData2[] = {
	{ &flashState,		   sizeof(int32) },
	{ &flashReadState,	   sizeof(int32) },
	{ &flashSize,		   sizeof(int32) },
	{ &flashSaveMemory[0], 0x20000		 },
	{ NULL,				   0			 }
};

static variable_desc flashSaveData3[] = {
	{ &flashState,		   sizeof(int32) },
	{ &flashReadState,	   sizeof(int32) },
	{ &flashSize,		   sizeof(int32) },
	{ &flashBank,		   sizeof(int32) },
	{ &flashSaveMemory[0], 0x20000		 },
	{ NULL,				   0			 }
};

void flashInit()
{
#ifdef USE_GBA_CORE_V7
	if (sramInitFix)
	{
		memset(flashSaveMemory, 0xff, 0x20000);
	}
	else
	{
		memset(flashSaveMemory, 0x00, 0x20000);
	}
#else
	memset(flashSaveMemory, 0xff, 0x20000);
#endif
	flashSize			= 0x10000;
	flashDeviceID		= 0x1b;
	flashManufacturerID = 0x32;
}

void flashReset()
{
	flashState	   = FLASH_READ_ARRAY;
	flashReadState = FLASH_READ_ARRAY;
	flashBank	   = 0;
}

void flashErase()
{
	flashInit();
	flashReset();
}

void flashSaveGame(gzFile gzFile)
{
	utilWriteData(gzFile, flashSaveData3);
}

void flashReadGame(gzFile gzFile, int version)
{
	if (version < SAVE_GAME_VERSION_5)
		utilReadData(gzFile, flashSaveData);
	else if (version < SAVE_GAME_VERSION_7)
	{
		utilReadData(gzFile, flashSaveData2);
		flashBank = 0;
		flashSetSize(flashSize);
	}
	else
	{
		utilReadData(gzFile, flashSaveData3);
	}
}

void flashReadGameSkip(gzFile gzFile, int version)
{
	// skip the flash data in a save game
	if (version < SAVE_GAME_VERSION_5)
		utilReadDataSkip(gzFile, flashSaveData);
	else if (version < SAVE_GAME_VERSION_7)
	{
		utilReadDataSkip(gzFile, flashSaveData2);
	}
	else
	{
		utilReadDataSkip(gzFile, flashSaveData3);
	}
}

void flashSetSize(int32 size)
{
	//  log("Setting flash size to %d\n", size);
	if (size == 0x10000)
	{
		flashDeviceID		= 0x1b;
		flashManufacturerID = 0x32;
	}
	else
	{
		flashDeviceID		= 0x13; //0x09;
		flashManufacturerID = 0x62; //0xc2;
	}
	// Added to make 64k saves compatible with 128k ones
	// (allow wrongfuly set 64k saves to work for Pokemon games)
	if ((size == 0x20000) && (flashSize == 0x10000))
		memcpy((u8 *)(flashSaveMemory + 0x10000), (u8 *)(flashSaveMemory), 0x10000);
	flashSize = size;
}

u8 flashRead(u32 address)
{
	//  log("Reading %08x from %08x\n", address, reg[15].I);
	//  log("Current read state is %d\n", flashReadState);
	address &= 0xFFFF;

	switch (flashReadState)
	{
	case FLASH_READ_ARRAY:
		return flashSaveMemory[(flashBank << 16) + address];
	case FLASH_AUTOSELECT:
		switch (address & 0xFF)
		{
		case 0:
			// manufacturer ID
			return u8(flashManufacturerID);
		case 1:
			// device ID
			return u8(flashDeviceID);
		}
		break;
	case FLASH_ERASE_COMPLETE:
		flashState	   = FLASH_READ_ARRAY;
		flashReadState = FLASH_READ_ARRAY;
		return 0xFF;
	}
	;
	return 0;
}

void flashSaveDecide(u32 address, u8 byte)
{
	//  log("Deciding save type %08x\n", address);
	if (address == 0x0e005555)
	{
		saveType		= 2;
		cpuSaveGameFunc = flashWrite;
	}
	else
	{
		saveType		= 1;
		cpuSaveGameFunc = sramWrite;
	}

	(*cpuSaveGameFunc)(address, byte);
}

void flashDelayedWrite(u32 address, u8 byte)
{
	saveType		= 2;
	cpuSaveGameFunc = flashWrite;
	flashWrite(address, byte);
}

void flashWrite(u32 address, u8 byte)
{
	//  log("Writing %02x at %08x\n", byte, address);
	//  log("Current state is %d\n", flashState);
	address &= 0xFFFF;
	switch (flashState)
	{
	case FLASH_READ_ARRAY:
		if (address == 0x5555 && byte == 0xAA)
			flashState = FLASH_CMD_1;
		break;
	case FLASH_CMD_1:
		if (address == 0x2AAA && byte == 0x55)
			flashState = FLASH_CMD_2;
		else
			flashState = FLASH_READ_ARRAY;
		break;
	case FLASH_CMD_2:
		if (address == 0x5555)
		{
			if (byte == 0x90)
			{
				flashState	   = FLASH_AUTOSELECT;
				flashReadState = FLASH_AUTOSELECT;
			}
			else if (byte == 0x80)
			{
				flashState = FLASH_CMD_3;
			}
			else if (byte == 0xF0)
			{
				flashState	   = FLASH_READ_ARRAY;
				flashReadState = FLASH_READ_ARRAY;
			}
			else if (byte == 0xA0)
			{
				flashState = FLASH_PROGRAM;
			}
			else if (byte == 0xB0 && flashSize == 0x20000)
			{
				flashState = FLASH_SETBANK;
			}
			else
			{
				flashState	   = FLASH_READ_ARRAY;
				flashReadState = FLASH_READ_ARRAY;
			}
		}
		else
		{
			flashState	   = FLASH_READ_ARRAY;
			flashReadState = FLASH_READ_ARRAY;
		}
		break;
	case FLASH_CMD_3:
		if (address == 0x5555 && byte == 0xAA)
		{
			flashState = FLASH_CMD_4;
		}
		else
		{
			flashState	   = FLASH_READ_ARRAY;
			flashReadState = FLASH_READ_ARRAY;
		}
		break;
	case FLASH_CMD_4:
		if (address == 0x2AAA && byte == 0x55)
		{
			flashState = FLASH_CMD_5;
		}
		else
		{
			flashState	   = FLASH_READ_ARRAY;
			flashReadState = FLASH_READ_ARRAY;
		}
		break;
	case FLASH_CMD_5:
		if (byte == 0x30)
		{
			// SECTOR ERASE
			memset(&flashSaveMemory[(flashBank << 16) + (address & 0xF000)],
			       0,
			       0x1000);
			systemSaveUpdateCounter = SYSTEM_SAVE_UPDATED;
			flashReadState = FLASH_ERASE_COMPLETE;
		}
		else if (byte == 0x10)
		{
			// CHIP ERASE
			memset(flashSaveMemory, 0, flashSize);
			systemSaveUpdateCounter = SYSTEM_SAVE_UPDATED;
			flashReadState = FLASH_ERASE_COMPLETE;
		}
		else
		{
			flashState	   = FLASH_READ_ARRAY;
			flashReadState = FLASH_READ_ARRAY;
		}
		break;
	case FLASH_AUTOSELECT:
		if (byte == 0xF0)
		{
			flashState	   = FLASH_READ_ARRAY;
			flashReadState = FLASH_READ_ARRAY;
		}
		else if (address == 0x5555 && byte == 0xAA)
			flashState = FLASH_CMD_1;
		else
		{
			flashState	   = FLASH_READ_ARRAY;
			flashReadState = FLASH_READ_ARRAY;
		}
		break;
	case FLASH_PROGRAM:
		flashSaveMemory[(flashBank << 16) + address] = byte;
		systemSaveUpdateCounter = SYSTEM_SAVE_UPDATED;
		flashState	   = FLASH_READ_ARRAY;
		flashReadState = FLASH_READ_ARRAY;
		break;
	case FLASH_SETBANK:
		if (address == 0)
		{
			flashBank = (byte & 1);
		}
		flashState	   = FLASH_READ_ARRAY;
		flashReadState = FLASH_READ_ARRAY;
		break;
	}
}

