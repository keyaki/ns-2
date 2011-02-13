#!/usr/bin/perl 

# CHKRREQ用 ホップ数集計ツール
# Last Modified: 2008/03/03 20:40:56

use strict;
use warnings;

#------------
# 設定
#------------
my $SRC = 0;
my $DST = 1;

#------------
# 作業用変数
#------------
my $tmp;
my @tmp;

#------------
# スクリプト
#------------

# 引数処理
if (@ARGV == 0) {
	print "Usage: parse.pl [filename]\n";
}

# 各行を処理しBCastID毎に整理しなおす
my @line;			# 行解析用
my $ref;			# 結果一時格納用
my @RREQs = ();		# 結果格納用
open (FILE, $ARGV[0]) or die "$!";
while (my $line = <FILE>) {

	# 1:index, 4:BCastID, 6:Dst, 8:Src, 12:HopCount, 14:Time
	@line = split(/ /, $line);
	if ($line[0] eq 'CHKRREQ:') {
		$line[4] =~ s/\#//; $line[14] =~ s/\n//;

		if ($line[2] eq 'sent') {
			if ($line[1] == $SRC && $line[6] == $DST && $line[8] eq 'him') {
				# 0:BCastID, 1:SentTime, 2:Hop, 3:Time, 4:Hops, 5:Times
				$ref = [$line[4], $line[14], 0, 0, '', ''];
				push @RREQs, $ref;
			}
		} elsif ($line[2] eq 'received') {
			if ($line[1] == $DST && $line[6] eq 'him' && $line[8] == $SRC) {
				# Hop, Time の更新
				$RREQs[$line[4]-1]->[2] = $line[12];
				$RREQs[$line[4]-1]->[3] = $line[14];
			}
		} elsif ($line[2] eq 'discarded') {
			if ($line[1] == $DST && $line[6] eq 'him' && $line[8] == $SRC) {
				# Hops, Times に追加
				$RREQs[$line[4]-1]->[4] .= "$line[12] ";
				$RREQs[$line[4]-1]->[5] .= "$line[14] ";
			}
		} 

	}

}
#print "-- Result of RREQs --\n";
#foreach(@RREQs) {
#	print "#$_->[0]: $_->[1], $_->[2], $_->[3], $_->[4], $_->[5]\n";
#}

# 構築されたルートのホップ数分布を集計
my %p_hops;
foreach(@RREQs) {
	if ($_->[2] != 0) {	# 構築に失敗した場合は無視 
		if (exists($p_hops{$_->[2]})) {
			$p_hops{$_->[2]}++;
		} else {
			$p_hops{$_->[2]} = 1;
		}
	}
}
print "-- Hops of established routes --\n";
foreach(keys(%p_hops)) {
	print "$_ -> $p_hops{$_}\n";
}

# 構築されたルートの遅延時間を集計 (そのまま羅列)
print "-- Delay times of established routes --\n";
foreach(@RREQs) {
	if ($_->[2] != 0) {	# 構築に失敗した場合は無視 
		printf("%.6f\n", $_->[3] - $_->[1]);
	}
}

# 無視されたルートの数の分布を集計
my %d_routes;
foreach(@RREQs) {
	if ($_->[2] != 0) {	# 構築に失敗した場合は無視
		@tmp = split(/ /, $_->[4]);
		my $d_route = @tmp - 1;
		if ($d_route == -1) { $d_route = 0; }
		if (exists($d_routes{$d_route})) {
			$d_routes{$d_route}++;
		} else {
			$d_routes{$d_route} = 1;
		}
	}
}
print "-- Number of discarded routes --\n";
foreach(keys(%d_routes)) {
	print "$_ -> $d_routes{$_}\n";
}

# 無視されたルートのホップ数分布を集計
my %d_hops;
foreach(@RREQs) {
	if ($_->[2] != 0) {	# 構築に失敗した場合は無視
		@tmp = split(/ /, $_->[4]);
		foreach my $tmp2 (@tmp) {
			if ($tmp2 ne '') {	# 末端の空白対策
				if (exists($d_hops{$tmp2})) {
					$d_hops{$tmp2}++;
				} else {
					$d_hops{$tmp2} = 1;
				}
			}
		}
	}
}
print "-- Hops of discarded routes --\n";
foreach(keys(%d_hops)) {
	print "$_ -> $d_hops{$_}\n";
}

# 無視されたルートの遅延時間を集計 (そのまま羅列)
print "-- Delay times of discarded routes --\n";
foreach(@RREQs) {
	if ($_->[2] != 0) {	# 構築に失敗した場合は無視 
		@tmp = split(/ /, $_->[5]);
		foreach my $tmp2 (@tmp) {
			if ($tmp2 ne '') {	# 末端の空白対策
				printf("%.6f\n", $tmp2 - $_->[1]);
			}
		}
	}
}
