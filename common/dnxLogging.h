/*--------------------------------------------------------------------------
 
   Copyright (c) 2006-2007, Intellectual Reserve, Inc. All rights reserved.
 
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License version 2 as 
   published by the Free Software Foundation.
 
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
 
   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 
  --------------------------------------------------------------------------*/

// dnxLogging.h
//
// DNX Server Logging Functions
//
// Copyright (c) 2006-2007 Robert W. Ingraham (dnx-devel@lists.sourceforge.net)
//
// First Written:   2006-08-22
// Last Modified:   2007-08-22
//
// License:
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License version 2 as
// published by the Free Software Foundation.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
//

#ifndef _DNXLOGGING_H_
#define _DNXLOGGING_H_

//
// Constants
//


//
// Structures
//


//
// Prototypes
//

#if 0
int nebLog (char *fmt, ...);
#endif
int dnxSyslog (int priority, char *fmt, ...);
int dnxDebug (int level, char *fmt, ...);

#endif   /* _DNXLOGGING_H_ */

