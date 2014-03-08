#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>

#include "../channeloutput/channeloutputthread.h"
#include "../log.h"
#include "mediaoutput.h"
#include "mpg123.h"
#include "../sequence.h"

#define MAX_BYTES_MP3 1000
#define TIME_STR_MAX  8

fd_set active_fd_set, read_fd_set;
struct timeval timeout;

int   pipeFromMP3[2];

char mp3Buffer[MAX_BYTES_MP3];
char strTime[34];


int mpg123_StartPlaying(const char *musicFile)
{
	char  fullAudioPath[1024];

	LogDebug(VB_MEDIAOUT, "mpg123_StartPlaying(%s)\n", musicFile);

	if (snprintf(fullAudioPath, 1024, "%s/%s", getMusicDirectory(), musicFile)
		>= 1024)
	{
		LogErr(VB_MEDIAOUT, "Unable to play %s, full path name too long\n",
			musicFile);
		return 0;
	}

	if (!FileExists(fullAudioPath))
	{
		LogErr(VB_MEDIAOUT, "%s does not exist!\n", fullAudioPath);
		return 0;
	}

	// Create Pipes to/from mpg123
	pipe(pipeFromMP3);
	
	pid_t mpg123Pid = fork();
	if (mpg123Pid == 0)			// mpg123 process
	{
		//mpg123 uses stderr for output
	    dup2(pipeFromMP3[MEDIAOUTPUTPIPE_WRITE], STDERR_FILENO);

		close(pipeFromMP3[MEDIAOUTPUTPIPE_WRITE]);
		pipeFromMP3[MEDIAOUTPUTPIPE_WRITE] = 0;

		execl("/usr/bin/mpg123", "mpg123", "-v", fullAudioPath, NULL);

	    exit(EXIT_FAILURE);
	}
	else							// Parent process
	{
		mediaOutput->childPID = mpg123Pid;

		// Close write side of pipe from mpg123
		close(pipeFromMP3[MEDIAOUTPUTPIPE_WRITE]);
		pipeFromMP3[MEDIAOUTPUTPIPE_WRITE] = 0;
	}

	// Clear active file descriptor sets
	FD_ZERO (&active_fd_set);
	// Set description for reading from mpg123
	FD_SET (pipeFromMP3[MEDIAOUTPUTPIPE_READ], &active_fd_set);
	// Set timeout value for select
	timeout.tv_sec = 0;
	timeout.tv_usec = 5;

	bzero(&mediaOutputStatus, sizeof(mediaOutputStatus));
	mediaOutputStatus.status = MEDIAOUTPUTSTATUS_PLAYING;

	return 1;
}


int mpg123_IsPlaying()
{

	int result = 0;

	pthread_mutex_lock(&mediaOutputLock);

	if(mediaOutput->childPID > 0)
		result = 1;

	pthread_mutex_unlock(&mediaOutputLock);

	return result;
}

void mpg123_StopPlaying()
{
	LogDebug(VB_MEDIAOUT, "mpg123_StopPlaying()\n");

	pthread_mutex_lock(&mediaOutputLock);

	if(mediaOutput->childPID > 0)
	{
		pid_t childPID = mediaOutput->childPID;

		mediaOutput->childPID = 0;
		kill(childPID, SIGKILL);
	}

	pthread_mutex_unlock(&mediaOutputLock);

	mediaOutputStatus.status = MEDIAOUTPUTSTATUS_IDLE;
	usleep(1000000);
}

void mpg123_ParseTimes()
{
	static int lastSyncCheck = 0;
	int result;
	int secs;
	int mins;
	int totalMins = 0;
	int totalMinsExtraSecs = 0;
	int subSecs;
	char tmp[3];
	tmp[2]= '\0';

	// Mins
	tmp[0]=strTime[1];
	tmp[1]=strTime[2];
	sscanf(tmp,"%d",&mins);
	totalMins += mins;

	// Secs
	tmp[0]=strTime[4];
	tmp[1]=strTime[5];
	sscanf(tmp,"%d",&secs);
	mediaOutputStatus.secondsElapsed = 60*mins + secs;
	totalMinsExtraSecs += secs;

	// Subsecs
	tmp[0]=strTime[7];
	tmp[1]=strTime[8];
	sscanf(tmp,"%d",&mediaOutputStatus.subSecondsElapsed);
		
	// Mins Remaining
	tmp[0]=strTime[11];
	tmp[1]=strTime[12];
	sscanf(tmp,"%d",&mins);
	totalMins += mins;
	
	// Secs Remaining
	tmp[0]=strTime[14];
	tmp[1]=strTime[15];
	sscanf(tmp,"%d",&secs);
	mediaOutputStatus.secondsRemaining = 60*mins + secs;
	totalMinsExtraSecs += secs;

	// Subsecs remaining
	tmp[0]=strTime[17];
	tmp[1]=strTime[18];
	sscanf(tmp,"%d",&mediaOutputStatus.subSecondsRemaining);

	mediaOutputStatus.minutesTotal = totalMins + totalMinsExtraSecs / 60;
	mediaOutputStatus.secondsTotal = totalMinsExtraSecs % 60;

	if ((IsSequenceRunning()) &&
		(mediaOutputStatus.secondsElapsed > 0) &&
		(lastSyncCheck != mediaOutputStatus.secondsElapsed))
	{
		float MusicSeconds = (float)((float)mediaOutputStatus.secondsElapsed + ((float)mediaOutputStatus.subSecondsElapsed/(float)100));

		LogDebug(VB_MEDIAOUT,
			"Elapsed: %.2d.%.2d  Remaining: %.2d Total %.2d:%.2d.\n",
			mediaOutputStatus.secondsElapsed,
			mediaOutputStatus.subSecondsElapsed,
			mediaOutputStatus.secondsRemaining,
			mediaOutputStatus.minutesTotal,
			mediaOutputStatus.secondsTotal);

		CalculateNewChannelOutputDelay(MusicSeconds);
		lastSyncCheck = mediaOutputStatus.secondsElapsed;
	}
}

// Sample format
// Frame#   149 [ 1842], Time: 00:03.89 [00:48.11], RVA:   off, Vol: 100(100)
void mpg123_ProcessMP3Data(int bytesRead)
{
	int  bufferPtr = 0;
	char state = 0;
	int  i = 0;
	bool commandNext=false;

	for(i=0;i<bytesRead;i++)
	{
		switch(mp3Buffer[i])
		{
			case 'T':
				state = 1;
				break;
			case 'i':
				if (state == 1)
					state = 2;
				else
					state = 0;
				break;
			case 'm':
				if (state == 2)
					state = 3;
				else
					state = 0;
				break;
			case 'e':
				if (state == 3)
					state = 4;
				else
					state = 0;
				break;
		 case ':':
				if(state==4)
				{
					state = 5;
				}
				else if (bufferPtr)
				{
					strTime[bufferPtr++]=mp3Buffer[i];
				}
				break;
			default:
				if(state==5)
				{
					bufferPtr=0; 
					state=6;
				}

				if (state >= 5)
				{
					strTime[bufferPtr++] = mp3Buffer[i];
					if(bufferPtr == 19)
					{
						mpg123_ParseTimes();
						bufferPtr = 0;
						state = 0;
					}
					if(bufferPtr>19)
					{
						bufferPtr=20;
					}
				}
				break;
		}
	}
}

void mpg123_PollMusicInfo()
{
	int bytesRead;
	int result;
	read_fd_set = active_fd_set;
	if(select(FD_SETSIZE, &read_fd_set, NULL, NULL, &timeout) < 0)
	{
	 	LogErr(VB_MEDIAOUT, "Error Select:%d\n",errno);
	 	return; 
	}
	if(FD_ISSET(pipeFromMP3[MEDIAOUTPUTPIPE_READ], &read_fd_set))
	{
 		bytesRead = read(pipeFromMP3[MEDIAOUTPUTPIPE_READ], mp3Buffer, MAX_BYTES_MP3);
		if (bytesRead > 0) 
		{
		    mpg123_ProcessMP3Data(bytesRead);
		} 
	}
}

int mpg123_ProcessData()
{
	if(mediaOutput->childPID > 0)
	{
		mpg123_PollMusicInfo();
	}
}

/*
 *
 */
int mpg123_CanHandle(const char *filename) {
	LogDebug(VB_MEDIAOUT, "mpg123_CanHandle(%s)\n", filename);
	int len = strlen(filename);

	if (len < 4)
		return 0;

	if (!strcasecmp(filename + len - 4, ".mp3"))
		return 1;

	return 0;
}

MediaOutput mpg123Output = {
	.filename     = NULL,
	.childPID     = 0,
	.childPipe    = pipeFromMP3,
	.canHandle    = mpg123_CanHandle,
	.startPlaying = mpg123_StartPlaying,
	.stopPlaying  = mpg123_StopPlaying,
	.processData  = mpg123_ProcessData,
	.isPlaying    = mpg123_IsPlaying
	};
