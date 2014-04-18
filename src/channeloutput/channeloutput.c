#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#include "channeloutput.h"
#include "E131.h"
#include "FPD.h"
#include "../log.h"
#include "../sequence.h"
#include "../settings.h"
#include "SPIws2801.h"
#include "USBDMXOpen.h"
#include "USBDMXPro.h"
#include "USBPixelnet.h"
#include "USBRenard.h"



/////////////////////////////////////////////////////////////////////////////

#define MAX_CHANNEL_REMAPS  512

typedef struct {
	int src;
	int count;
	int dest;
} ChannelRemap;

ChannelRemap             remappedChannels[MAX_CHANNEL_REMAPS];
int                      channelRemaps      = 0;
int                      channelOutputCount = 0;
unsigned long            channelOutputFrame = 0;
FPPChannelOutputInstance channelOutputs[FPPD_MAX_CHANNEL_OUTPUTS];

/* Some prototypes for helpers below */
int LoadChannelRemapData(void);
void RemapChannels(char *channelData);
void PrintRemappedChannels(void);

/////////////////////////////////////////////////////////////////////////////

/*
 *
 */
int InitializeChannelOutputs(void) {
	int i = 0;

	channelOutputFrame = 0;

	for (i = 0; i < FPPD_MAX_CHANNEL_OUTPUTS; i++) {
		bzero(&channelOutputs[i], sizeof(channelOutputs[i]));
	}

	// Reset index so we can start populating the outputs array
	i = 0;

	if (FPDOutput.isConfigured())
	{
		channelOutputs[i].startChannel = 0;
		channelOutputs[i].output       = &FPDOutput;

		if (FPDOutput.open("", &channelOutputs[i].privData)) {
			i++;
		} else {
			LogErr(VB_CHANNELOUT, "ERROR Opening FPD Channel Output\n");
		}
	}

	if ((getFPPmode() == PLAYER_MODE) &&
		(E131Output.isConfigured()))
	{
		channelOutputs[i].startChannel = 0;
		channelOutputs[i].output       = &E131Output;

		if (E131Output.open("", &channelOutputs[i].privData)) {
			i++;
		} else {
			LogErr(VB_CHANNELOUT, "ERROR Opening E1.31 Channel Output\n");
		}
	}

	// Parse the channeloutputs config file for non-FPD, non-E1.31 outputs
	FILE *fp;
	char filename[1024];
	char buf[128];

	strcpy(filename, getMediaDirectory());
	strcat(filename, "/channeloutputs");

	LogDebug(VB_CHANNELOUT, "Loading Channel Outputs config\n");
	fp = fopen(filename, "r");
	if (fp == NULL) 
	{
		LogErr(VB_CHANNELOUT,
			"Could not open Channel Outputs config file %s: %s\n",
			filename, strerror(errno));
		return 0;
	}

	while(fgets(buf, 128, fp) != NULL)
	{
		int  enabled = 0;
		char type[32];
		int  start = 0;
		int  count = 0;
		char deviceConfig[160];

		if (buf[0] == '#') // Allow # comments for testing
			continue;

		int fields = sscanf(buf, "%d,%[^,],%d,%d,%s",
			&enabled, type, &start, &count, deviceConfig);

		if (fields != 5) {
			LogErr(VB_CHANNELOUT,
				"Invalid line in channeloutputs config file: %s\n", buf);
			continue;
		}

		if (!enabled) {
			LogInfo(VB_CHANNELOUT, "Skipping disabled channel output: %s\n", buf);
			continue;
		}

		if (count > (FPPD_MAX_CHANNELS - start)) {
			LogWarn(VB_CHANNELOUT,
				"Channel Output config, start (%d) + count (%d) exceeds max (%d) channel\n",
				start, count, FPPD_MAX_CHANNELS);

			count = FPPD_MAX_CHANNELS - start;

			LogWarn(VB_CHANNELOUT,
				"Count suppressed to %d for config line: %s\n", count, buf);
		}

		if (strlen(deviceConfig))
			strcat(deviceConfig, ";");

		strcat(deviceConfig, "type=");
		strcat(deviceConfig, type);

		LogDebug(VB_CHANNELOUT, "ChannelOutput: %d %s %d %d %s\n", enabled, type, start, count, deviceConfig);

		channelOutputs[i].startChannel = start - 1; // internally we start channel counts at zero
		channelOutputs[i].channelCount = count;

		if ((!strcmp(type, "Pixelnet-Lynx")) ||
			(!strcmp(type, "Pixelnet-Open")))
		{
			channelOutputs[i].output       = &USBPixelnetOutput;
		} else if (!strcmp(type, "DMX-Pro")) {
			channelOutputs[i].output       = &USBDMXProOutput;
		} else if (!strcmp(type, "DMX-Open")) {
			channelOutputs[i].output       = &USBDMXOpenOutput;
		} else if (!strcmp(type, "Renard")) {
			channelOutputs[i].output       = &USBRenardOutput;
		} else if (!strcmp(type, "SPI-WS2801")) {
			channelOutputs[i].output       = &SPIws2801Output;
		} else {
			LogErr(VB_CHANNELOUT, "Unknown Channel Output type: %s\n", type);
			continue;
		}

		if ((channelOutputs[i].output) &&
			(channelOutputs[i].output->open(deviceConfig, &channelOutputs[i].privData)))
		{
			if (channelOutputs[i].channelCount > channelOutputs[i].output->maxChannels(channelOutputs[i].privData)) {
				LogWarn(VB_CHANNELOUT,
					"Channel Output config, count (%d) exceeds max (%d) channel for configured output\n",
					channelOutputs[i].channelCount, channelOutputs[i].output->maxChannels(channelOutputs[i].privData));

				channelOutputs[i].channelCount = channelOutputs[i].output->maxChannels(channelOutputs[i].privData);

				LogWarn(VB_CHANNELOUT,
					"Count suppressed to %d for config: %s\n", channelOutputs[i].channelCount, buf);
			}
			i++;
		} else {
			LogErr(VB_CHANNELOUT, "ERROR Opening %s Channel Output\n", type);
		}
	}

	if ((getFPPmode() == PLAYER_MODE) &&
		(USBRenardOutput.isConfigured()))
	{
		channelOutputs[i].startChannel = 0;
		channelOutputs[i].output       = &USBRenardOutput;

		if (USBRenardOutput.open(getUSBDonglePort(),
			&channelOutputs[i].privData))
		{
			i++;
		} else {
			LogErr(VB_CHANNELOUT, "ERROR Opening USBRenard Channel Output\n");
		}
	}

	channelOutputCount = i;

	LogDebug(VB_CHANNELOUT, "%d Channel Outputs configured\n", channelOutputCount);

	LoadChannelRemapData();
}

/*
 * Reset the output frame count
 */
void ResetChannelOutputFrameNumber(void) {
	channelOutputFrame = 0;
}

/*
 *
 */
int SendChannelData(char *channelData) {
	int i = 0, maxChannels;
	FPPChannelOutputInstance *inst;

	RemapChannels(channelData);

	if (logMask & VB_CHANNELDATA) {
		HexDump("Channel Data", channelData, 16);
	}

	for (i = 0; i < channelOutputCount; i++) {
		inst = &channelOutputs[i];
		maxChannels = inst->output->maxChannels(inst->privData);
		inst->output->send(
			inst->privData,
			channelData + inst->startChannel,
			inst->channelCount < (FPPD_MAX_CHANNELS - inst->startChannel) ? inst->channelCount : (FPPD_MAX_CHANNELS - inst->startChannel));
	}

	channelOutputFrame++;
}

/*
 *
 */
int CloseChannelOutputs(void) {
	FPPChannelOutputInstance *output;
	int i = 0;

	for (i = 0; i < channelOutputCount; i++) {
		channelOutputs[i].output->close(channelOutputs[i].privData);
		if (channelOutputs[i].privData)
			free(channelOutputs[i].privData);
	}
}

/*
 *
 * NOTE: We subtract 1 from all source and destination channel numbers
 *       because our array is 0-based and the channel numbers start at 1.
 */
int LoadChannelRemapData(void) {
	FILE *fp;
	char filename[1024];
	char buf[32];
	char *s;
	int src;
	int dest;
	int count;

	strcpy(filename, getMediaDirectory());
	strcat(filename, "/channelremap");

	channelRemaps = 0;

	LogDebug(VB_CHANNELOUT, "Loading Channel Remap data.\n");
	fp = fopen(filename, "r");
	if (fp == NULL) 
	{
		LogErr(VB_CHANNELOUT, "Could not open Channel Remap file %s\n", filename);
		return 0;
	}

	while(fgets(buf, 32, fp) != NULL)
	{
		if (buf[0] == '#') // Allow # comments for testing
			continue;

		// Source
		s = strtok(buf, ",");
		src = strtol(s, NULL, 10);
		if (src <= 0)
			continue;

		remappedChannels[channelRemaps].src = src - 1;

		// Destination
		s = strtok(NULL, ",");
		dest = strtol(s, NULL, 10);
		if (dest <= 0)
			continue;

		remappedChannels[channelRemaps].dest = dest - 1;

		// Count
		s=strtok(NULL,",");
		count = strtol(s, NULL, 10);
		if (count <= 0)
			continue;

		remappedChannels[channelRemaps].count = count;

		if ((src + count - 1) > FPPD_MAX_CHANNELS) {
			LogErr(VB_CHANNELOUT, "ERROR: Source + Count exceeds max channel count in: %s\n", buf );
		} else if ((dest + count - 1) > FPPD_MAX_CHANNELS) {
			LogErr(VB_CHANNELOUT, "ERROR: Destination + Count exceeds max channel count in: %s\n", buf );
		} else {
		    channelRemaps++;
		}
	}
	fclose(fp);

	PrintRemappedChannels();

	return 1;
}

/*
 *
 */
inline void RemapChannels(char *channelData) {
	int i = 0;
	ChannelRemap *mptr;

	if (!channelRemaps)
		return;

	for (i = 0, mptr = &remappedChannels[0]; i < channelRemaps; i++, mptr++) {
		if (mptr->count > 1) {
			memcpy(channelData + mptr->dest,
				   channelData + mptr->src,
				   mptr->count);
		} else {
			channelData[mptr->dest] = channelData[mptr->src];
		}
	}
}

/*
 *
 */
void PrintRemappedChannels(void) {
	int i = 0;
	ChannelRemap *mptr;

	if (!channelRemaps) {
		LogDebug(VB_CHANNELOUT, "No channels are remapped.\n");
		return;
	}

	LogDebug(VB_CHANNELOUT, "Remapped Channels:\n");
	for (i = 0, mptr = &remappedChannels[0]; i < channelRemaps; i++, mptr++) {
		if (mptr->count > 1) {
			LogDebug(VB_CHANNELOUT, "  %d-%d => %d-%d (%d channels)\n",
				mptr->src, mptr->src + mptr->count - 1,
				mptr->dest, mptr->dest + mptr->count - 1, mptr->count);
		} else {
			LogDebug(VB_CHANNELOUT, "  %d => %d\n",
				mptr->src, mptr->dest);
		}
	}
}

