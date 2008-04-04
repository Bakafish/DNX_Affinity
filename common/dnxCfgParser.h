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

/** An enumeration of data types that the configuration parser understands. 
 * 
 * The comment after each type in the enumeration describes the data type
 * that should be passed as a void pointer in the @em value field of the 
 * dictionary structure.
 * 
 * The syntax is clearly not strictly conformant to C language syntax. For
 * example, (&char*) indicates that the address of a character pointer should
 * be passed. Strict C language syntax would be more like (char**), but this 
 * syntax is ambiguous. It could mean that a variable defined as char ** is
 * passed by value, or that a variable of type char * is passed by address.
 * 
 * The addresses of pointer types (eg., &char*) are returned as references to 
 * allocated heap blocks, while the addresses of pointer-sized integral types 
 * (eg., &unsigned) are returned by value as objects.
 * 
 * Strings, string arrays and pointer arrays are null-terminated. The first 
 * element of integer arrays is the size of the remaining array of values.
 * 
 * Allocated memory may be freed by calling dnxCfgFreeValues.
 */
typedef enum DnxCfgType
{
   DNX_CFG_STRING,            //!< Text string (&char*).
   DNX_CFG_STRING_ARRAY,      //!< String array; comma-delimited (&char**).
   DNX_CFG_INT,               //!< Signed integer (&int).
   DNX_CFG_INT_ARRAY,         //!< Comma-delimited array (&int*).
   DNX_CFG_UNSIGNED,          //!< Unsigned integer (&unsigned).
   DNX_CFG_UNSIGNED_ARRAY,    //!< Comma-delimited array (&unsigned*).
   DNX_CFG_ADDR,              //!< Address or DNS name (&struct sockaddr*).
   DNX_CFG_ADDR_ARRAY,        //!< Comma-delimited array (&struct sockaddr**).
   DNX_CFG_URL,               //!< URL (&char*).
   DNX_CFG_FSPATH,            //!< File system path (&char*).
} DnxCfgType;

/** An array of these structures defines a user's configuration variables. */
typedef struct DnxCfgDict
{
   char * varname;            //!< The string name of the variable.
   DnxCfgType type;           //!< The type of the variable.
   void * valptr;             //!< The address of the value object.
} DnxCfgDict;

/** A function type defining the prototype for a validator function. 
 * 
 * The elements of @p ppvals correspond directly to the entries in the
 * user specified configuration dictionary. However, these pointers refer to 
 * temporary space. Once this validator function has returned success, then
 * the values in this temporary space are exported to the user dictionary.
 * 
 * @param[in] dict - a reference to (a copy of) the user-specified dictionary.
 * @param[in] ppvals - an array of pointers to configuration values.
 * @param[in] passthru - an opaque pointer passed through from the 
 *    dnxCfgParserCreate @p passthru parameter.
 * 
 * @return Zero on success, or a non-zero error value which is subsequently
 * returned through the dnxCfgParserParse function.
 */
typedef int DnxCfgValidator_t(DnxCfgDict * dict, void * ppvals[], void * passthru);

/** An abstraction data type for the DNX configuration parser. */
typedef struct { int unused; } DnxCfgParser;

/** Create a new configuration parser object.
 * 
 * A configuration parser is used to read and parse a specified configuration 
 * file using a user-provided configuration variable dictionary to validate 
 * and convert the string values into usable data values.
 * 
 * A null-terminated string containing default configuration file entries may 
 * optionally be passed in the @p cfgdefs parameter. The format of the string
 * is the same as that of a configuration file. The @p cfgdefs string is 
 * parsed BEFORE the configuration file, thus configuration file entries
 * override default configuration entries.
 * 
 * A null-terminated string containing command-line overrides for existing 
 * configuration file entries may optionally be passed in the @p cmdover 
 * parameter. The format of the string is the same as that of a configuration 
 * file. The @p cmdover string is parsed AFTER the configuration file, thus
 * @p cmdover entries override configuration file entries.
 * 
 * The validator function @p vfp is called after all values are parsed. The 
 * values will only be written to the user's dictionary after @p vfp returns 
 * success (0). 
 * 
 * @param[in] cfgdefs - an optional string containing default config file 
 *    entries. Pass NULL if not required.
 * @param[in] cfgfile - an optional path name of the config file to be parsed.
 *    Pass NULL if not required.
 * @param[in] cmdover - an optional string containing command line override 
 *    entries. Pass NULL if not required.
 * @param[in] dict - an array of DnxCfgDict objects, each of which defines a 
 *    valid configuration variable name and type for this parser. The @p dict
 *    array should be terminated with NULL field values (a NULL pointer in the 
 *    @em varname field, and DNX_CFG_NULL in the @em type field).
 * @param[in] vfp - a pointer to a validator function.
 * @param[out] cpp - the address of storage for the newly created 
 *    configuration parser object.
 * 
 * @return Zero on success, or a non-zero error value.
 */
int dnxCfgParserCreate(char * cfgdefs, char * cfgfile, char * cmdover, 
      DnxCfgDict * dict, DnxCfgValidator_t * vfp, DnxCfgParser ** cpp);

/** Parse a configuration file into a value array.
 * 
 * Parses an optional default set of configuration values, followed by the 
 * configuration file, followed by an optional set of command line override
 * configuration values. After all parsing is complete, calls the validator 
 * function (if specified). If the validator passes (returns success - 0), 
 * then the parsed values are copied into the user's dictionary and success
 * is returned. Otherwise no dictionary value changes take effect.
 * 
 * @param[in] cp - the configuration parser object on which to run the parser.
 * @param[in] passthru - an opaque pointer to user data that's passed through 
 *    to the validator function.
 * 
 * @return Zero on success, or a non-zero error value. Possible error 
 * values include DNX_OK (on success), DNX_ERR_ACCESS, DNX_ERR_NOTFOUND, 
 * DNX_ERR_SYNTAX or DNX_ERR_MEMORY.
 */
int dnxCfgParserParse(DnxCfgParser * cp, void * passthru);

/** Destroy a previously created configuration parser object.
 * 
 * @param[in] cp - the configuration parser object to be destroyed.
 */
void dnxCfgParserDestroy(DnxCfgParser * cp);

#endif   /* _DXCFGPARSER_H_ */

