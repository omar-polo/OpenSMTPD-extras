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

$5 == "check" {
	printf("check-result|%s|found\n", $7)
	fflush
}

$5 == "lookup" {
	printf("lookup-result|%s|found|op\n", $7)
	fflush
}

$5 == "update" {
	printf("update-result|%s|ok\n", $7)
	fflush
}
