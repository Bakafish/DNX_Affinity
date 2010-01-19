#!/usr/bin/perl

use Data::Dumper;

open (FILE, "</usr/local/dnx/var/log/dnxsrv.log.test");

my %counter;
my $types = {
    ASSIGN => 1,
    DISPATCH => 2,
    ACK => 3,
    COLLECT => 4,
    RESPONSE => 5,
    DECLINE => 6,
    EXPIRE => 7
    };
$types = $types, reverse $types;

my $match = qr(.*\ (ASSIGN|DISPATCH|ACK|COLLECT|RESPONSE|DECLINE|EXPIRE):\ Job\ (\d+):.*);

while(<FILE>) {
    $_ =~ m|$match|;
    ${$counter{$2}}[$types->{$1}]++;
}

print Dumper \%counter;
