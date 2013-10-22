#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <stdbool.h>
#include <string.h>
#include "log.h"
#include "ogg123.h"

  
MusicStatus musicStatus;
pid_t pid=0;
pid_t childPID=0;
//int   pipeToOGG[2];
int   pipeFromOGG[2];
int MusicPlayerStatus = IDLE_MPLAYER_STATUS;

fd_set active_fd_set, read_fd_set;
struct timeval timeout;

int i;
char oggBuffer[MAX_BYTES_OGG];
int bufferPtr=0;
char state=0;
char strTime[34];


void sigchld_handler(int signal)
{
	int status;
	pid_t p = waitpid(-1, &status, WNOHANG);

	if (p == childPID)
	{
		childPID =0;
		close(pipeFromOGG[PIPE_READ]);
		//close(pipeToOGG[PIPE_WRITE]);
		MusicPlayerStatus = IDLE_MPLAYER_STATUS;
		E131_CloseSequenceFile();
		usleep(1000000);
	}
}

int oggInit()
{
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = sigchld_handler;
	sigaction(SIGCHLD, &sa, NULL);	
}


int oggPlaySong(char * musicFile)
{
	// Create Pipes to/from ogg123
  //pipe(pipeToOGG);
  pipe(pipeFromOGG);
	
  LogWrite("Play Song %s\n", musicFile);
  pid = fork();
  if (pid == 0)			// ogg123 process
  {
      dup2(pipeFromOGG[PIPE_WRITE], STDERR_FILENO); //ogg123 uses stderr for output
			close(pipeFromOGG[PIPE_WRITE]);
			// Close write side of pipe to ogg
			//close(pipeToOGG[PIPE_WRITE]);
			execlp("/usr/bin/ogg123", "ogg123", musicFile, NULL);
      exit(EXIT_FAILURE);
  }
  else							// Parent process
  {
    LogWrite("After fork\n");
    childPID = pid;
		// Close write side of pipe from ogg
     close(pipeFromOGG[PIPE_WRITE]);
		// Close read side of pipe to ogg
		//close(pipeToOGG[PIPE_READ]);
  }

	// Clear active file descriptor sets
	FD_ZERO (&active_fd_set);
	// Set description for reading from ogg
	FD_SET (pipeFromOGG[PIPE_READ], &active_fd_set);
	// Set timeout value for select
	timeout.tv_sec = 0;
	timeout.tv_usec = 5;
	MusicPlayerStatus = PLAYING_MPLAYER_STATUS;
}


int oggRunning()
{
  
  int status;
	if(childPID > 0)
  {
	  return 1; 
	}
	else
	{
		return 0; 
  }
}


int MusicProc()
{
    if(oggRunning()==1)
    {
      PollMusicInfo();
    }
}

void OGGstopSong()
{
	if(childPID > 0)
	{
		kill(childPID, SIGKILL);
	}
	childPID = 0;
  MusicPlayerStatus = IDLE_MPLAYER_STATUS;
	usleep(1000000);
}

void PollMusicInfo()
{
  int bytesRead;
	int result;
	read_fd_set = active_fd_set;
	if(select(FD_SETSIZE, &read_fd_set, NULL, NULL, &timeout) < 0)
  {
   	LogWrite("Error Select: %d\n",errno);
	if ( errno == 4 )
	{
		FD_ZERO(&read_fd_set);
	}
   	return; 
  }
	if(FD_ISSET(pipeFromOGG[PIPE_READ], &read_fd_set))
  {
 		bytesRead = read(pipeFromOGG[PIPE_READ], oggBuffer, MAX_BYTES_OGG);
		if (bytesRead > 0) 
		{
      ProcessOGGData(bytesRead);
		} 
  }
}

void ProcessOGGData(int bytesRead)
{
  bool commandNext=false;
  for(i=0;i<bytesRead;i++)
  {
    switch(oggBuffer[i])
    {
      case 'T':
        state = 1;
        break;
      case 'i':
      case 'm':
      case 'e':
        state++;
				break;
     case ':':
				if(state==4)
				{
					state++;
				}
				else
				{
					strTime[bufferPtr++]=oggBuffer[i];
				}
        break;
      default:
				if(state==5)
				{
					bufferPtr=0; 
					state=0;
				}
				else
				{
					state=0;
				}
				strTime[bufferPtr++]=oggBuffer[i];
				if(bufferPtr==32)
				{
					ParseTimes();
					bufferPtr++;
				}
				if(bufferPtr>32)
				{
					bufferPtr=33;
				}
				break;
    }
  }
}

void ParseTimes()
{
  int result;
  int secs;
  int mins;
  int subSecs;
	char tmp[3];
	tmp[2]= '\0';

	// Mins
	tmp[0]=strTime[1];
	tmp[1]=strTime[2];
	sscanf(tmp,"%d",&mins);

	// Secs
	tmp[0]=strTime[4];
	tmp[1]=strTime[5];
	sscanf(tmp,"%d",&secs);
	musicStatus.secondsElasped = 60*mins + secs;

	// Subsecs
	tmp[0]=strTime[7];
	tmp[1]=strTime[8];
	sscanf(tmp,"%d",&musicStatus.subSecondsElasped);
		
	// Mins Remaining
	tmp[0]=strTime[11];
	tmp[1]=strTime[12];
	sscanf(tmp,"%d",&mins);
	
	// Secs Remaining
	tmp[0]=strTime[14];
	tmp[1]=strTime[15];
	sscanf(tmp,"%d",&secs);
	musicStatus.secondsRemaining = 60*mins + secs;

	// Subsecs remaining
	tmp[0]=strTime[17];
	tmp[1]=strTime[18];
	sscanf(tmp,"%d",&musicStatus.subSecondsRemaining);

	// Total Mins
	tmp[0]=strTime[24];
	tmp[1]=strTime[25];
	sscanf(tmp,"%d",&musicStatus.minutesTotal);

	// Total Secs
	tmp[0]=strTime[27];
	tmp[1]=strTime[28];
	sscanf(tmp,"%d",&musicStatus.secondsTotal);
  LogWrite("Elasped: %.2d.%.2d  Remaining: %.2d Total %.2d:%.2d\n",
        musicStatus.secondsElasped,
        musicStatus.subSecondsElasped,
        musicStatus.secondsRemaining,
        musicStatus.minutesTotal,
        musicStatus.secondsTotal);
}











