use strict;
use warnings;

# Same contract as hello.py: the request comes in the environment,
# the answer goes to stdout (headers, a blank line, the page).
my $query = $ENV{"QUERY_STRING"} // "";
my $name = "inconnu";
if ($query =~ /(?:^|&)name=([^&]*)/) {
    $name = $1;
}

# Escape HTML, as html.escape does in hello.py.
$name =~ s/&/&amp;/g;
$name =~ s/</&lt;/g;
$name =~ s/>/&gt;/g;

print "Content-Type: text/html\n";
print "\n";
print "<!DOCTYPE html>\n";
print "<html><head><meta charset=\"utf-8\"><title>CGI Perl</title></head><body>\n";
print "<h1>Bonjour $name, ici Perl !</h1>\n";
print "<p>Interpreteur : /usr/bin/perl</p>\n";
print "</body></html>\n";
