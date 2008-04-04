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

/** Types and definitions for plugin loading and execution.
 * 
 * @file dnxPlugin.h
 * @author Robert W. Ingraham (dnx-devel@lists.sourceforge.net)
 * @attention Please submit patches to http://dnx.sourceforge.net
 * @ingroup DNX_CLIENT_IFC
 */

#ifndef _DNXPLUGIN_H_
#define _DNXPLUGIN_H_

#include <stdio.h>

#define DNX_PLUGIN_RESULT_OK        0  /*!< DNX plugin result: success. */
#define DNX_PLUGIN_RESULT_WARNING   1  /*!< DNX plugin result: warning. */
#define DNX_PLUGIN_RESULT_CRITICAL  2  /*!< DNX plugin result: critical. */
#define DNX_PLUGIN_RESULT_UNKNOWN   3  /*!< DNX plugin result: unknown. */

/** Find an appropriate dnx plugin and use it to execute a commmand.
 * 
 * @param[in] command - the command to be executed by the plugin.
 * @param[out] resCode - the address of storage for the command's result code.
 * @param[out] resData - the address of storage for the command's stdout text.
 * @param[in] maxData - the maximum size of the @p resData buffer.
 * @param[in] timeout - the maximum number of seconds to wait for @p command 
 *    to complete before returning a timeout error.
 * @param[in] myaddr - the address (in human readable format) of this DNX node.
 */
void dnxPluginExecute(char * command, int * resCode, char * resData, 
      int maxData, int timeout, char * myaddr);

/** Initialize the dnx client plugin utility library.
 *
 * @param[in] pluginPath - the file system path where plugin libraries are
 *    to be found.
 * 
 * @return Zero on success, or a non-zero error value.
 */
int dnxPluginInit(char * pluginPath);

/** Clean up the dnx plugin utility library. */
void dnxPluginRelease(void);

#endif   /* _DNXPLUGIN_H_ */

