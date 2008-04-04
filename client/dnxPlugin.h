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

/** An abstract data type for a DNX plugin object. */
typedef struct { int unused; } DnxPlugin;

/** An abstract data type for a DNX module object. */
typedef struct { int unused; } DnxModule;

/** Register a dnx plugin with entry points.
 * 
 * @param[in] szPlugin - the name of the plugin to be registered.
 * @param[in] szErrMsg - an error message to be displayed if the plugin
 *    could not be registered.
 * 
 * @return Zero on success, or a non-zero error value.
 */
int dnxPluginRegister(char * szPlugin, char * szErrMsg);

/** Load a dnx plugin module into memory.
 * 
 * @param[in] module - the name of the module to be loaded.
 * 
 * @return Zero on success, or a non-zero error value.
 */
int dnxPluginLoad(DnxModule * module);

/** Unload a dnx plugin module from memory.
 * 
 * @param[in] module - the name of the module to be unloaded.
 */
void dnxPluginUnload(DnxModule * module);

/** Find an appropriate dnx plugin and use it to execute a commmand.
 * 
 * @param[in] command - the command to be executed by the plugin.
 * @param[out] resCode - the address of storage for the command's result code.
 * @param[out] resData - the address of storage for the command's stdout text.
 * @param[in] maxData - the maximum size of the @p resData buffer.
 * @param[in] timeout - the maximum number of seconds to wait for @p command 
 *    to complete before returning a timeout error.
 * @param[in] myaddr - the address (in human readable format) of this DNX node.
 * 
 * @return Zero on success, or a non-zero error value.
 */
int dnxPluginExecute(char * command, int * resCode, char * resData, 
      int maxData, int timeout, char * myaddr);

/** Search for a plugin in the plugin chain.
 * 
 * @param[in] command - the command to be executed.
 * @param[out] plugin - the address of storage for the located plugin to 
 *    be returned.
 * 
 * @return Zero on success, or a non-zero error value.
 */
int dnxPluginLocate(char * command, DnxPlugin ** plugin);

/** Isolate the base name of a plugin command.
 * 
 * @param[in] command - the command for which to have the base name isolated.
 * @param[out] baseName - the address of storage for the returned base name.
 * @param[in] maxData - the maximum size of the @p baseName buffer.
 * 
 * @return Zero on success, or a non-zero error value.
 */
int dnxPluginBaseName(char * command, char * baseName, int maxData);

/** Executes an internal plugin module.
 * 
 * @param[in] plugin - the plugin module to execute against @p command.
 * @param[in] command - the command to have @p plugin execute.
 * @param[out] resCode - the address of storage for the result code returned
 *    by @p plugin.
 * @param[out] resData - the resulting STDOUT text from the execution 
 *    of @p command by @p plugin.
 * @param[in] maxData - the maximum size of the @p resData buffer.
 * @param[in] timeout - the maximum number of seconds to wait for @p plugin
 *    to complete execution of @p command before returning a timeout error.
 * @param[in] myaddr - the address (in human readable format) of this DNX node.
 * 
 * @return Zero on success, or a non-zero error value.
 */
int dnxPluginInternal(DnxPlugin * plugin, char * command, int * resCode, 
      char * resData, int maxData, int timeout, char * myaddr);

/** Execute an external command line.
 * 
 * @param[in] command - the command to be executed.
 * @param[out] resCode - the address of storage for the result code returned
 *    by @p command.
 * @param[out] resData - the resulting STDOUT text from the execution 
 *    of @p command.
 * @param[in] maxData - the maximum size of the @p resData buffer.
 * @param[in] timeout - the maximum number of seconds to wait for @p command 
 *    to complete before returning a timeout error.
 * @param[in] myaddr - the address (in human readable format) of this DNX node.
 * 
 * @return Zero on success, or a non-zero error value.
 */
int dnxPluginExternal(char * command, int * resCode, char * resData, 
      int maxData, int timeout, char * myaddr);

/** Perform a time sensitive fgets.
 * 
 * @param[out] data - the address of storage for returning data from @p fp.
 * @param[in] size - the maximum size of @p data in bytes.
 * @param[in] fp - the file pointer to be read.
 * @param[in] timeout - the maximum number of seconds to wait for data to be
 *    returned before failing with a timeout error.
 * 
 * @return The address of @p data on success, or NULL on error.
 */
char * dnxFgets(char * data, int size, FILE * fp, int timeout);

/** Convert a dnx plugin string to a vector array.
 * 
 * The @p command buffer is modified such that each command argument is 
 * null-terminated on return.
 * 
 * @param[in] command - the string to be converted.
 * @param[out] argc - the address of storage for the number of elements 
 *    actually returned in @p argv.
 * @param[out] argv - the address of storage for returning a null-terminated 
 *    array of pointers to white-space-separated arguments in @p command.
 * @param[in] maxargs - the maximum number of entries in the @p argv array.
 * 
 * @return Zero on success, or a non-zero error value.
 */
int dnxPluginVector(char * command, int * argc, char ** argv, int maxargs);

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

