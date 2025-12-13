#!/usr/bin/perl -w

use strict;
use Getopt::Long;

my $event_filter = "";    # event type filter, defaults to first encountered event
my $comm_filter = "";    # event type filter, defaults to none
my $group_by_pid = 0;

GetOptions(
    'event-filter=s' => \$event_filter,
    'comm-filter=s' => \$comm_filter,
    'group' => \$group_by_pid,
) or die <<USAGE_END;
USAGE: $0 [options] infile > outfile\n
    --event-filter=EVENT    # event name filter\n
    --group # group by pid or not\n
USAGE_END

my $pid;
my $can_print;
my %content;

if ($event_filter eq "") {
    print 'Please set event_filter.';
    exit;
}

# Split comm_filter into an array of allowed comm values
my @comm_filters = split /,/, $comm_filter;

# Split event_filter into an array of allowed event values
my @event_filters = split /,/, $event_filter;

#
# Main loop
#
while (defined($_ = <>)) {
    # skip remaining comments
    next if m/^#/;
    chomp;

    # end of stack. save cached data.
    if (m/^$/) {
        # ignore filtered samples
        next if not $can_print;
        if ($group_by_pid) {
            $content{$pid} .= $_ . "\n";
        } else {
            print $_ . "\n";
        }
        undef $can_print;
        undef $pid;
        next;
    }

    #
    # event record start
    #
    if (/^\s*?(\S.*?)\s+(-?\d+)\/*(\d+)*\s+/) {
        my $comm = $1;
        $pid = $2;

        # Check if comm matches any in the comm_filters array
        if (@comm_filters && !grep { $_ eq $comm } @comm_filters) {
            next;
        }

        if (/\d+\.\d+:\s*(\d+)*\s+(\S+):\s/) {
            # 697.180928: probe:tcp_sendmsg: (ffffffffba398b80)
            my $event = $2;
            # Check if event matches any in the event_filters array
            if (@event_filters && !grep { $_ eq $event } @event_filters) {
                next;
            }
        }

        $can_print = 1;
        if ($group_by_pid) {
            $content{$pid} .= $_ . "\n";
        } else {
            print $_ . "\n";
        }

    #
    # stack line
    #
    } elsif (/^\s*(\w+)\s*(.+) \((.*)\)/) {
        # ignore filtered samples
        next if not $can_print;
        if ($group_by_pid) {
            $content{$pid} .= $_ . "\n";
        } else {
            print $_ . "\n";
        }
    } else {
        # warn "Unrecognized line: $_";
    }
}

if ($group_by_pid) {
    foreach my $key (keys %content) {
        my $value = $content{$key};
        print "=== pid=$key: ===\n";
        print $value;
    }
}
