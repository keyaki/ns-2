#!/usr/bin/perl

# Application/UserData用 スループット計算ツール
#
# Wada Laboratory, Shizuoka University
# Takahiro Sugimoto	<keyaki.no.kokage@gmail.com>
#
# Last Modified: 2008/12/11 17:45:21

use strict;
use warnings;
use Getopt::Std;

# ** 引数処理
# 既定値
my $INTERVAL = 1.0;
# Getoptで処理
my %opts = ();
getopts('t:i:bh', \%opts);
if (exists $opts{'h'}) {
	&usage;
	exit 0;
}
unless (exists $opts{'t'}) {
	printf STDERR "ログファイルが指定されていません\n";
	&usage;
	exit 1;
}
if (exists $opts{'i'}) {
	$INTERVAL = $opts{'i'};
}

# ** 集計
open (FILE, "< $opts{'t'}") or die "ログファイル $opts{'t'} が開けません。($!)";

my $sum_recv = 0; my $throughput = 0; my $clock = 0;
my @checked_packets = ();
while (<FILE>) {

	my @line = split(' ', $_);
	#  0: 送信(s) / 受信(r)
	#  1: イベント発生時間 [s]
	#  2: パケット番号 
	#  3: 割当先ルート
	#  4: パケットサイズ [bytes]

	if ((exists $opts{'d'}) && !(exists $opts{'s'}) ) {

		# 重複・遅延パケットもスループットに算入 … 随時スループットを計算
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
		if ($line[0] eq 'r') {
			# 受信時
			$sum_recv = $sum_recv + $line[4];
		}

	} else {

		# 重複・遅延パケットの両方、または片方を除外 … 一旦checked_packetsに記憶し、スループット計算は後で
		my $current_id = 0;
		if ($line[0] eq 'r') {
			my $drop = 0;
			if (exists $opts{'d'}) {
				# 重複確認
				foreach (@checked_packets) {
					if ( $_->[1] == $line[2] ) {
						$drop = 1;
						last;
					}
				}
			}
			if (!exists $opts{'s'}) {
				# 遅延確認
				if ( $line[2] < $current_id ) {
					$drop = 1;
				} else {
					$current_id = $line[2];
				}
			}
			if ($drop != 1) {
				my $ref = [$line[1], $line[2], $line[4]];	# 時間, シーケンス番号, サイズ
				push @checked_packets, $ref;
			}
		}

	}

}
if (!(exists $opts{'d'}) || (exists $opts{'s'})) {

	# パケットチェックあり … @checked_packets に格納されているデータからスループットを計算
	my $sum_recv = 0; my $throughput = 0; my $clock = 0;
	foreach (@checked_packets) {
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

	# パケットチェックなし … 逐次計算の最後の1回
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

Usage: ud_throughput [-t TRACEFILE] [-i INTERVAL] [-b] [-h]

Where:
  -t FILE	ログファイル FILE を指定します。
  -i INTERVAL	集計間隔を指定します。(既定値：1.0)
  -b		出力をバイト毎秒に変更します。(指定のない時はbpsで出力)
  -s		遅れて到着したパケットをスループットに算入しません。(指定のないときは算入)
  -d		重複したパケットも除外せずスループットに算入します。(指定のないときは算入しません)

  -h		このメッセージを出力して終了します。

_OUT_
}

__END__

** 補足説明

ログファイル(UserDataLog)の例
-----
s 28.860000 1135 1 128
r 28.865381 1135 1 128
s 28.870000 1136 0 128
r 28.874359 1136 0 128
s 28.880000 1137 1 128
r 28.885321 1137 1 128
s 28.890000 1138 1 128
r 28.895361 1138 1 128
-----

[0] = 送信(s) / 受信(r)
[1] = 時間 [s]
[2] = パケットID     : userdata->packetID()
[3] = 割当先経路     : userdata->path()
　　　送信元のルーティングテーブルにおける経路番号であることに注意。
[4] = サイズ [bytes] : userdata->size()
　　　Application層でのパケットサイズ。下位レイヤのヘッダは含まれていない。

