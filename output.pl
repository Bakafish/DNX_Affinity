#!/usr/bin/perl

use Data::Dumper;

open (FILE, "</usr/local/dnx/var/log/dnxsrv.log.test");

my @counter;
my $types = {
    'ASSIGN' => 1,
    'DISPATCH' => 2,
    'ACK' => 3,
    'COLLECT' => 4,
    'RESPONSE' => 5,
    'DECLINE' => 6,
    'EXPIRE' => 7
    };
$types = $types, reverse $types;

my $match = qr(.*\ (ASSIGN|DISPATCH|ACK|COLLECT|RESPONSE|DECLINE|EXPIRE):\ Job\ (\d+):.*);

while(<FILE>) {
    $_ =~ m|$match|;
    my ($event, $job) = ($1, $2);
    ${$counter[$job]}[0] = $job;
    ${$counter[$job]}[$types->{$event}]++;
}

foreach my $job (@counter) {
    print "Job: ", shift @{$job};
    for(my $i=1; $i<8; $i++) {
        if(defined $job->[$i]) {
            print $types->{$i} . " : $job->[$i] ";
        }
    }
    print "\n";
}

print Dumper reverse $types;
