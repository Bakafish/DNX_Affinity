----------------------------------
 A Note on Using Doxygen with DNX
----------------------------------

doxyfile.in
-----------
The file doxyfile.in is an autoconf input file, and can't be used directly by
Doxygen. However, this file can be manually configured to become a doxygen input
file. Use an editor to change any text surrounded by at '@' signs for live
values. Currently, doxyfile.in contains the following autoconf variables:

  @VERSION@ - the DNX major.minor version numbers.
  @top_srcdir@ - the top-level source directory - where configure.ac is located.

Others may be added over time. Replace these variables by hand, and this file
may be used with the Windows version of Doxygen to generate html documentation
from the DNX source code.

dnx.dxy
-------
Another option is to use the version-less doxygen input file, dnx.dxy, 
which will work from Windows as long as you execute doxygen from the dnx/
doc directory. The documentation version of the output files will be 0.0
since there's no simple way to insert that information in a static text file.
