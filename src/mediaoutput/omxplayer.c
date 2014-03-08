#include <errno.h>
#include <pty.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdio_ext.h>

#include <sys/types.h>
#include <sys/socket.h>

#include "../channeloutput/channeloutputthread.h"
#include "../log.h"
#include "omxplayer.h"
#include "../settings.h"

#define MAX_BYTES_OMX 1000
#define TIME_STR_MAX  8

fd_set active_fd_set, read_fd_set;
struct timeval timeout;

int pipeFromOMX[2];

char omxBuffer[MAX_BYTES_OMX];
char dataStr[17];


int omxplayer_StartPlaying(const char *filename)
{
	char  fullVideoPath[1024];

	LogDebug(VB_MEDIAOUT, "omxplayer_StartPlaying(%s)\n", filename);

	if (snprintf(fullVideoPath, 1024, "%s/%s", getVideoDirectory(), filename)
		>= 1024)
	{
		LogErr(VB_MEDIAOUT, "Unable to play %s, full path name too long\n",
			filename);
		return 0;
	}

	if (!FileExists(fullVideoPath))
	{
		LogErr(VB_MEDIAOUT, "%s does not exist!\n", fullVideoPath);
		return 0;
	}

	// Create Pipes to/from omxplayer
	pid_t omxplayerPID = forkpty(&pipeFromOMX[0], 0, 0, 0);
	if (omxplayerPID == 0)			// omxplayer process
	{
		seteuid(1000); // 'pi' user
		execl("/usr/bin/omxplayer", "omxplayer", "-s", fullVideoPath, NULL);

		LogErr(VB_MEDIAOUT, "omxplayer_StartPlaying(), ERROR, we shouldn't "
			"be here, this means that execl() failed\n");

		exit(EXIT_FAILURE);
	}
	else							// Parent process
	{
		mediaOutput->childPID = omxplayerPID;
	}

	// Clear active file descriptor sets
	FD_ZERO (&active_fd_set);
	// Set description for reading from omxplayer
	FD_SET (pipeFromOMX[0], &active_fd_set);
	// Set timeout value for select
	timeout.tv_sec = 0;
	timeout.tv_usec = 5;

	bzero(&mediaOutputStatus, sizeof(mediaOutputStatus));
	mediaOutputStatus.status = MEDIAOUTPUTSTATUS_PLAYING;

	return 1;
}

/*
 *
 */
int omxplayer_IsPlaying()
{
	if (mediaOutput->childPID)
		return 1;

	return 0;
}

/*
 *
 */
void omxplayer_ParseTicks(int ticks)
{
}

/*
 *
 */
void omxplayer_ProcessPlayerData(int bytesRead)
{
	int        ticks = 0;
	static int lastSyncCheck = 0;
	int        mins = 0;
	int        secs = 0;
	int        subsecs = 0;
	int        totalCentiSecs = 0;


	// Data is line buffered so all stats lines should start with "V : "
    if ((!strncmp(omxBuffer, "V : ", 4)) &&
		(bytesRead > 20))
	{
		errno = 0;
		ticks = strtol(&omxBuffer[4], NULL, 10);
		if (errno) {
			LogErr(VB_MEDIAOUT, "Error parsing omxplayer output.\n");
			return;
		}
	} else {
		return;
	}

	totalCentiSecs = ticks / 10000;
	mins = totalCentiSecs / 6000;
	secs = totalCentiSecs % 6000 / 100;
	subsecs = totalCentiSecs % 100;

	mediaOutputStatus.secondsElapsed = 60 * mins + secs;

	mediaOutputStatus.subSecondsElapsed = subsecs;

	// FIXME, need to get the following for videos:
	// mediaOutputStatus.secondsRemaining = 60 * mins + secs;
	// mediaOutputStatus.subSecondsRemaining = subsecs;
	// mediaOutputStatus.minutesTotal = somethingotother
	// mediaOutputStatus.secondsTotal = somethingotother

	if ((IsSequenceRunning()) &&
		(mediaOutputStatus.secondsElapsed > 0) &&
		(lastSyncCheck != mediaOutputStatus.secondsElapsed))
	{
		float MediaSeconds = (float)((float)mediaOutputStatus.secondsElapsed + ((float)mediaOutputStatus.subSecondsElapsed/(float)100));

		LogDebug(VB_MEDIAOUT,
			"Elapsed: %.2d.%.2d  Remaining: %.2d Total %.2d:%.2d.\n",
			mediaOutputStatus.secondsElapsed,
			mediaOutputStatus.subSecondsElapsed,
			mediaOutputStatus.secondsRemaining,
			mediaOutputStatus.minutesTotal,
			mediaOutputStatus.secondsTotal);

		CalculateNewChannelOutputDelay(MediaSeconds);
		lastSyncCheck = mediaOutputStatus.secondsElapsed;
	}
}

/*
 *
 */
void omxplayer_PollPlayerInfo()
{
	int bytesRead;
	int result;
	read_fd_set = active_fd_set;
	if(select(FD_SETSIZE, &read_fd_set, NULL, NULL, &timeout) < 0)
	{
	 	LogErr(VB_MEDIAOUT, "Error Select:%d\n", errno);
	 	return; 
	}
	if(FD_ISSET(pipeFromOMX[0], &read_fd_set))
	{
 		bytesRead = read(pipeFromOMX[0], omxBuffer, MAX_BYTES_OMX);
		if (bytesRead > 0) 
		{
			omxplayer_ProcessPlayerData(bytesRead);
		} 
	}
}
int omxplayer_ProcessData()
{
	if(omxplayer_IsPlaying())
	{
	  omxplayer_PollPlayerInfo();
	}
}

void omxplayer_StopPlaying()
{
	if (!mediaOutput->childPID)
		return;

	LogDebug(VB_MEDIAOUT, "StopVideo(), killing pid %d\n",
		mediaOutput->childPID);

	kill(mediaOutput->childPID, SIGKILL);
	mediaOutput->childPID = 0;

	// omxplayer is a shell script wrapper around omxplayer.bin and
	// killing the PID of the schell script doesn't kill the child
	// for some reason, so use this hack for now.
	system("killall -9 omxplayer.bin");
}

/*
 *
 */
int omxplayer_CanHandle(const char *filename) {
	LogDebug(VB_MEDIAOUT, "omxplayer_CanHandle(%s)\n", filename);

	int len = strlen(filename);

	if (len < 4)
		return 0;

	if (!strcasecmp(filename + len - 4, ".mp4"))
		return 1;

	return 0;
}

MediaOutput omxplayerOutput = {
	.filename     = NULL,
	.childPID     = 0,
	.childPipe    = pipeFromOMX,
	.canHandle    = omxplayer_CanHandle,
	.startPlaying = omxplayer_StartPlaying,
	.stopPlaying  = omxplayer_StopPlaying,
	.processData  = omxplayer_ProcessData,
	.isPlaying    = omxplayer_IsPlaying
	};

