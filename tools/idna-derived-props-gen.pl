#!/usr/bin/perl
#
# Copyright 2014 Chris Young <chris@unsatisfactorysoftware.co.uk>
#
# This file is part of NetSurf, http://www.netsurf-browser.org/
#
# NetSurf is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; version 2 of the License.
#
# NetSurf is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

use strict;

use Getopt::Long ();
use Fcntl qw( O_CREAT O_EXCL O_WRONLY O_APPEND O_RDONLY O_WRONLY );

use constant GETOPT_OPTS => qw( auto_abbrev no_getopt_compat bundling );
use constant GETOPT_SPEC =>
  qw( output|o=s
      properties|p=s
      joining|j=s
      help|h|? );

# default option values:
my %opt = qw(properties "idna-tables-properties.csv" joining "DerivedJoiningType.txt");

sub usage
{
    my @fmt = map { s/::$//; $_ } keys(%{$::{'msgfmt::'}});
    print(STDERR <<TXT );
usage:
     $0 [-o output-file] -p properties-file -j joining-file

     output-file   : defaults to standard output
TXT
    exit(1);
}

sub output_stream
{
    if( $opt{output} )
    {
	my $ofh;

	sysopen( $ofh, $opt{output}, O_CREAT|O_EXCL|O_APPEND|O_WRONLY ) ||
	  die( "$0: Failed to open output file $opt{output}: $!\n" );

	return $ofh;
    }

    return \*STDOUT;
}

sub input_stream
{
    my $stream = $_[0];

    if( $opt{$stream} )
    {
	my $ifh;

	sysopen( $ifh, $opt{$stream}, O_RDONLY ) ||
	    die( "$0: Failed to open input file $stream: $!\n" );

	return $ifh;
    }
    die( "$0: No input file for $stream");
}

sub main
{
    my $output;
    my $properties;
    my $joining;
    my $opt_ok;

    # option parsing:
    Getopt::Long::Configure( GETOPT_OPTS );
    $opt_ok = Getopt::Long::GetOptions( \%opt, GETOPT_SPEC );

    # double check the options are sane (and we weren't asked for the help)
    if( !$opt_ok || $opt{help} )
    {
        usage();
    }

    # open the appropriate files
    $properties = input_stream("properties");
    $joining = input_stream("joining");
    $output = output_stream();

    
    print { $output } <<HEADER;
/* This file is generated by idna-derived-props-gen.pl
 * DO NOT EDIT BY HAND
 */
#ifndef _NETSURF_UTILS_IDNA_PROPS_H_
#define _NETSURF_UTILS_IDNA_PROPS_H_

typedef enum idna_property {
	IDNA_P_PVALID		= 1,
	IDNA_P_CONTEXTJ		= 2,
	IDNA_P_CONTEXTO		= 3,
	IDNA_P_DISALLOWED	= 4,
	IDNA_P_UNASSIGNED	= 5
} idna_property;

typedef enum idna_unicode_jt {
	IDNA_UNICODE_JT_U	= 0,
	IDNA_UNICODE_JT_C	= 1,
	IDNA_UNICODE_JT_D	= 2,
	IDNA_UNICODE_JT_R	= 3,
	IDNA_UNICODE_JT_T	= 4,
	IDNA_UNICODE_JT_L	= 5
} idna_unicode_jt;


typedef struct idna_table {
	int32_t start;
	int32_t end;
	union p {
		idna_property property;
		idna_unicode_jt jt;
	} p;
} idna_table;

idna_table idna_derived[] = {
HEADER


    my $line = <$properties>; # discard header line

    while($line = <$properties>) {
	my @items = split(/\,/, $line);
	my @codepoints = split(/-/, $items[0]);
	if($#codepoints == 0) {
	    $codepoints[1] = $codepoints[0];
	}
	print { $output } "\t{ 0x" . $codepoints[0] . ", 0x" . $codepoints[1] . ", .p.property = IDNA_P_" . $items[1] . " },\n";
    }

    close($properties);

    print { $output } <<HEADER;
	{ 0, 0, .p.property = 0}
};

idna_table idna_joiningtype[] = {
HEADER


    while($line = <$joining>) {
	chop($line);
	if(substr($line, 0, 1) eq '#') {next;}
	if(length($line) == 0) {next;}
	my @items = split(/;/, $line);
	my @codepoints = split(/\./, $items[0]);
	if($#codepoints == 0) {
	    $codepoints[2] = $codepoints[0];
	}
	print { $output } "\t{ 0x" . $codepoints[0] . ", 0x" . $codepoints[2] . ", .p.jt = IDNA_UNICODE_JT_" . substr($items[1], 1, 1) . " },\n";
    }

    close($joining);

    print { $output } <<HEADER;
	{ 0, 0, .p.jt = 0}
};
#endif
HEADER


}

main();
