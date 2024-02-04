#!/usr/bin/awk -f

BEGIN {
	FS = "|"
}

$1 == "config" && $2 == "ready" {
	print "register|alias"
	print "register|ready"
	fflush
	next
}

$1 == "config" {
	printf("XXX: config line: %s\n", $0) > "/dev/stderr"
	fflush
	next
}

$1 == "check" {
	printf("check-result|%s|found\n", $3)
	fflush
	next
}

$1 == "lookup" {
	printf("lookup-result|%s|found|op\n", $3)
	fflush
}
