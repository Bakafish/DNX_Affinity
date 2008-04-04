#!/usr/bin/perl

use Getopt::Std;

###################################
#Get command line options and set variables
###################################

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
