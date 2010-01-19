#!/usr/bin/perl

use Data::Dumper;

open (FILE, "</usr/local/dnx/var/log/dnxsrv.log.test");

my @counter;
my $types = {
    'ASSIGN' => 1,
    'DISPATCH' => 2,
    'ACK' => 3,
    'COLLECT' => 4,
    'DECLINE' => 5,
    'EXPIRE' => 6
    };
%{$types} = (%{$types}, reverse %{$types});


my $match = qr(.*\ (ASSIGN|DISPATCH|ACK|COLLECT|DECLINE|EXPIRE):\ Job\ (\d+):.*);

while(<FILE>) {
    my ($event, $job) = ($_ =~ m|$match|);
    if(defined $event) {
        ${$counter[$job]}[0] = $job;
        print "$job $event\n";
        ${$counter[$job]}[$types->{$event}]++;
    }
}

foreach my $job (@counter) {
    print "Job: ", shift @{$job};
    for(my $i=1; $i<8; $i++) {
        if(defined $job->[$i]) {
            print " $types->{$i} : $job->[$i]";
        }
    }
    print "\n";
}

