#!/usr/bin/perl

use Data::Dumper;

open (FILE, "</usr/local/dnx/var/log/dnxsrv.log.test");

my @counter;
my $types = {
    ASSIGN => 0,
    DISPATCH => 1,
    ACK => 2,
    COLLECT => 3,
    RESPONSE => 4,
    DECLINE => 5,
    EXPIRE => 6
    };
$types = $types, reverse $types;

my $match = qr(.*\ (ASSIGN|DISPATCH|ACK|COLLECT|RESPONSE|DECLINE|EXPIRE):\ Job\ (\d+):.*);

while(<FILE>) {
    $_ =~ m|$match|;
    ${$counter[$2]}[$types->{$1}]++;
}



print Dumper \@counter;
