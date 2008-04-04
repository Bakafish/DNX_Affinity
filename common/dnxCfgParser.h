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
 * that should be passed as a void pointer in the corresponding element of 
 * the @em ppvals parameters of the functions defined below.
 * 
 * The syntax is clearly not strictly conformant to C language syntax. For
 * example (&char*) indicates that the address of a character pointer should
 * be passed. Strict C language syntax would be more like (char**), but this 
 * syntax is ambiguous. It could mean that a variable defined as char ** is
 * passed by value, or that a variable of type char * is passed by address.
 * 
 * The addresses of pointer types (eg., &char*) are returned as references to 
 * allocated heap blocks, while the addresses of integral types are returned 
 * by value as objects.
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
} DnxCfgDict;

/** An abstraction data type for the DNX configuration parser. */
typedef struct { int unused; } DnxCfgParser;

/** Create a new configuration parser object.
 * 
 * A configuration parser is used to read and parse a specified configuration 
 * file using a user-provided configuration variable dictionary to validate 
 * and convert the string values into usable data values.
 * 
 * A null-terminated array of pointers to configuration strings containing 
 * default values may optionally be passed in the @p cfgdefs parameter. The 
 * @p cfgdefs array strings contain configuration file entries that should be 
 * parsed before the actual file is parsed. Thus, configuration file entries 
 * override default array entries.
 * 
 * @param[in] cfgfile - the path name of the config file to be parsed.
 * @param[in] cfgdefs - an array of pointers to default config file entries.
 *    Elements of @p ppvals are initialized to these defaults before @p cfgfile
 *    is parsed. The format of the strings in @cfgdefs is identical to that 
 *    of the configuration file itself. The pointer array must be null-
 *    terminated. This parameter is optional and may be passed as NULL.
 * @param[in] dict - an array of DnxCfgDict objects, each of which defines a 
 *    valid configuration variable name and type for this parser. The @p dict
 *    array should be terminated with NULL field values (a NULL pointer in the 
 *    @em varname field, and DNX_CFG_NULL in the @em type field).
 * @param[out] cpp - the address of storage for the newly created 
 *    configuration parser object.
 * 
 * @return Zero on success, or a non-zero error value.
 */
int dnxCfgParserCreate(char * cfgfile, char * cfgdefs[], DnxCfgDict * dict, 
      DnxCfgParser ** cpp);

/** Parse a configuration file into a value array.
 * 
 * A configuration parser is used to read and parse a specified configuration 
 * file using a user-provided configuration variable dictionary to validate 
 * and convert the string values into usable data values.
 * 
 * A null-terminated array of pointers to configuration strings containing 
 * default values may optionally be passed in the @p cfgdefs parameter. The 
 * @p cfgdefs array strings contain configuration file entries that should be 
 * parsed before the actual file is parsed. Thus, configuration file entries 
 * override default array entries.
 * 
 * Depending on the type of the data value, the @p ppvals array will either
 * return the value, or the address of allocated storage containing the value.
 * If the internal type of the value (defined in the comment following the 
 * dnx config value type enumeration entry) is, for example, &int, then the 
 * config value is set by value; if the type is, say &char*, then, the config
 * value is allocated and returned by reference.
 * 
 * @param[in] cp - the configuration parser object on which to run the parser.
 * @param[out] ppvals - an array of opaque pointers to storage locations for 
 *    the values parsed from @p cfgfile. Each element of the array is the 
 *    address of storage for either the actual value or the address of 
 *    allocated storage for the string or array value. The fields in @p ppvals 
 *    are typed by the corresponding type fields in the @p dict array. This
 *    array need not be terminated, but must be as large as the number of 
 *    valid entries in the @p dict array (minus the null-terminator entry).
 * 
 * @return Zero on success, or a non-zero error value. Possible error 
 * values include DNX_OK (on success), DNX_ERR_ACCESS, DNX_ERR_NOTFOUND, 
 * DNX_ERR_SYNTAX or DNX_ERR_MEMORY.
 */
int dnxCfgParserParse(DnxCfgParser * cp, void * ppvals[]);

/** Free memory in all pointer types in a value array; zero addresses.
 * 
 * @param[in] dict - the dictionary to be cleared. The @p dict array should
 *    be terminated with NULL field values (a NULL pointer in the @em varname 
 *    field, and DNX_CFG_NULL in the @em type field).
 * @param[in] ppvals - an array of opaque pointers to value storage addresses,
 *    each of which contains either the address of a stored object, or the 
 *    address of allocated storage. The fields in @p ppvals are typed by the
 *    corresponding type fields in the @p dict array.
 */
void dnxCfgParserFreeCfgValues(DnxCfgParser * cp, void * ppvals[]);

/** Destroy a previously created configuration parser object.
 * 
 * @param[in] cp - the configuration parser object to be destroyed.
 */
void dnxCfgParserDestroy(DnxCfgParser * cp);

#endif   /* _DXCFGPARSER_H_ */

