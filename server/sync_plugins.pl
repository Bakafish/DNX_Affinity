#!/usr/bin/perl

#----------------------------------------------------------------------------
# Copyright (c) 2006-2007, Intellectual Reserve, Inc. All rights reserved.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License version 2 as 
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
#----------------------------------------------------------------------------

# @file sync_plugins.pl
# @author Robert W. Ingraham (dnx-devel@lists.sourceforge.net)
# @attention Please submit patches to http://dnx.sourceforge.net
# @ingroup DNX
#

use Getopt::Std;

# Get command line options and set variables

getopts("h:");
checkusage();

my @hosts = split(/,/,$opt_h);

foreach my $host (@hosts){
	print "Pushing plugins to $host\n";
	`/usr/bin/rsync -a --delete /usr/local/nagios/libexec/* nagios\@$host:/usr/local/nagios/libexec/`;
}
exit(0);

sub checkusage{
	if (!$opt_h){
		print "USAGE: $0 -h <LIST OF HOSTS>\nList should be comma seperated\n";	
		exit(3);
	}
}
