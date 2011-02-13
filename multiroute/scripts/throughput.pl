#!/usr/bin/perl

# スループット計算ツール (トレースファイル使用)
#
# Wada Laboratory, Shizuoka University
# Takahiro Sugimoto	<keyaki.no.kokage@gmail.com>
#
# Last Modified: 2008/12/19 16:29:44

use strict;
use warnings;
use Getopt::Std;

# ** 引数処理
# 既定値
my $FLOW = 'udp';
my $SRC = '_0_';
my $DST = '_1_';
my $INTERVAL = '1.0';
# Getoptで処理
my %opts = ();
getopts('t:i:d:s:d:f:bh', \%opts);
if (exists $opts{'h'}) {
	&usage;
	exit 0;
}
unless (exists $opts{'t'}) {
	printf STDERR "トレースファイルが指定されていません\n";
	&usage;
	exit 1;
}
if (exists $opts{'f'}) {
	$FLOW = $opts{'f'};
}
if (exists $opts{'i'}) {
	$INTERVAL = $opts{'i'};
}
if (exists $opts{'s'}) {
	$SRC = $opts{'s'};
}
if (exists $opts{'d'}) {
	$DST = $opts{'d'};
}

# ** 集計
open (FILE, "< $opts{'t'}") or die "トレースファイル $opts{'t'} が開けません。($!)";

my $sum_recv = 0; my $throughput = 0; my $clock = 0;
my @tcp_packets = ();
while (<FILE>) {

	my @line = split(' ', $_);
	#  0: 送信(s) / 受信(r)
	#  1: イベント発生時間 [s]
	#  2: ノード番号
	#  3: イベント発生レイヤ (IFQ, MAC, AGT, RTR 等)
	#  5: シーケンス番号
	#  6: フローの種類 (ACK, RTS, CTS, tcp, cbr 等)
	#  7: パケットサイズ [bytes]
	# 17: TCPシーケンス番号 (TCPの場合)

	if ($FLOW eq 'tcp' && $line[6] eq 'tcp') {

		# TCP データパケットの場合 … 重複したものを削除してtcp_packetsに記憶、スループット計算は後で
		$line[17] =~ s/\[//; $line[18] =~ s/\]//;
		if ($line[0] eq 'r' && $line[2] eq $DST && $line[3] eq 'AGT' && $line[6] eq 'tcp') {
			my $drop = 0;
			# 重複確認
			foreach (@tcp_packets) {
				if ( $_->[1] == $line[17] ) {
					$drop = 1;
					last;
				}
			}
			if ($drop != 1) {
				my $ref = [$line[1], $line[17], $line[7]];	# 時間, シーケンス番号, サイズ
				push @tcp_packets, $ref;
			}
		}

	} elsif ($FLOW ne 'tcp') {

		# TCP以外 … スループットを逐次計算
		if ($line[1] - $clock > $INTERVAL) {
			if (exists $opts{'b'}) {
				$throughput = $sum_recv / $INTERVAL;
			} else {
				$throughput = $sum_recv * 8 / $INTERVAL;
			}
			print "$clock $throughput\n";
			$clock = $clock + $INTERVAL;
			$sum_recv = 0;
		}
		if ($line[0] eq 'r' && $line[2] eq $DST && $line[3] eq 'AGT' && $line[6] eq $FLOW) {
			# 受信時
			$sum_recv = $sum_recv + $line[7];
		}

	}

}
if ($FLOW eq 'tcp') {

	# TCP … @tcp_packets に格納されているデータからスループットを計算
	my $sum_recv = 0; my $throughput = 0; my $clock = 0;
	foreach (@tcp_packets) {
		if ($_->[0] - $clock > $INTERVAL) {
			if (exists $opts{'b'}) {
				$throughput = $sum_recv / $INTERVAL;
			} else {
				$throughput = $sum_recv * 8 / $INTERVAL;
			}
			print "$clock $throughput\n";
			$clock = $clock + $INTERVAL;
			$sum_recv = 0;
		}
		$sum_recv = $sum_recv + $_->[2];
	}

} else {

	# TCP以外 … 逐次計算の最後の1回
	if (exists $opts{'b'}) {
		$throughput = $sum_recv / $INTERVAL;
	} else {
		$throughput = $sum_recv * 8 / $INTERVAL;
	}
	print "$clock $throughput\n";

}

# ** 後処理
close (FILE);

# ** 関数: Usageの出力
sub usage {
	printf STDERR <<_OUT_;

Usage: throughput [-t TRACEFILE] [-h]
                  [-f FLOW] [-i INTERVAL] [-s SRC] [-d DST]

Where:
  -t FILE	トレースファイル FILE を指定します。
  -f FLOW	処理対象となるフローを指定します。(既定値：$FLOW)
  -i INTERVAL	集計間隔を指定します。(既定値：$INTERVAL)
  -s SRC	送信元ノードを指定します。(既定値：$SRC)
  -d DST	送信先ノードを指定します。(既定値：$DST)
  -b		出力をバイト毎秒に変更します。(指定のない時はbpsで出力)

  -h		このメッセージを出力して終了します。

_OUT_
}

__END__
