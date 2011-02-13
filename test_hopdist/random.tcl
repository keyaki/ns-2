# random.tcl - ホップ数調査用シナリオ (一様分布)
# original: http://www-sop.inria.fr/mistral/personnel/Eitan.Altman/ns.htm
#
# Last Modified: 2008/02/04 19:49:12

# シミュレーション条件
set val(chan)           Channel/WirelessChannel    ;# channel type
set val(prop)           Propagation/TwoRayGround   ;# radio-propagation model
set val(netif)          Phy/WirelessPhy            ;# network interface type
set val(mac)            Mac/802_11                 ;# MAC type
set val(ifq)            Queue/DropTail/PriQueue    ;# interface queue type
set val(ll)             LL                         ;# link layer type
set val(ant)            Antenna/OmniAntenna        ;# antenna model
set val(ifqlen)         50                         ;# max packet in ifq
set val(nn)             100                        ;# number of mobilenodes
set val(rp)             AODV                       ;# routing protocol
set val(x)              1000   			   ;# X dimension of topography
set val(y)              1000   			   ;# Y dimension of topography  
set val(stop)		150			   ;# time of simulation end

set ns		  [new Simulator]
set tracefd       [open random.tr w]
set namtrace      [open random.nam w]    

$ns trace-all $tracefd
$ns namtrace-all-wireless $namtrace $val(x) $val(y)

# set up topography object
set topo       [new Topography]

$topo load_flatgrid $val(x) $val(y)

create-god $val(nn)

#
#  Create nn mobilenodes [$val(nn)] and attach them to the channel. 
#

# ノードの設定
        $ns node-config -adhocRouting $val(rp) \
			 -llType $val(ll) \
			 -macType $val(mac) \
			 -ifqType $val(ifq) \
			 -ifqLen $val(ifqlen) \
			 -antType $val(ant) \
			 -propType $val(prop) \
			 -phyType $val(netif) \
			 -channelType $val(chan) \
			 -topoInstance $topo \
			 -agentTrace ON \
			 -routerTrace ON \
			 -macTrace OFF \
			 -movementTrace ON

# ノードの生成・初期位置の計算
# 送信元 node_(0), 送信先 node_(1) のみ独立して生成
set node_(0) [$ns node]
$node_(0) set X_ 0.0
$node_(0) set Y_ 0.0
$node_(0) set Z_ 0.0
set node_(1) [$ns node]
$node_(1) set X_ $val(x)
$node_(1) set Y_ $val(y)
$node_(1) set Z_ 0.0
# node_(2) 以降はランダムに配置 (一様分布)
set rng [new RNG]
$rng seed 0
set rnd [new RandomVariable/Uniform]
$rnd use-rng $rng
$rnd set min_ 0.0
for {set i 2} {$i < $val(nn) } { incr i } {
	set node_($i) [$ns node]
	# x
	$rnd set max_ $val(x)
	set random [expr [$rnd value]]
	$node_($i) set X_ $random
	# y
	$rnd set max_ $val(y)
	set random [expr [$rnd value]]
	$node_($i) set Y_ $random
	# z
	$node_($i) set Z_ 0.0
}
# Define node initial position in nam
for {set i 0} {$i < $val(nn)} { incr i } {
	# 30 defines the node size for nam
	$ns initial_node_pos $node_($i) 30
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

