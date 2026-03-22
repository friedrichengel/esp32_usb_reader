#!/usr/bin/perl
use strict;
use warnings;
use IO::Socket::INET;
use Socket qw(inet_ntoa);
use POSIX qw(strftime);

my $PORT = $ARGV[0] // 80;

my $server = IO::Socket::INET->new(
    LocalPort => $PORT,
    Type      => SOCK_STREAM,
    Reuse     => 1,
    Listen    => 10,
) or die "Konnte Socket auf Port $PORT nicht öffnen: $!\n"
        . "(Tipp: Ports < 1024 benötigen Root/Admin-Rechte, alternativ: perl http_logger.pl 8080)\n";

# Lokale IP-Adressen ermitteln
my @ips = ("127.0.0.1");
eval {
    my $hostname = `hostname`;
    chomp $hostname;
    my @addrs = (gethostbyname($hostname))[4];
    for my $addr (@addrs) {
        next unless defined $addr;
        my $ip = inet_ntoa($addr);
        next if $ip eq "127.0.0.1" || $ip =~ /:/;
        push @ips, $ip;
    }
};

print "=" x 60 . "\n";
print "  HTTP Debug Logger lauscht auf Port $PORT\n";
print "  Erreichbar unter:\n";
for my $ip (@ips) {
    print "    http://$ip:$PORT/\n";
}
print "  Abbrechen mit Ctrl+C\n";
print "=" x 60 . "\n\n";

while (my $client = $server->accept()) {
    my $peer_addr = $client->peerhost();
    my $peer_port = $client->peerport();
    my $timestamp = strftime("%Y-%m-%d %H:%M:%S", localtime);

    # Request lesen (bis Leerzeile)
    my @header_lines;
    my $content_length = 0;
    while (my $line = <$client>) {
        $line =~ s/\r\n$/\n/;
        last if $line eq "\n";
        push @header_lines, $line;
        if ($line =~ /^Content-Length:\s*(\d+)/i) {
            $content_length = int($1);
        }
    }

    # Body lesen wenn vorhanden
    my $body = "";
    if ($content_length > 0) {
        read($client, $body, $content_length);
    }

    # Ausgabe
    print "─" x 60 . "\n";
    print "[$timestamp]  $peer_addr:$peer_port\n";
    print "─" x 60 . "\n";

    if (@header_lines) {
        # Erste Zeile (Methode + Pfad + Protokoll)
        my $request_line = $header_lines[0];
        chomp $request_line;
        print "  $request_line\n\n";

        # GET-Parameter aus URL parsen
        if ($request_line =~ m{^\w+\s+[^?]*\?([^\s]+)}) {
            my $query = $1;
            print "  GET-Parameter:\n";
            for my $pair (split /&/, $query) {
                my ($k, $v) = split /=/, $pair, 2;
                $v //= "";
                $k = url_decode($k);
                $v = url_decode($v);
                printf "    %-20s = %s\n", $k, $v;
            }
            print "\n";
        }

        # Header-Zeilen
        print "  Header:\n";
        for my $i (1 .. $#header_lines) {
            my $h = $header_lines[$i];
            chomp $h;
            print "    $h\n";
        }
    }

    # POST-Body
    if ($body ne "") {
        print "\n  Body ($content_length Bytes):\n";
        # Versuche als URL-encoded zu parsen
        if ($body =~ /^[\w%+\-.]+=/) {
            print "  POST-Parameter:\n";
            for my $pair (split /&/, $body) {
                my ($k, $v) = split /=/, $pair, 2;
                $v //= "";
                $k = url_decode($k);
                $v = url_decode($v);
                printf "    %-20s = %s\n", $k, $v;
            }
        } else {
            print "    $body\n";
        }
    }

    print "\n";

    # HTTP-Antwort senden
    my $response_body = "OK - Request empfangen um $timestamp von $peer_addr:$peer_port\n";
    print $client "HTTP/1.1 200 OK\r\n";
    print $client "Content-Type: text/plain; charset=utf-8\r\n";
    print $client "Content-Length: " . length($response_body) . "\r\n";
    print $client "Connection: close\r\n";
    print $client "\r\n";
    print $client $response_body;

    close($client);
}

sub url_decode {
    my ($str) = @_;
    $str =~ s/\+/ /g;
    $str =~ s/%([0-9A-Fa-f]{2})/chr(hex($1))/ge;
    return $str;
}
