/*
 *   Sequence handler for Falcon Pi Player (FPP)
 *
 *   Copyright (C) 2013 the Falcon Pi Player Developers
 *      Initial development by:
 *      - David Pitts (dpitts)
 *      - Tony Mace (MyKroFt)
 *      - Mathew Mrosko (Materdaddy)
 *      - Chris Pinkham (CaptainMurdoch)
 *      For additional credits and developers, see credits.php.
 *
 *   The Falcon Pi Player (FPP) is free software; you can redistribute it
 *   and/or modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation; either version 2 of
 *   the License, or (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _SEQUENCE_H
#define _SEQUENCE_H

#define FPPD_MAX_CHANNELS 65536

extern unsigned long seqFileSize;
extern int  seqDuration;
extern int  seqSecondsElapsed;
extern int  seqSecondsRemaining;
extern char seqData[FPPD_MAX_CHANNELS];

int   OpenSequenceFile(const char *filename);
int   IsSequenceRunning(void);
void  ReadSequenceData(void);
void  SendSequenceData(void);
void  SendBlankingData(void);
void  CloseSequenceFile(void);

#endif /* _SEQUENCE_H */
