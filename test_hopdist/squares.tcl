# large1.tcl - 2nd RREQ のホップ数調査用シナリオ (正方格子)
#
# References:
#  tcl/ex/wireless-mitf.tcl 
#  http://www-sop.inria.fr/mistral/personnel/Eitan.Altman/ns.htm
#
# Last Modified: 2008/02/04 19:57:11

#------------------------
# シミュレーション条件
#------------------------
set val(chan)           Channel/WirelessChannel    ;# channel type
set val(prop)           Propagation/TwoRayGround   ;# radio-propagation model
set val(netif)          Phy/WirelessPhy            ;# network interface type
set val(mac)            Mac/802_11                 ;# MAC type
set val(ifq)            Queue/DropTail/PriQueue    ;# interface queue type
set val(ll)             LL                         ;# link layer type
set val(ant)            Antenna/OmniAntenna        ;# antenna model
set val(ifqlen)         50                         ;# max packet in ifq
set val(n)              10                         ;# number of mobilenodes in a row
set val(nn)             [expr $val(n) * $val(n)]   ;# number of mobilenodes
set val(rp)             AODV                       ;# routing protocol
set val(x)              1000  			   ;# X dimension of topography
set val(y)              1000  			   ;# Y dimension of topography  
set val(stop)		150			   ;# time of simulation end

#------------------------
# シミュレータの設定
#------------------------
set ns		  [new Simulator]
set tracefd       [open squares.tr w]	;# トレースファイル出力先
set namtrace      [open squares.nam w]	;# nam用トレースファイル出力先

$ns trace-all $tracefd
$ns namtrace-all-wireless $namtrace $val(x) $val(y)

# set up topography object
set topo       [new Topography]

$topo load_flatgrid $val(x) $val(y)

create-god $val(nn)

#------------------------
# ノードの設定
#------------------------

# New API to config node: 
# 1. Create channel (or multiple-channels);
# 2. Specify channel in node-config (instead of channelType);
# 3. Create nodes for simulations.

# Create channel #1 and #2
# node_(nn) can also be created with the same configuration, or with a different
# channel specified.
set chan_1_ [new $val(chan)]
# set chan_2_ [new $val(chan)]

# Create nn mobilenodes [$val(nn)] and attach them to the channel. 
$ns node-config -adhocRouting $val(rp) \
		 -llType $val(ll) \
		 -macType $val(mac) \
		 -ifqType $val(ifq) \
		 -ifqLen $val(ifqlen) \
		 -antType $val(ant) \
		 -propType $val(prop) \
		 -phyType $val(netif) \
		 -topoInstance $topo \
		 -agentTrace ON \
		 -routerTrace ON \
		 -macTrace OFF \
		 -movementTrace ON \
		 -channel $chan_1_ 

# ノードの生成・初期位置の計算
# Uncomment below two lines will create node_(nn) with a different channel.
#  $ns node-config \
#		 -channel $chan_2_ 

# 送信元 node_(0)、送信先 node_(1)のみ別扱い
set node_(0) [$ns node]
#$node_(0) set X_ 0.0
$node_(0) set X_ [expr ($val(x) / $val(n)) / 2]
#$node_(0) set Y_ 0.0
$node_(0) set Y_ [expr ($val(y) / $val(n)) / 2]
$node_(0) set Z_ 0.0
$ns initial_node_pos $node_(0) 10
set node_(1) [$ns node]
#$node_(1) set X_ $val(x)
$node_(1) set X_ [expr $val(x) - ($val(x) / $val(n)) / 2]
#$node_(1) set Y_ $val(y)
$node_(1) set Y_ [expr $val(y) - ($val(y) / $val(n)) / 2]
$node_(1) set Z_ 0.0
$ns initial_node_pos $node_(1) 10

# 残りを配置
for {set i 2} {$i < $val(nn)} { incr i } {
	set node_($i) [$ns node]
	$node_($i) set X_ [expr ($val(x) / $val(n)) / 2 + (($i - 1) % $val(n)) * ($val(x) / $val(n))]
	$node_($i) set Y_ [expr ($val(y) / $val(n)) / 2 + (int(($i - 1) / $val(n))) * ($val(y) / $val(n))]
	$node_($i) set Z_ 0.0 
	# Define node initial position in nam
	# 30 defines the node size for nam
	$ns initial_node_pos $node_($i) 10
}

# Set a TCP connection between node_(0) and node_(1)
set tcp [new Agent/TCP/Newreno]
$tcp set class_ 2
set sink [new Agent/TCPSink]
$ns attach-agent $node_(0) $tcp
$ns attach-agent $node_(1) $sink
$ns connect $tcp $sink
set ftp [new Application/FTP]
$ftp attach-agent $tcp
$ns at 5.0 "$ftp start" 

# Telling nodes when the simulation ends
for {set i 0} {$i < $val(nn) } { incr i } {
    $ns at $val(stop) "$node_($i) reset";
}

# ending nam and the simulation 
$ns at $val(stop) "$ns nam-end-wireless $val(stop)"
$ns at $val(stop) "stop"
$ns at 150.01 "puts \"end simulation\" ; $ns halt"
proc stop {} {
    global ns tracefd namtrace
    $ns flush-trace
    close $tracefd
    close $namtrace
}

$ns run

