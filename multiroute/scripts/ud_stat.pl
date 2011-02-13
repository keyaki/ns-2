#!/usr/bin/perl

# Application/UserData用 パケット受信状況解析ツール
#
# Wada Laboratory, Shizuoka University
# Takahiro Sugimoto	<keyaki.no.kokage@gmail.com>
#
# Last Modified: 2008/12/12 12:39:37

use strict;
use warnings;

# 引数確認
if (@ARGV < 1) {
	# 引数なし
	printf STDERR "Usage: ud_stat [log file]\n";
	exit 1;
}

# ファイルを開く
open (DATA, "<" . $ARGV[0]) or die "$!: open failed";

# ファイル読み込みとデータ処理
my $count = 0; my $total = 0; my $dup = 0; my $prev = 0;my @rawdata; my @map = ();
while (<DATA>) {

	@rawdata = split(' ');
	#  0: 送信(s) / 受信(r)
	#  1: イベント発生時間 [s]
	#  2: パケット番号 
	#  3: 割当先ルート
	#  4: パケットサイズ [bytes]

	if ($rawdata[0] eq "s") {
		# パケット送信時
		my $ref = [ $rawdata[2], 0]; # パケット番号, 受信状況(初期値0)
		push @map, $ref;
		$total++;
	} elsif ($rawdata[0] eq "r") {
		# パケット受信時
		my $status;
		# 通し番号の記録と順番チェック
		if ($rawdata[2] > $prev) {
			# 正常な場合
			$status = 1;
		} else {
			# 通し番号が逆転している場合
			$status = 2;
		}
		# 状況の記録
		#foreach (@map) {
		foreach (reverse (@map)) {
			if ( $_->[0] == $rawdata[2] ) {
				if ( $_->[1] == 0 ) {
					$_->[1] = $status;
					$count++;
					last;
				} else {
					$dup++;
				}
			}
		}
		$prev = $rawdata[2];
	} else {
		print STDERR "invalid log entry!!\n";
		exit 1;
	}

}

# 処理結果の並べ替え
my @sorted = sort { $a->[0] <=> $b->[0] } @map;

# 解析結果表示
#  0 (x) = ドロップ
#  1 (o) = 正常に受信
#  2 (*) = 順番が狂って受信
my $ratio = $count / $total * 100;
print STDOUT "$count of $total packets delivered ($ratio %).\n\n";
print STDOUT "Block Map:\n";
print STDOUT "|--------|---------|---------|---------|---------|---------|---------|---------|\n";
my $i = 0;
foreach (@sorted) {
	$i++;
	if ($i%80 == 1 && $i != 1) { print STDOUT "\n"; }
	if ($_->[1] == 0) { print STDOUT "x"; }
	elsif ($_->[1] == 1) { print STDOUT "o"; }
	elsif ($_->[1] == 2) { print STDOUT "*"; }
}
print STDOUT "\n|--------|---------|---------|---------|---------|---------|---------|---------|\n";

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

