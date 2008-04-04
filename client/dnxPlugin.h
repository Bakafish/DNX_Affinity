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

//	dnxPlugin.h
//
//	Utility routines to support plugin loading and execution.
//
//	Copyright (c) 2006-2007 Robert W. Ingraham (dnx-devel@lists.sourceforge.net)
//
//	First Written:   2006-09-09
//	Last Modified:   2007-03-21
//
//	License:
//
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License version 2 as
//	published by the Free Software Foundation.
//
//	This program is distributed in the hope that it will be useful,
//	but WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//	GNU General Public License for more details.
//
//	You should have received a copy of the GNU General Public License
//	along with this program; if not, write to the Free Software
//	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
//

#ifndef _DNXPLUGIN_H_
#define _DNXPLUGIN_H_

//
//	Constants
//

#define DNX_PLUGIN_RESULT_OK		0
#define DNX_PLUGIN_RESULT_WARNING	1
#define DNX_PLUGIN_RESULT_CRITICAL	2
#define DNX_PLUGIN_RESULT_UNKNOWN	3


//
//	Structures
//

typedef struct _DnxModule_ {
	char *path;
	void *handle;
	int (*init)(void);
	int (*deinit)(void);
	struct _DnxModule_ *next;
	struct _DnxModule_ *prev;
} DNX_MODULE;

typedef struct _DnxPlugin_ {
	char *name;
	int (*func)(int argc, char **argv);
	DNX_MODULE *parent;
	struct _DnxPlugin_ *next;
	struct _DnxPlugin_ *prev;
} DNX_PLUGIN;


//
//	Globals
//


//
//	Prototypes
//

// Initializes the plugin utility library
int dnxPluginInit (char *pluginPath);

// Releases the plugin utility library
int dnxPluginRelease (void);

// Registers a plugin with entry points
int dnxPluginRegister (char *szPlugin, char *szErrMsg);

// Loads/Unloads plugin module into memory
int dnxPluginLoad (DNX_MODULE *module);
int dnxPluginUnload (DNX_MODULE *module);

// Finds and executes the plugin
int dnxPluginExecute (char *command, int *resCode, char *resData, int maxData, int timeout);

// Searches for a plugin in the plugin chain
int dnxPluginLocate (char *name, DNX_PLUGIN **plugin);

// Isolate base name of a plugin command
int dnxPluginBaseName (char *command, char *baseName, int maxData);

// Executes an internal plugin module
int dnxPluginInternal (DNX_PLUGIN *plugin, char *command, int *resCode, char *resData, int maxData, int timeout);

// Executes an external plugin module
int dnxPluginExternal (char *command, int *resCode, char *resData, int maxData, int timeout);

// Performs time sensitive fgets
char *dnxFgets (char *data, int size, FILE *fp, int timeout);

// Converts plugin string to vector array
int dnxPluginVector (char *command, int *argc, char **argv, int max);

#endif   /* _DNXPLUGIN_H_ */

