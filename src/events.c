
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "effects.h"
#include "events.h"
#include "log.h"
#include "settings.h"

/*
 * Free a FPPevent structure pointer
 */
void FreeEvent(FPPevent *e)
{
	free(e->name);

	if (e->effect)
		free(e->effect);

	if (e->script)
		free(e->script);

	free(e);
}

/*
 * Load an event file into a FPPevent
 */
FPPevent* LoadEvent(char *id)
{
	FPPevent *event = NULL;
	FILE     *file;
	char      filename[1024];


	if (snprintf(filename, 1024, "%s/%s.fevt", getEventDirectory(), id) >= 1024)
	{
		LogErr(VB_EVENT, "Unable to open Event file: %s, filename too long\n",
			filename);
		return NULL;
	}

	file = fopen(filename, "r");
	if (!file)
	{
		LogErr(VB_EVENT, "Unable to open Event file %s\n", filename);
		return NULL;
	}

	event = (FPPevent*)malloc(sizeof(FPPevent));

	if (!event)
	{
		LogErr(VB_EVENT, "Unable to allocate memory for new Event %s\n", filename);
		return NULL;
	}

	bzero(event, sizeof(FPPevent));

	char     *line = NULL;
	size_t    len = 0;
	ssize_t   read;
	while ((read = getline(&line, &len, file)) != -1)
	{
		if (( ! line ) || ( ! read ) || ( read == 1 ))
			continue;

		char *token = strtok(line, "=");
		if ( ! token )
			continue;

		token = trimwhitespace(token);
		if (!strlen(token))
		{
			free(token);
			continue;
		}

		char *key = token;
		token = trimwhitespace(strtok(NULL, "="));

		if (token && strlen(token))
		{
			if (!strcmp(key, "majorID"))
			{
				int id = atoi(token);
				if (id < 1)
				{
					FreeEvent(event);
					free(token);
					free(key);
					return NULL;
				}
				event->majorID = id;
			}
			else if (!strcmp(key, "minorID"))
			{
				int id = atoi(token);
				if (id < 1)
				{
					FreeEvent(event);
					free(token);
					free(key);
					return NULL;
				}
				event->minorID = id;
			}
			else if (!strcmp(key, "name"))
			{
				if (strlen(token))
				{
					if (token[0] == '\'')
					{
						event->name = strdup(token + 1);
						if (event->name[strlen(event->name) - 1] == '\'')
							event->name[strlen(event->name) - 1] = '\0';
					}
					else
						event->name = strdup(token);
				}
			}
			else if (!strcmp(key, "effect"))
			{
				if (strlen(token) && strcmp(token, "''"))
				{
					char *c = strstr(token, ".eseq");
					if (c)
					{
						if ((c == (token + strlen(token) - 5)) ||
						    (c == (token + strlen(token) - 6)))
							*c = '\0';

						if (token[0] == '\'')
							event->effect = strdup(token + 1);
						else
							event->effect = strdup(token);
					}
				}
			}
			else if (!strcmp(key, "startChannel"))
			{
				int ch = atoi(token);
				if (ch < 1)
				{
					FreeEvent(event);
					free(token);
					free(key);
					return NULL;
				}
				event->startChannel = ch;
			}
			else if (!strcmp(key, "script"))
			{
				if (strlen(token) && strcmp(token, "''"))
				{
					if (token[0] == '\'')
					{
						event->script = strdup(token + 1);
						if (event->script[strlen(event->script) - 1] == '\'')
							event->script[strlen(event->script) - 1] = '\0';
					}
					else
						event->script = strdup(token);
				}
			}
		}

		if (token)
			free(token);
		free(key);
	}

	if (!event->effect && !event->script)
	{
		FreeEvent(event);
		return NULL;
	}

	LogDebug(VB_EVENT, "Event Loaded:\n");
	if (event->name)
		LogDebug(VB_EVENT, "Event Name  : %s\n", event->name);
	else
		LogDebug(VB_EVENT, "Event Name  : ERROR, no name defined in event file\n");

	LogDebug(VB_EVENT, "Event ID    : %d/%d\n", event->majorID, event->minorID);

	if (event->script)
		LogDebug(VB_EVENT, "Event Script: %s\n", event->script);

	if (event->effect)
	{
		LogDebug(VB_EVENT, "Event Effect: %s\n", event->effect);
		LogDebug(VB_EVENT, "Event St.Ch.: %d\n", event->startChannel);
	}

	return event;
}

/*
 * Fork and run an event script
 */
int RunEventScript(FPPevent *e)
{
	pid_t pid = 0;
	char  eventScript[1024];

	strcpy(eventScript, getScriptDirectory());
	strcat(eventScript, "/");
	strncat(eventScript, e->script, 1024 - strlen(eventScript));
	eventScript[1023] = '\0';

	pid = fork();
	if (pid == 0) // Event Script process
	{
		char *args[128];
		char *token = strtok(eventScript, " ");
		int   i = 1;

		args[0] = strdup(eventScript);
		while (token && i < 126)
		{
			args[i] = strdup(token);
			i++;

			token = strtok(NULL, " ");
		}
		args[i] = NULL;
		execvp("/bin/bash", args);

		LogErr(VB_EVENT, "RunEventScript(), ERROR, we shouldn't be here, this means "
			"that execvp() failed\n");
		exit(EXIT_FAILURE);
	}

	return 1;
}

/*
 * Trigger an event by major/minor number
 */
int TriggerEvent(char major, char minor)
{
	LogDebug(VB_EVENT, "TriggerEvent(%d, %d)\n", (unsigned char)major, (unsigned char)minor);

	if ((major > 25) || (major < 1) || (minor > 25) || (minor < 1))
		return 0;

	char id[6];

	sprintf(id, "%02d_%02d", major, minor);

	return TriggerEventByID(id);
}

/*
 * Trigger an event
 */
int TriggerEventByID(char *id)
{
	LogDebug(VB_EVENT, "TriggerEventByName(%s)\n", id);

	FPPevent *event = LoadEvent(id);

	if (!event)
	{
		LogErr(VB_EVENT, "Unable to load event %s\n", id);
		return 0;
	}

	if (event->effect)
		StartEffect(event->effect, event->startChannel);

	if (event->script)
		RunEventScript(event);

	FreeEvent(event);

	return 1;
}

