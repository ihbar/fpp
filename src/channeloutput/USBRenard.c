
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "../log.h"
#include "../settings.h"

#include "USBRenard.h"

/////////////////////////////////////////////////////////////////////////////

typedef struct usbRenardPrivData {
	char filename[1024];
	char *outputData;
	int  fd;
	int  maxChannels;
} USBRenardPrivData;

// Assume clocks are accurate to 1%, so insert a pad byte every 100 bytes.
#define PAD_DISTANCE 100

/////////////////////////////////////////////////////////////////////////////

/*
 *
 */
void USBRenard_Dump(USBRenardPrivData *privData) {
	LogDebug(VB_CHANNELOUT, "  privData: %p\n", privData);

	if (!privData)
		return;

	LogDebug(VB_CHANNELOUT, "    filename   : %s\n", privData->filename);
	LogDebug(VB_CHANNELOUT, "    fd         : %d\n", privData->fd);
	LogDebug(VB_CHANNELOUT, "    maxChannels: %i\n", privData->maxChannels);
}

/*
 *
 */
int USBRenard_Open(char *configStr, void **privDataPtr) {
	LogDebug(VB_CHANNELOUT, "USBRenard_Open('%s')\n", configStr);

	USBRenardPrivData *privData = malloc(sizeof(USBRenardPrivData));
	if (privData == NULL)
	{
		LogErr(VB_CHANNELOUT, "Error %d allocating private memory: %s\n",
			errno, strerror(errno));
		
		return 0;
	}
	bzero(privData, sizeof(USBRenardPrivData));
	privData->fd = -1;

	strcpy(privData->filename, "/dev/");
	strcat(privData->filename, configStr);

	LogInfo(VB_CHANNELOUT, "Opening %s for Renard output\n",
		privData->filename);

	privData->fd = SerialOpen(privData->filename, atoi(getUSBDongleBaud()), "8N1");
	if (privData->fd < 0)
	{
		LogErr(VB_CHANNELOUT, "Error %d opening %s: %s\n",
			errno, privData->filename, strerror(errno));

		free(privData);
		return 0;
	}
	
	privData->outputData = malloc(USBRenard_MaxChannels(privData));
	if (privData->outputData == NULL)
	{
		LogErr(VB_CHANNELOUT, "Error %d allocating channel memory: %s\n",
			errno, strerror(errno));

		free(privData);
		return 0;
	}
	bzero(privData->outputData, privData->maxChannels);

	USBRenard_Dump(privData);

	*privDataPtr = privData;

	return 1;
}

/*
 *
 */
int USBRenard_Close(void *data) {
	LogDebug(VB_CHANNELOUT, "USBRenard_Close(%p)\n", data);

	USBRenardPrivData *privData = (USBRenardPrivData*)data;
	USBRenard_Dump(privData);

	SerialClose(privData->fd);
	privData->fd = -1;
}

/*
 *
 */
int USBRenard_IsConfigured(void) {
	if ((strcmp(getUSBDonglePort(),"DISABLED")) &&
		(!strcmp(getUSBDongleType(), "Renard")))
		return 1;

	return 0;
}

/*
 *
 */
int USBRenard_IsActive(void *data) {
	LogDebug(VB_CHANNELOUT, "USBRenard_IsActive(%p)\n", data);
	USBRenardPrivData *privData = (USBRenardPrivData*)data;

	if (!privData)
		return 0;

	USBRenard_Dump(privData);

	if (privData->fd > 0)
		return 1;

	return 0;
}

/*
 *
 */
int USBRenard_SendData(void *data, char *channelData, int channelCount)
{
	LogDebug(VB_CHANNELDATA, "USBRenard_SendData(%p, %p, %d)\n",
		data, channelData, channelCount);

	USBRenardPrivData *privData = (USBRenardPrivData*)data;

	if (channelCount <= privData->maxChannels) {
		bzero(privData->outputData, privData->maxChannels);
	} else {
		LogErr(VB_CHANNELOUT,
			"USBRenard_SendData() tried to send %d bytes when max is 4096\n",
			channelCount);
		return 0;
	}

	memcpy(privData->outputData, channelData, channelCount);

	// Act like "Renard (modified)" and don't output special codes.  There are
	// 3 we need to worry about.
	// 0x7D - Pad Byte    - map to 0x7C
	// 0x7E - Sync Byte   - map to 0x7C
	// 0x7F - Escape Byte - map to 0x80
	char *dptr = privData->outputData;
	int i = 0;
	for( i = 0; i < privData->maxChannels; i++ ) {
		if (*dptr == '\x7D')
			*dptr = '\x7C';
		else if (*dptr == '\x7E')
			*dptr = '\x7C';
		else if (*dptr == '\x7F')
			*dptr = '\x80';

		dptr++;
	}

	// Send start of packet byte
	write(privData->fd, "\x7E\x80", 2);

	dptr = privData->outputData;

	// Assume clocks are accurate to 1%, so insert a pad byte every 100 bytes.
	for ( i = 0; i < privData->maxChannels/PAD_DISTANCE; i++ )
	{
		// Send our pad byte
		write(privData->fd, "\x7D", 1);

		// Send Renard Data (Only send the channels we're given, not max)
		write(privData->fd, dptr, (channelCount - (100 * i)));

		dptr += 100;
	}
}

/*
 * Data for this was gathered from the DIYC wiki pages on Renard channels at
 * 50ms refresh rate.  For the newer (faster) speeds the PX1 page was
 * referenced.
 *
 * http://www.doityourselfchristmas.com/wiki/index.php?title=Renard#Number_of_Circuits_.28Channels.29_per_Serial_Port
 * http://www.doityourselfchristmas.com/wiki/index.php?title=Renard_PX1_Pixel_Controller#Maximum_Number_of_Pixels_per_Controller
 *
 */
int USBRenard_MaxChannels(void *data)
{
	USBRenardPrivData *privData = (USBRenardPrivData*)data;

	if (privData->maxChannels != 0)
		return privData->maxChannels;
	
	char *baud = getUSBDongleBaud();

	if ( strcmp(baud, "460800") == 0 )
		privData->maxChannels = 2292;
	else if ( strcmp(baud, "230400") == 0 )
		privData->maxChannels = 1146;
	else if ( strcmp(baud, "115200") == 0 )
		privData->maxChannels = 574;
	else if ( strcmp(baud, "57600") == 0 )
		privData->maxChannels = 286;
	else if ( strcmp(baud, "38400") == 0 )
		privData->maxChannels = 190;
	else if ( strcmp(baud, "19200") == 0 )
		privData->maxChannels = 94;
	
	return privData->maxChannels;
}

/*
 * Declare our external interface struct
 */
FPPChannelOutput USBRenardOutput = {
	.maxChannels  = USBRenard_MaxChannels,
	.open         = USBRenard_Open,
	.close        = USBRenard_Close,
	.isConfigured = USBRenard_IsConfigured,
	.isActive     = USBRenard_IsActive,
	.send         = USBRenard_SendData //TODO, this is the guts of the thing here...
	};

