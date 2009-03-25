#ifndef DNXTYPES_H_INCLUDED
#define DNXTYPES_H_INCLUDED

///These typed define some 64 bit datatypes we will need later on.
typedef unsigned long long      U64;  ///unsigned 64bit int
typedef signed long long        S64;  ///signed 64bit int
typedef long double         F64;  ///64bit float

#ifndef bool
typedef enum { false, true } bool;
#endif

#ifndef PACKETS_IN
enum
{
        PACKETS_OUT,
        PACKETS_IN,
        PACKETS_FAILED
};
#endif

#endif // DNXTYPES_H_INCLUDED
