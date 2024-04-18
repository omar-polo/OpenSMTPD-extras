#!/usr/bin/perl

use v5.36;

my %passwd;
sub parse {
	foreach my $passwd (@ARGV) {
		open(my $f, '<', $passwd) or die "can't open $passwd: $!\n";
		while (<$f>) {
			my ($name, $passwd, $uid, $gid, $lc, $change, $expire,
			    $gecos, $homedir, $shell) = split /:/;
			$passwd{$name} = {
				passwd => $passwd,
				uid => $uid,
				gid => $gid,
				lc => $lc,
				change => $change,
				expire => $expire,
				gecos => $gecos,
				home => $homedir,
				shell => $shell,
			};
		}
	}
}

parse; # fetch initial state

while (<STDIN>) {
	chomp;
	my @args = split /\|/, $_, 8;

	if ($args[0] eq 'config') {
		if ($args[1] eq 'ready') {
			say "register|credentials";
			say "register|userinfo";
			say "register|ready";
		}
		next; # ignore the configs for now
	}

	die "unknown message" if $args[0] ne 'table';
	die "unknown protocol" if $args[1] ne '0.1';

	my $cmd = $args[4];

	if ($cmd eq 'update') {
		my $id = $args[5];
		parse;	# XXX this can die on error
		say "update-result|$id|ok";
		next;
	}

	if ($cmd eq 'check' or $cmd eq 'lookup') {
		my ($kind, $id, $query) = ($args[5], $args[6], $args[7]);
		if ($kind ne 'credentials' and $kind ne 'userinfo') {
			say "$cmd-result|$id|error";
			next;
		}

		my $res = $passwd{$query};
		if (not defined $res) {
			say "$cmd-result|$id|not-found";
			next;
		}

		if ($cmd eq 'check') {
			say "$cmd-result|$id|found";
			next;
		}

		say "$cmd-result|$id|found|". $query .":". $res->{'passwd'};
		next;
	}

	if ($cmd eq 'fetch') {
		my $id = $args[6];
		say "fetch-result|$id|error";
		next;
	}

	die "unknown operation $cmd";
}
