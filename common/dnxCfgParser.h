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

/** Definitions and prototypes for parsing DNX server config files.
 *
 * @file dnxCfgParser.h
 * @author John Calcote (jcalcote@users.sourceforge.net)
 * @attention Please submit patches to http://dnx.sourceforge.net
 * @ingroup DNX_COMMON_IFC
 */

#ifndef _DXCFGPARSER_H_
#define _DXCFGPARSER_H_

#include <stddef.h>

/** An enumeration of data types that the config parser understands. 
 * 
 * The comment after each type in the enumeration describes the data type
 * that should be passed in the void * data element of the DnxCfgDictionary
 * array passed by the user. The address of an object of the specified type
 * should be passed in the data element of the structure. Some of these 
 * variables are allocated, and others are simple direct storage. If the 
 * type is defined as the address of a pointer type, the space is allocated
 * by the configuration system. If the type is defined as the address of 
 * an object, then the object is not allocated.
 * 
 * Strings and string arrays are null-terminated. The first element of 
 * integer arrays indicates the size of the array following.
 * 
 * Allocated memory is freed when the DnxCfgParser object is deleted.
 */
typedef enum DnxCfgType
{
   DNX_CFG_STRING = 0,        /*!< Text string - data: &char* */
   DNX_CFG_INT,               /*!< Signed integer - data: &int */
   DNX_CFG_INT_ARRAY,         /*!< Comma-delimited array - data: &int* */
   DNX_CFG_UNSIGNED,          /*!< Unsigned integer - data: &unsigned */
   DNX_CFG_UNSIGNED_ARRAY,    /*!< Comma-delimited array - data: &unsigned* */
   DNX_CFG_IP_ADDR,           /*!< IP address or DNS name - data: &struct sockaddr* */
   DNX_CFG_IP_ADDR_ARRAY,     /*!< Comma-delimited array - data: &struct sockaddr** */
   DNX_CFG_URL,               /*!< URL - data: &char* */
   DNX_CFG_FSPATH,            /*!< File system path - data: &char* */
} DnxCfgType;

/** An array of these structures defines a users configuration database. */
typedef struct DnxCfgDictionary
{
   char * varname;            /*!< The string name of the variable. */
   DnxCfgType type;           /*!< The type of the variable. */
   void * data;               /*!< The address of storage for the parsed value. */
} DnxCfgDictionary;

/** An opaque type for the DNX configuration parser object. */
typedef struct { int unused; } DnxCfgParser;

/** Create and initialize a DNX configuration parser object.
 * 
 * A configuration parser object is used to read and parse a specified 
 * configuration file, using a user-provided configuration variable dictionary
 * to validate and convert the string values into true data types.
 * 
 * The user-supplied @p errhandler function is called for all parse and 
 * validation errors. The @p errhandler routine takes four parameters; an 
 * integer error code, an integer line number, a context buffer, and a caller-
 * specific opaque data pointer. The line number and buffer represent the 
 * problem location and some context in @p cfgfile.
 * 
 * The @p errhandler parameter is optional; the caller may pass NULL. If this 
 * parameter is omitted, the parser will stop at the first parse or validation
 * error. If it is NOT omitted, the parser will attempt to recover and continue
 * parsing and calling the @p errhandler routine for each additional error
 * discovered.
 * 
 * @param[in] cfgfile - the path name of the config file to be parsed.
 * @param[in] dict - a static array of DnxCfgDictionary objects, each of which 
 *    defines a valid configuration variable name and type for this parser,
 *    as well as storage for either the parsed value or a pointer to allocated
 *    storage for the parsed value.
 * @param[in] dictsz - the number of elements in the @p dict array.
 * @param[in] errhandler - the error handler routine called on parse and
 *    validation errors. This parameter is optional - the caller may pass NULL.
 * @param[in] data - an opaque pointer passed through to the client error 
 *    handler routine.
 * @param[out] pcp - the address of storage for the returned config parser.
 * 
 * @return Zero on success, or a non-zero error value.
 */
int dnxCfgParserCreate(char * cfgfile, DnxCfgDictionary * dict, size_t dictsz,
      void (*errhandler)(int err, int line, char * buf, void * data),
      void * data, DnxCfgParser ** pcp);

/** Parse or reparse the config file associated with an existing config parser.
 * 
 * @param[in] cp - the DNX configuration parser whose file should be reparsed.
 * 
 * @return Zero on success, or a non-zero error value.
 */
int dnxCfgParserParse(DnxCfgParser * cp);

/** Destroy an existing configuration object.
 * 
 * Frees memory associated with the configuration parser object.
 * 
 * @param[in] cp - the DNX configuration parser object to be destroyed.
 */
void dnxCfgParserDestroy(DnxCfgParser * cp);

#endif   /* _DXCFGPARSER_H_ */

