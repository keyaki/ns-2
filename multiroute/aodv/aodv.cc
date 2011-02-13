/*
Copyright (c) 1997, 1998 Carnegie Mellon University.  All Rights
Reserved. 

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice,
this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
this list of conditions and the following disclaimer in the documentation
and/or other materials provided with the distribution.
3. The name of the author may not be used to endorse or promote products
derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

The AODV code developed by the CMU/MONARCH group was optimized and tuned by Samir Das and Mahesh Marina, University of Cincinnati. The work was partially done in Sun Microsystems. Modified for gratuitous replies by Anant Utgikar, 09/16/02.

*/

/*
 * AODV マルチルート拡張 (UserData連携ver.)
 * Last Modified: 2008/12/11 18:48:14
 */

//#include <ip.h>

#include <aodv/aodv.h>
#include <aodv/aodv_packet.h>
#include <random.h>
#include <cmu-trace.h>
//#include <energy-model.h>

#define max(a,b)        ( (a) > (b) ? (a) : (b) )
#define CURRENT_TIME    Scheduler::instance().clock()

#define DEBUG
//#define ERROR

#ifdef DEBUG
static int extra_route_reply = 0;
static int limit_route_request = 0;
static int route_request = 0;
#endif


/*
  TCL Hooks
*/


int hdr_aodv::offset_;
static class AODVHeaderClass : public PacketHeaderClass {
public:
        AODVHeaderClass() : PacketHeaderClass("PacketHeader/AODV",
                                              sizeof(hdr_all_aodv)) {
	  bind_offset(&hdr_aodv::offset_);
	} 
} class_rtProtoAODV_hdr;

static class AODVclass : public TclClass {
public:
        AODVclass() : TclClass("Agent/AODV") {}
        TclObject* create(int argc, const char*const* argv) {
          assert(argc == 5);
          //return (new AODV((nsaddr_t) atoi(argv[4])));
	  return (new AODV((nsaddr_t) Address::instance().str2addr(argv[4])));
        }
} class_rtProtoAODV;


int
AODV::command(int argc, const char*const* argv) {
  if(argc == 2) {
  Tcl& tcl = Tcl::instance();

    if(strncasecmp(argv[1], "id", 2) == 0) {
      tcl.resultf("%d", index);
      return TCL_OK;
    }
    
    if(strncasecmp(argv[1], "start", 2) == 0) {
      btimer.handle((Event*) 0);

#ifndef AODV_LINK_LAYER_DETECTION
      htimer.handle((Event*) 0);
      ntimer.handle((Event*) 0);
#endif // LINK LAYER DETECTION

      rtimer.handle((Event*) 0);
      return TCL_OK;
     }               
  }
  else if(argc == 3) {

    if(strcmp(argv[1], "index") == 0) {
      index = atoi(argv[2]);
      return TCL_OK;
    }

    else if(strcmp(argv[1], "log-target") == 0 || strcmp(argv[1], "tracetarget") == 0) {
      logtarget = (Trace*) TclObject::lookup(argv[2]);
      if(logtarget == 0)
	return TCL_ERROR;
      return TCL_OK;
    }
    else if(strcmp(argv[1], "drop-target") == 0) {
    int stat = rqueue.command(argc,argv);
      if (stat != TCL_OK) return stat;
      return Agent::command(argc, argv);
    }
    else if(strcmp(argv[1], "if-queue") == 0) {
    ifqueue = (PriQueue*) TclObject::lookup(argv[2]);

      if(ifqueue == 0)
	return TCL_ERROR;
      return TCL_OK;
    }
    else if (strcmp(argv[1], "port-dmux") == 0) {
    	dmux_ = (PortClassifier *)TclObject::lookup(argv[2]);
	if (dmux_ == 0) {
		fprintf (stderr, "%s: %s lookup of %s failed\n", __FILE__,
		argv[1], argv[2]);
		return TCL_ERROR;
	}
	return TCL_OK;
    }
  }
  return Agent::command(argc, argv);
}

/* 
   Constructor
*/

AODV::AODV(nsaddr_t id) : Agent(PT_AODV),
			  btimer(this), htimer(this), ntimer(this), 
			  rtimer(this), lrtimer(this), rqueue() {
 
	index = id;
	seqno = 2;
	bid = 1;

	LIST_INIT(&nbhead);
	LIST_INIT(&bihead);

	logtarget = 0;
	ifqueue = 0;

#ifdef AODV_USERDATA_CONNECT
	rt_request_status_ = 0;
#endif // AODV_USERDATA_CONNECT

}

/*
  Timers
*/

void
BroadcastTimer::handle(Event*) {
  agent->id_purge();
  Scheduler::instance().schedule(this, &intr, BCAST_ID_SAVE);
}

void
HelloTimer::handle(Event*) {
   agent->sendHello();
   double interval = MinHelloInterval + 
                 ((MaxHelloInterval - MinHelloInterval) * Random::uniform());
   assert(interval >= 0);
   Scheduler::instance().schedule(this, &intr, interval);
}

void
NeighborTimer::handle(Event*) {
  agent->nb_purge();
  Scheduler::instance().schedule(this, &intr, HELLO_INTERVAL);
}

void
RouteCacheTimer::handle(Event*) {
  agent->rt_purge();
#define FREQUENCY 0.5 // sec
  Scheduler::instance().schedule(this, &intr, FREQUENCY);
}

void
LocalRepairTimer::handle(Event* p)  {  // SRD: 5/4/99
aodv_rt_entry *rt;
struct hdr_ip *ih = HDR_IP( (Packet *)p);

   /* you get here after the timeout in a local repair attempt */
   /*	fprintf(stderr, "%s\n", __FUNCTION__); */


    rt = agent->rtable.rt_lookup(ih->daddr());
	
#ifndef AODV_MULTIROUTE
    if (rt && rt->rt_flags != RTF_UP) {
#else // AODV_MULTIROUTE
    if (rt && (rt->rt_flags != RTF_UP || rt->rt_flags != RTF_WAIT)) {
#endif // AODV_MULTIROUTE
    // route is yet to be repaired
    // I will be conservative and bring down the route
    // and send route errors upstream.
    /* The following assert fails, not sure why */
    /* assert (rt->rt_flags == RTF_IN_REPAIR); */
		
      //rt->rt_seqno++;
      agent->rt_down(rt);
      // send RERR
#ifdef DEBUG
      fprintf(stderr,"Node %d: Dst - %d, failed local repair\n",index, rt->rt_dst);
#endif      
    }
    Packet::free((Packet *)p);
}


/*
   Broadcast ID Management  Functions
*/


void
AODV::id_insert(nsaddr_t id, u_int32_t bid) {
BroadcastID *b = new BroadcastID(id, bid);

 assert(b);
 b->expire = CURRENT_TIME + BCAST_ID_SAVE;
 LIST_INSERT_HEAD(&bihead, b, link);
}

/* SRD */
bool
AODV::id_lookup(nsaddr_t id, u_int32_t bid) {
BroadcastID *b = bihead.lh_first;
 
 // Search the list for a match of source and bid
 for( ; b; b = b->link.le_next) {
   if ((b->src == id) && (b->id == bid))
     return true;     
 }
 return false;
}

void
AODV::id_purge() {
BroadcastID *b = bihead.lh_first;
BroadcastID *bn;
double now = CURRENT_TIME;

 for(; b; b = bn) {
   bn = b->link.le_next;
   if(b->expire <= now) {
     LIST_REMOVE(b,link);
     delete b;
   }
 }
}

/*
  Helper Functions
*/

double
AODV::PerHopTime(aodv_rt_entry *rt) {
int num_non_zero = 0, i;
double total_latency = 0.0;

 if (!rt)
   return ((double) NODE_TRAVERSAL_TIME );
	
 for (i=0; i < MAX_HISTORY; i++) {
   if (rt->rt_disc_latency[i] > 0.0) {
      num_non_zero++;
      total_latency += rt->rt_disc_latency[i];
   }
 }
 if (num_non_zero > 0)
   return(total_latency / (double) num_non_zero);
 else
   return((double) NODE_TRAVERSAL_TIME);

}

/*
  Link Failure Management Functions
*/

static void
aodv_rt_failed_callback(Packet *p, void *arg) {
  ((AODV*) arg)->rt_ll_failed(p);
}

/*
 * This routine is invoked when the link-layer reports a route failed.
 */
void
AODV::rt_ll_failed(Packet *p) {
struct hdr_cmn *ch = HDR_CMN(p);
struct hdr_ip *ih = HDR_IP(p);
aodv_rt_entry *rt;
nsaddr_t broken_nbr = ch->next_hop_;

#ifndef AODV_LINK_LAYER_DETECTION
 drop(p, DROP_RTR_MAC_CALLBACK);
#else 

 /*
  * Non-data packets and Broadcast Packets can be dropped.
  */
  if(! DATA_PACKET(ch->ptype()) ||
     (u_int32_t) ih->daddr() == IP_BROADCAST) {
    drop(p, DROP_RTR_MAC_CALLBACK);
    return;
  }
  log_link_broke(p);
	if((rt = rtable.rt_lookup(ih->daddr())) == 0) {
    drop(p, DROP_RTR_MAC_CALLBACK);
    return;
  }
  log_link_del(ch->next_hop_);

#ifdef AODV_LOCAL_REPAIR
  /* if the broken link is closer to the dest than source, 
     attempt a local repair. Otherwise, bring down the route. */


  if (ch->num_forwards() > rt->rt_hops) {
    local_rt_repair(rt, p); // local repair
    // retrieve all the packets in the ifq using this link,
    // queue the packets for which local repair is done, 
    return;
  }
  else	
#endif // LOCAL REPAIR	

  {
    drop(p, DROP_RTR_MAC_CALLBACK);
    // Do the same thing for other packets in the interface queue using the
    // broken link -Mahesh
while((p = ifqueue->filter(broken_nbr))) {
     drop(p, DROP_RTR_MAC_CALLBACK);
    }	
    nb_delete(broken_nbr);
  }

#endif // LINK LAYER DETECTION
}

void
AODV::handle_link_failure(nsaddr_t id) {
aodv_rt_entry *rt, *rtn;
Packet *rerr = Packet::alloc();
struct hdr_aodv_error *re = HDR_AODV_ERROR(rerr);

 re->DestCount = 0;
 for(rt = rtable.head(); rt; rt = rtn) { // for each rt entry
 /*
  * 切断したリンクを利用した経路が無いかルーティングテーブルの各エントリを調べる。
  *
  * AODV_MULTIROUTEでは、次のような処理を行う。
  *  1. マルチルート用ルーティングテーブルの検索・削除処理
  *  2. 通常のルーティングテーブルに対する検索・削除処理
  *  3. 通常のルーティングテーブルの内容が削除され、かつマルチルート用テーブルに
  *     エントリが残っていたら、マルチルート用のルートの一つをメインに昇格
  *     残っていなかったらRTF_DOWN。
  *
  * ※類似の処理がrecvError()にもあり。
  */ 

	rtn = rt->rt_link.le_next;

	/*
	 * マルチルート用ルーティングテーブルに対する処理
	 */ 
#ifdef AODV_MULTIROUTE
	u_int8_t j;
	for (j=0; j<rt->rt_routes; j++) {
		if (rt->rt_m_nexthop[j] == id) {
#ifdef DEBUG
		fprintf(stderr, "AODV %d %f m remove the multi table to %d via %d with %d hop(s)\n",
				index, CURRENT_TIME, rt->rt_dst, j, rt->rt_m_hops[j]);
#endif // DEBUG
                rt->rt_m_delete(id);
		if( (rt->rt_hops == INFINITY2) && (rt->rt_nexthop != id) ) {
			// ルーティングパケット関係の後処理。
			// nexthop == id の時は通常のルーティングテーブルに対する処理のときに行う。
			assert((rt->rt_seqno%2) == 0);
			rt->rt_seqno++;
			re->unreachable_dst[re->DestCount] = rt->rt_dst;
			re->unreachable_dst_seqno[re->DestCount] = rt->rt_seqno;
#ifdef DEBUG
			fprintf(stderr, "%s(%f): %d\t(%d\t%u\t%d)\n", __FUNCTION__, CURRENT_TIME,
					index, re->unreachable_dst[re->DestCount],
					re->unreachable_dst_seqno[re->DestCount], rt->rt_nexthop);
#endif // DEBUG
			re->DestCount += 1;
		}
           }
        }
#endif // AODV_MULTIROUTE

	/*
	 * 通常のルーティングテーブルに対する処理
	 */
	if ((rt->rt_hops != INFINITY2) && (rt->rt_nexthop == id) ) {
#ifndef AODV_MULTIROUTE
	assert (rt->rt_flags == RTF_UP);
#else // AODV_MULTIROUTE
	assert (rt->rt_flags == RTF_UP || rt->rt_flags == RTF_WAIT);
#endif // AODV_MULTIROUTE
	assert((rt->rt_seqno%2) == 0);
	rt->rt_seqno++;
	re->unreachable_dst[re->DestCount] = rt->rt_dst;
	re->unreachable_dst_seqno[re->DestCount] = rt->rt_seqno;
#ifdef DEBUG
	fprintf(stderr, "%s(%f): %d\t(%d\t%u\t%d)\n", __FUNCTION__, CURRENT_TIME,
			index, re->unreachable_dst[re->DestCount],
			re->unreachable_dst_seqno[re->DestCount], rt->rt_nexthop);
#endif // DEBUG
	re->DestCount += 1;
#ifndef AODV_MULTIROUTE	// マルチルートではdownは保留	
	rt_down(rt);
#endif // AODV_MULTIROUTE
	}
	// remove the lost neighbor from all the precursor lists
	rt->pc_delete(id);

	/*
	 * マルチルート関連の残りの処理
	 */
#ifdef AODV_MULTIROUTE
	if (rt->rt_routes == 0) {
	// マルチルート用ルーティングテーブルのルート数が0だったらdown
#ifdef DEBUG
		fprintf(stderr, "MULTIROUTE: %s: %d's multiple routing table is empty, down at %f.\n",
				__FUNCTION__, index, CURRENT_TIME);
#endif // DEBUG
		rt_down(rt);
	} else if (rt->rt_nexthop == id) {
	// エントリ数が0でなく、今回削除したのがメインルートなら、サブルートをメインに
#ifdef DEBUG
		fprintf(stderr, "MULTIROUTE: %s: %d's multiple routing table is not empty, salvaged at %f.\n",
				__FUNCTION__, index, CURRENT_TIME);
		//rt->rt_m_dump();
#endif // DEBUG
		rt->rt_hops = rt->rt_m_hops[0];
		rt->rt_nexthop = rt->rt_m_nexthop[0];
	}
#endif // AODV_MULTIROUTE

 }   

 if (re->DestCount > 0) {
	// 削除したエントリーがあったのでRERRを転送 (BROADCAST)
#ifdef DEBUG
	fprintf(stderr, "%s(%f): %d\tsending RERR...\n", __FUNCTION__, CURRENT_TIME, index);
#endif // DEBUG
	sendError(rerr, false);
 } else {
	Packet::free(rerr);
 }

}

void
AODV::local_rt_repair(aodv_rt_entry *rt, Packet *p) {
#ifdef DEBUG
  fprintf(stderr,"%s: Dst - %d\n", __FUNCTION__, rt->rt_dst); 
#endif  
  // Buffer the packet 
  rqueue.enque(p);

  // mark the route as under repair 
  rt->rt_flags = RTF_IN_REPAIR;

  sendRequest(rt->rt_dst);

  // set up a timer interrupt
  Scheduler::instance().schedule(&lrtimer, p->copy(), rt->rt_req_timeout);
}

void
AODV::rt_update(aodv_rt_entry *rt, u_int32_t seqnum, u_int16_t metric,
	       	nsaddr_t nexthop, double expire_time) {

#ifdef DEBUG
	fprintf(stderr, "AODV %d %f t update the master table to %d via %d with %d hop(s)\n",
		index, CURRENT_TIME, rt->rt_dst, nexthop, metric);
#endif // DEBUG

     rt->rt_seqno = seqnum;
     rt->rt_hops = metric;
#ifndef AODV_MULTIROUTE
     rt->rt_flags = RTF_UP;
#else // AODV_MULTIROUTE
	// 既にUPしている場合はWAITさせない
	if(rt->rt_flags != RTF_UP) rt->rt_flags = RTF_WAIT;
	rt->rt_m_create = CURRENT_TIME; 
#endif // AODV_MULTIROUTE
     rt->rt_nexthop = nexthop;
     rt->rt_expire = expire_time;
}

#ifdef AODV_MULTIROUTE
/* RTF_UPへの切替 */
void
AODV::rt_up(aodv_rt_entry *rt) {

#ifdef DEBUG
	fprintf(stderr, "AODV %d %f t RTF_UP for %d\n",
		index, CURRENT_TIME, rt->rt_dst);
#endif // DEBUG

	rt->rt_flags = RTF_UP;
}
#endif // AODV_MULTIROUTE

void
AODV::rt_down(aodv_rt_entry *rt) {
  /*
   *  Make sure that you don't "down" a route more than once.
   */

	if(rt->rt_flags == RTF_DOWN) {
		return;
	}

#ifdef DEBUG
	fprintf(stderr, "AODV %d %f t RTF_DOWN for %d\n",
		index, CURRENT_TIME, rt->rt_dst);
#endif // DEBUG

	// assert (rt->rt_seqno%2); // is the seqno odd?
	rt->rt_last_hop_count = rt->rt_hops;
	rt->rt_hops = INFINITY2;
	rt->rt_flags = RTF_DOWN;
	rt->rt_nexthop = 0;
	rt->rt_expire = 0;

#ifdef AODV_MULTIROUTE
	rt->rt_m_hops[0] = INFINITY2;
	rt->rt_m_nexthop[0] = 0;
	rt->rt_routes = 0;
	rt->rt_counter = 0;
	rt->rt_m_create = 0;
#endif // AODV_MULTIROUTE

#ifdef AODV_USERDATA_CONNECT
	rt_request_status_ = 0;
#endif // AODV_USERDATA_CONNECT

} /* rt_down function */

/*
  Route Handling Functions
*/

void
AODV::rt_resolve(Packet *p) {
struct hdr_cmn *ch = HDR_CMN(p);
struct hdr_ip *ih = HDR_IP(p);
aodv_rt_entry *rt;

 /*
  *  Set the transmit failure callback.  That
  *  won't change.
  */
 ch->xmit_failure_ = aodv_rt_failed_callback;
 ch->xmit_failure_data_ = (void*) this;
	rt = rtable.rt_lookup(ih->daddr());
 if(rt == 0) {
	  rt = rtable.rt_add(ih->daddr());
 }

 /*
  * If the route is up, forward the packet 
  */
	
 if(rt->rt_flags == RTF_UP) {
   assert(rt->rt_hops != INFINITY2);
   forward(rt, p, NO_DELAY);
 }
#ifdef AODV_MULTIROUTE
 /*
  * 追加のRREPパケットを待っている送信元ノードの場合、パケットをバッファする。
  */
 else if (ih->saddr() == index && rt->rt_flags == RTF_WAIT) {
	rqueue.enque(p);
 }
 /*
  * 中継ノードの場合、RTF_WAIT状態でも送信する。
  */
 else if (ih->saddr() != index && rt->rt_flags == RTF_WAIT) {
	assert(rt->rt_hops != INFINITY2);
	forward(rt, p, NO_DELAY);
 }
#endif // AODV_MULTIROUTE
 /*
  *  if I am the source of the packet, then do a Route Request.
  */
 else if(ih->saddr() == index) {
   rqueue.enque(p);
   sendRequest(rt->rt_dst);
 }
 /*
  *	A local repair is in progress. Buffer the packet. 
  */
 else if (rt->rt_flags == RTF_IN_REPAIR) {
   rqueue.enque(p);
 }

 /*
  * I am trying to forward a packet for someone else to which
  * I don't have a route.
  */
 else {
 Packet *rerr = Packet::alloc();
 struct hdr_aodv_error *re = HDR_AODV_ERROR(rerr);
 /* 
  * For now, drop the packet and send error upstream.
  * Now the route errors are broadcast to upstream
  * neighbors - Mahesh 09/11/99
  */	
 
   assert (rt->rt_flags == RTF_DOWN);
   re->DestCount = 0;
   re->unreachable_dst[re->DestCount] = rt->rt_dst;
   re->unreachable_dst_seqno[re->DestCount] = rt->rt_seqno;
   re->DestCount += 1;
#ifdef DEBUG
   fprintf(stderr, "%s: %d sending RERR...\n", __FUNCTION__, index);
#endif
   sendError(rerr, false);

   drop(p, DROP_RTR_NO_ROUTE);
 }

}

void
AODV::rt_purge() {
aodv_rt_entry *rt, *rtn;
double now = CURRENT_TIME;
double delay = 0.0;
Packet *p;

 /*
  * RouteCacheTimer::handle() から FREQUENCY おきに呼び出される
  */

 for(rt = rtable.head(); rt; rt = rtn) {  // for each rt entry
	rtn = rt->rt_link.le_next;
#ifndef AODV_MULTIROUTE
	if ((rt->rt_flags == RTF_UP) && (rt->rt_expire < now)) {
#else // AODV_MULTIROUTE
	// マルチルート有効時には、RTF_WAIT中のルートに対しても同様にexpire処理する。
	if ((rt->rt_flags == RTF_UP || rt->rt_flags == RTF_WAIT) && (rt->rt_expire < now)) {
#endif // AODV_MULTIROUTE
		// if a valid route has expired, purge all packets from 
		// send buffer and invalidate the route.                    
		assert(rt->rt_hops != INFINITY2);
		while((p = rqueue.deque(rt->rt_dst))) {
#ifdef DEBUG
			fprintf(stderr, "%s: calling drop()\n",
					__FUNCTION__);
#endif // DEBUG
			drop(p, DROP_RTR_NO_ROUTE);
		}
		rt->rt_seqno++;
		assert (rt->rt_seqno%2);
		rt_down(rt);
	}
#ifdef AODV_MULTIROUTE
#ifdef MULTIROUTE_TIMEOUT
	// MULTIROUTE_TIMEOUTが指定されている時には、指定本数のルートが構築できていなくても、
	// 現在あるルートだけでRTF_UPにする。
	else if ((rt->rt_flags == RTF_WAIT) && (rt->rt_m_create + MULTIROUTE_TIMEOUT  < now)) {

#ifdef DEBUG
		fprintf(stderr, "MULTIROUTE: %s: %d's RTF_WAIT for %d timeout at %f\n",
			__FUNCTION__, index, rt->rt_dst, CURRENT_TIME);
#endif // DEBUG
		rt_up(rt);

#ifdef AODV_USERDATA_CONNECT
		userdata_callback(rt->rt_dst);
#endif // AODV_USERDATA_CONNECT
		while((p = rqueue.deque(rt->rt_dst))) {
			forward (rt, p, delay);
			delay += ARP_DELAY;
		}

	} 
#endif // MULTIROUTE_TIMEOUT
#endif // AODV_MULTIROUTE
	else if (rt->rt_flags == RTF_UP) {
		// If the route is not expired,
		// and there are packets in the sendbuffer waiting,
		// forward them. This should not be needed, but this extra 
		// check does no harm.
		assert(rt->rt_hops != INFINITY2);
		while((p = rqueue.deque(rt->rt_dst))) {
			forward (rt, p, delay);
			delay += ARP_DELAY;
		}
	}
	else if (rqueue.find(rt->rt_dst)) {
		// If the route is down and 
		// if there is a packet for this destination waiting in
		// the sendbuffer, then send out route request. sendRequest
		// will check whether it is time to really send out request
		// or not.
		// This may not be crucial to do it here, as each generated 
		// packet will do a sendRequest anyway.

#ifdef DEBUG
		fprintf(stderr, "%s: %d calling sendRequest(%d)\n", __FUNCTION__, index, rt->rt_dst);
#endif // DEBUG

		sendRequest(rt->rt_dst); 
	}
#ifdef AODV_USERDATA_CONNECT
	else {
		userdata_fail_callback(rt->rt_dst);
	}
#endif // AODV_USERDATA_CONNECT

 }

}

/*
  Packet Reception Routines
*/

void
AODV::recv(Packet *p, Handler*) {
struct hdr_cmn *ch = HDR_CMN(p);
struct hdr_ip *ih = HDR_IP(p);

 assert(initialized());
 //assert(p->incoming == 0);
 // XXXXX NOTE: use of incoming flag has been depracated; In order to track direction of pkt flow, direction_ in hdr_cmn is used instead. see packet.h for details.

 if(ch->ptype() == PT_AODV) {
   ih->ttl_ -= 1;
   recvAODV(p);
   return;
 }


 /*
  *  Must be a packet I'm originating...
  */
if((ih->saddr() == index) && (ch->num_forwards() == 0)) {
 /*
  * Add the IP Header
  */
   ch->size() += IP_HDR_LEN;
   // Added by Parag Dadhania && John Novatnack to handle broadcasting
   if ( (u_int32_t)ih->daddr() != IP_BROADCAST)
     ih->ttl_ = NETWORK_DIAMETER;
}
 /*
  *  I received a packet that I sent.  Probably
  *  a routing loop.
  */
else if(ih->saddr() == index) {
   drop(p, DROP_RTR_ROUTE_LOOP);
   return;
 }
 /*
  *  Packet I'm forwarding...
  */
 else {
 /*
  *  Check the TTL.  If it is zero, then discard.
  */
   if(--ih->ttl_ == 0) {
     drop(p, DROP_RTR_TTL);
     return;
   }
 }
// Added by Parag Dadhania && John Novatnack to handle broadcasting
 if ( (u_int32_t)ih->daddr() != IP_BROADCAST)
   rt_resolve(p);
 else
   forward((aodv_rt_entry*) 0, p, NO_DELAY);
}


void
AODV::recvAODV(Packet *p) {
 struct hdr_aodv *ah = HDR_AODV(p);

 assert(HDR_IP (p)->sport() == RT_PORT);
 assert(HDR_IP (p)->dport() == RT_PORT);

 /*
  * Incoming Packets.
  */
 switch(ah->ah_type) {

 case AODVTYPE_RREQ:
   recvRequest(p);
   break;

 case AODVTYPE_RREP:
   recvReply(p);
   break;

 case AODVTYPE_RERR:
   recvError(p);
   break;

 case AODVTYPE_HELLO:
   recvHello(p);
   break;
        
 default:
   fprintf(stderr, "Invalid AODV type (%x)\n", ah->ah_type);
   exit(1);
 }

}


void
AODV::recvRequest(Packet *p) {
struct hdr_ip *ih = HDR_IP(p);
struct hdr_aodv_request *rq = HDR_AODV_REQUEST(p);
aodv_rt_entry *rt;

/*
 *  アドレス参照方法一覧
 *
 *   * 送信元		rq->rq_src
 *   |
 *   * 前ホップ		ih->saddr()
 *   |
 *   x 自ノード		index
 *   |
 *   * 次ホップ
 *   |
 *   * 送信先		rq->rq_dst
 */

#ifdef DEBUG
 fprintf(stderr, "AODV %d %f r RREQ #%d from %d to %d via %d\n",
		index, CURRENT_TIME, rq->rq_bcast_id, rq->rq_src, rq->rq_dst, ih->saddr());
#endif // DEBUG

 /*
  * Drop if:
  *      - I'm the source
  *      - I recently heard this request.
  *
  * dropさせる条件:
  *      - 自分が送信元の場合
  *      - そのRREQを受信したことがある場合
  *
  * ** AODV_MULTIROUTEにおいては、受信元では2番目の条件を無視する
  */

 if(rq->rq_src == index) {
	// 自分の送信したRREQの場合
#ifdef DEBUG
	//fprintf(stderr, "%s: %d got my own REQUEST\n", __FUNCTION__, index);
	fprintf(stderr, "AODV %d %f i RREQ #%d discarded own\n",
		index, CURRENT_TIME, rq->rq_bcast_id);
#endif // DEBUG
	Packet::free(p);
	return;
 } 

#ifndef AODV_MULTIROUTE
 // 重複RREQはdropさせる
 if (id_lookup(rq->rq_src, rq->rq_bcast_id)) {
#else // AODV_MULTIROUTE
 // AODV_MULTIROUTEでは、受信ノードのみ重複RREQを破棄しない
 if (rq->rq_dst != index && id_lookup(rq->rq_src, rq->rq_bcast_id)) {
#endif // AODV_MULTIROUTE

#ifdef DEBUG
	//fprintf(stderr, "%s: %d discarding request\n", __FUNCTION__, index);
	fprintf(stderr, "AODV %d %f i RREQ #%d discarded dup\n",
			index, CURRENT_TIME, rq->rq_bcast_id);
#endif // DEBUG
 
	Packet::free(p);
	return;
 }

 /*
  * Cache the broadcast ID (broadcast ID を記憶する)
  */
#ifdef AODV_MULTIROUTE
 if ( !id_lookup(rq->rq_src, rq->rq_bcast_id)) {	// 二重挿入回避
#endif // AODV_MULTIROUTE
 id_insert(rq->rq_src, rq->rq_bcast_id);
#ifdef AODV_MULTIROUTE
 }
#endif // AODV_MULTIROUTE

 /* 
  * We are either going to forward the REQUEST or generate a
  * REPLY. Before we do anything, we make sure that the REVERSE
  * route is in the route table.
  *
  * RREQの転送または、RREPの生成を行う。
  * どちらを行う場合でも、ルーティングテーブルに逆経路が確実に存在する状態にする。
  */
 aodv_rt_entry *rt0; // rt0 is the reverse route 
   
   rt0 = rtable.rt_lookup(rq->rq_src);
   if(rt0 == 0) { /* if not in the route table */
   // create an entry for the reverse route.
     rt0 = rtable.rt_add(rq->rq_src);
   }
  
   rt0->rt_expire = max(rt0->rt_expire, (CURRENT_TIME + REV_ROUTE_LIFE));

   if ( (rq->rq_src_seqno > rt0->rt_seqno ) ||
    	((rq->rq_src_seqno == rt0->rt_seqno) && 
	 (rq->rq_hop_count < rt0->rt_hops)) ) {
   // If we have a fresher seq no. or lesser #hops for the 
   // same seq no., update the rt entry. Else don't bother.
       rt_update(rt0, rq->rq_src_seqno, rq->rq_hop_count, ih->saddr(),
     	       max(rt0->rt_expire, (CURRENT_TIME + REV_ROUTE_LIFE)) );
     if (rt0->rt_req_timeout > 0.0) {
     // Reset the soft state and 
     // Set expiry time to CURRENT_TIME + ACTIVE_ROUTE_TIMEOUT
     // This is because route is used in the forward direction,
     // but only sources get benefited by this change
       rt0->rt_req_cnt = 0;
       rt0->rt_req_timeout = 0.0; 
       rt0->rt_req_last_ttl = rq->rq_hop_count;
       rt0->rt_expire = CURRENT_TIME + ACTIVE_ROUTE_TIMEOUT;
     }

     /* Find out whether any buffered packet can benefit from the 
      * reverse route.
      * May need some change in the following code - Mahesh 09/11/99
      */
#ifndef AODV_MULTIROUTE
     assert (rt0->rt_flags == RTF_UP);
#else // AODV_MULTIROUTE
     assert (rt0->rt_flags == RTF_UP || rt0->rt_flags == RTF_WAIT);
     if (rt0->rt_flags == RTF_UP) {	// 念のため
#endif // AODV_MULTIROUTE
     Packet *buffered_pkt;
     while ((buffered_pkt = rqueue.deque(rt0->rt_dst))) {
       if (rt0 && (rt0->rt_flags == RTF_UP)) {
	assert(rt0->rt_hops != INFINITY2);
         forward(rt0, buffered_pkt, NO_DELAY);
       }
     }
#ifdef AODV_MULTIROUTE
     }
#endif // AODV_MULTIROUTE
   } 
   // End for putting reverse route in rt table
   // 逆経路を追加する処理終了

#ifdef AODV_MULTIROUTE
	/*
	 *  マルチルート用ルーティングテーブルへの経路追加処理
	 */
	if ( rq->rq_dst != index ) {
		// 宛先ノードではない場合：追加処理はしない
#ifdef DEBUG
	//	fprintf(stderr, "MULTIROUTE: %s: %d skip request #%d\n",
	//		 __FUNCTION__, index, rq->rq_bcast_id);
	fprintf(stderr, "AODV %d %f m RREQ #%d ignored table nondst\n",
		index, CURRENT_TIME, rq->rq_bcast_id);
#endif // DEBUG
	} else if (! rt0->rt_m_add(ih->saddr(), rq->rq_hop_count)) {
		// ルーティングテーブルが満杯 / 既に同じルートが存在する場合：dropさせる
#ifdef DEBUG
	//	fprintf(stderr, "MULTIROUTE: %s: %d discarding request #%d, routing table is full or duplicate route.\n",
	//		 __FUNCTION__, index, rq->rq_bcast_id);
	fprintf(stderr, "AODV %d %f m RREQ #%d discarded table full/dup\n",
		index, CURRENT_TIME, rq->rq_bcast_id);
#endif // DEBUG
		Packet::free(p);
		return;
	} else {
		// 一つ上の条件式で経路を追加できた場合：経路数がROUTE_COUNTに達していれば経路をUP
		if(rt0->rt_routes >= ROUTE_COUNT) {
			rt_up(rt0);
			assert(rt0->rt_flags == RTF_UP);
			// 溜まっていたパケットの送信
			Packet *buf_pkt;
			while ((buf_pkt = rqueue.deque(rt0->rt_dst))) {
				if (rt0 && (rt0->rt_flags == RTF_UP)) {
					assert(rt0->rt_hops != INFINITY2);
					forward(rt0, buf_pkt, NO_DELAY);
				}
			}
		}
#ifdef DEBUG
	//	fprintf(stderr, "MULTIROUTE: %s: %d add a new route to %d via %d with hop count %d\n",
	//			__FUNCTION__, index, rq->rq_src, ih->saddr(), rq->rq_hop_count);
		fprintf(stderr, "AODV %d %f m RREQ #%d add table to %d via %d with %d hop(s)\n",
			index, CURRENT_TIME, rq->rq_bcast_id, rq->rq_src, ih->saddr(), rq->rq_hop_count);
		//rt0->rt_m_dump();
#endif // DEBUG
	}
#endif // AODV_MULTIROUTE

 /*
  * We have taken care of the reverse route stuff.
  * Now see whether we can send a route reply. 
  */

 rt = rtable.rt_lookup(rq->rq_dst);

 // First check if I am the destination ..

 if(rq->rq_dst == index) {

//#ifdef DEBUG
//   fprintf(stderr, "%d - %s: destination sending reply\n",
//                   index, __FUNCTION__);
//#endif // DEBUG

               
   // Just to be safe, I use the max. Somebody may have
   // incremented the dst seqno.
   seqno = max(seqno, rq->rq_dst_seqno)+1;
   if (seqno%2) seqno++;

   sendReply(rq->rq_src,           // IP Destination
             1,                    // Hop Count
             index,                // Dest IP Address
             seqno,                // Dest Sequence Num
             MY_ROUTE_TIMEOUT,     // Lifetime
             rq->rq_timestamp);    // timestamp
 
   Packet::free(p);
 }

 // I am not the destination, but I may have a fresh enough route.

 else if (rt && (rt->rt_hops != INFINITY2) && 
	  	(rt->rt_seqno >= rq->rq_dst_seqno) ) {

   //assert (rt->rt_flags == RTF_UP);
   assert(rq->rq_dst == rt->rt_dst);
   //assert ((rt->rt_seqno%2) == 0);	// is the seqno even?
   sendReply(rq->rq_src,
             rt->rt_hops + 1,
             rq->rq_dst,
             rt->rt_seqno,
	     (u_int32_t) (rt->rt_expire - CURRENT_TIME),
	     //             rt->rt_expire - CURRENT_TIME,
             rq->rq_timestamp);
   // Insert nexthops to RREQ source and RREQ destination in the
   // precursor lists of destination and source respectively
   rt->pc_insert(rt0->rt_nexthop); // nexthop to RREQ source
   rt0->pc_insert(rt->rt_nexthop); // nexthop to RREQ destination

#ifdef RREQ_GRAT_RREP  

   sendReply(rq->rq_dst,
             rq->rq_hop_count,
             rq->rq_src,
             rq->rq_src_seqno,
	     (u_int32_t) (rt->rt_expire - CURRENT_TIME),
	     //             rt->rt_expire - CURRENT_TIME,
             rq->rq_timestamp);
#endif
   
// TODO: send grat RREP to dst if G flag set in RREQ using rq->rq_src_seqno, rq->rq_hop_counT
   
// DONE: Included gratuitous replies to be sent as per IETF aodv draft specification. As of now, G flag has not been dynamically used and is always set or reset in aodv-packet.h --- Anant Utgikar, 09/16/02.

	Packet::free(p);
 }
 /*
  * Can't reply. So forward the  Route Request
  * (RREPを返せないので、RREQを転送する)
  */
 else {
#ifdef DEBUG
	fprintf(stderr, "AODV %d %f s RREQ #%d forward\n",
		index, CURRENT_TIME, rq->rq_bcast_id);
#endif // DEBUG
   ih->saddr() = index;
   ih->daddr() = IP_BROADCAST;
   rq->rq_hop_count += 1;
   // Maximum sequence number seen en route
   if (rt) rq->rq_dst_seqno = max(rt->rt_seqno, rq->rq_dst_seqno);
   forward((aodv_rt_entry*) 0, p, DELAY);
 }

}


void
AODV::recvReply(Packet *p) {
struct hdr_cmn *ch = HDR_CMN(p);
struct hdr_ip *ih = HDR_IP(p);
struct hdr_aodv_reply *rp = HDR_AODV_REPLY(p);
aodv_rt_entry *rt;
char suppress_reply = 0;
double delay = 0.0;

/*
 * ・この関数内にある、英語と日本語が併記されているコメントは、オリジナルのコードに
 * 　記載されていたコメントを杉本が和訳したものです。
 * ・アドレス参照方法一覧
 * 　dst, srcはデータパケット転送方向が基準なので注意。rt系はrt_lookup後から使用可。
 *
 *     * RREP送信先(データ送信元)	(rt->rt_dst), ih->daddr()
 *     |
 *     * 次ホップ			rp->rp_src, (rt->rt_nexthop)
 *     |
 *     x 自ノード			index
 *     |
 *     * 前ホップ			ih->saddr()
 *     |
 *     * RREP送信元(データ送信先)	rp->rp_dst
 */

#ifdef DEBUG
 //fprintf(stderr, "%d - %s: received a REPLY from %d via %d\n", index, __FUNCTION__, rp->rp_dst, rp->rp_src);
 fprintf(stderr, "AODV %d %f r RREP from %d via %d for RREQ %f\n",
		index, CURRENT_TIME, rp->rp_dst, rp->rp_src, rp->rp_timestamp);
#endif // DEBUG

 /*
  *  Got a reply. So reset the "soft state" maintained for 
  *  route requests in the request table. We don't really have
  *  have a separate request table. It is just a part of the
  *  routing table itself.
  *  RREPを受信したため、リクエストテーブル内のRREQに関する"soft state"をリセット
  *  する。この実装では、独立したリクエストテーブルはなく、ルーティングテーブルの
  *  一部になっている。
  */
 // Note that rp_dst is the dest of the data packets, not the
 // the dest of the reply, which is the src of the data packets.
 // rp_dstはデータパケットの送信先を示している。
 // RREPの送信先、すなわちデータパケットの送信元ではないことに注意。

 rt = rtable.rt_lookup(rp->rp_dst);
 
 /*
  *  If I don't have a rt entry to this host... adding
  *  ルーティングテーブルに送信先に対するエントリが存在していなければ追加する。
  */
 if(rt == 0) {
	rt = rtable.rt_add(rp->rp_dst);
 }

 /*
  * Add a forward route table entry... here I am following 
  * Perkins-Royer AODV paper almost literally - SRD 5/99
  * 送信方向のルーティングテーブルエントリ更新処理。
  * 以下の処理はPerkins-RoyerによるAODVの論文にほぼ従っている。
  * (訳注: マルチルート修正により論文とは異なっていることに注意)
  */

 if ( (rt->rt_seqno < rp->rp_dst_seqno) ||   // newer route 
      ((rt->rt_seqno == rp->rp_dst_seqno) &&  
       (rt->rt_hops > rp->rp_hop_count)) ) { // shorter or better route

	// Update the rt entry
	rt_update(rt, rp->rp_dst_seqno, rp->rp_hop_count,
		rp->rp_src, CURRENT_TIME + rp->rp_lifetime);

	// reset the soft state
	rt->rt_req_cnt = 0;
	rt->rt_req_timeout = 0.0; 
	rt->rt_req_last_ttl = rp->rp_hop_count;
  
	if (ih->daddr() == index) { // If I am the original source
		// Update the route discovery latency statistics
		// rp->rp_timestamp is the time of request origination
		
		rt->rt_disc_latency[(unsigned char)rt->hist_indx] = (CURRENT_TIME - rp->rp_timestamp)
	                                         / (double) rp->rp_hop_count;
		// increment indx for next time
		rt->hist_indx = (rt->hist_indx + 1) % MAX_HISTORY;
	}	

#ifdef AODV_MULTIROUTE
 } // マルチルート関連処理挿入のため、if括弧をここで一旦閉じる。
 else {
	suppress_reply = 1;
#ifdef DEBUG
	fprintf(stderr, "%s: %d suppressed reply at %f\n", __FUNCTION__, index, CURRENT_TIME);
#endif // DEBUG
 }
	if(ih->daddr() == index) {
	/* 
	 * 自分がRREQ送信元ならば、マルチルート用ルーティングテーブルへルートを追加。
	 */ 
		if (! rt->rt_m_add(rp->rp_src, rp->rp_hop_count)) {
#ifdef DEBUG
			//fprintf(stderr, "MULTIROUTE: %s: %d failed to add a route to %d via %d at %f\n",
			//		__FUNCTION__, index,
			//		rp->rp_dst, rp->rp_src,
			//		CURRENT_TIME);
			fprintf(stderr, "AODV %d %f m failed to add route to %d via %d\n",
					index, CURRENT_TIME, rp->rp_dst, rp->rp_src);
#endif // DEBUG
		} else {
			if (rt->rt_routes >= ROUTE_COUNT) {
				rt_up(rt);
			}
#ifdef DEBUG
			fprintf(stderr, "MULTIROUTE: %s: %d add a new route to %d via %d with hop count %d at %f\n",
					__FUNCTION__, index,
					rp->rp_dst, rp->rp_src, rp->rp_hop_count,
					CURRENT_TIME);
			rt->rt_m_dump();
#endif // DEBUG
		}
	}

 // 一旦切ったif文の条件をここで復活させる (送信元の例外を追加)
 if ( ( ih->daddr() != index && (	// 自分がpの宛先ではない かつ
 	(rt->rt_seqno < rp->rp_dst_seqno) ||   // 新しいルート (シーケンス番号が若い) または
 	((rt->rt_seqno == rp->rp_dst_seqno) &&  
 	(rt->rt_hops > rp->rp_hop_count))      // 同じシーケンス番号で、ホップ数が少ない良いルート
 ) ) || ( 
 	(ih->daddr() == index) && 	// 自分がpの宛先 かつ
 	(rt->rt_flags == RTF_UP)		// ルートがUPしている場合 
 ) ) {
#endif // AODV_MULTIROUTE

	/*
	 * Send all packets queued in the sendbuffer destined for
	 * this destination. 
	 * キュー内にある、今回追加したルート向けのパケットを全て送信する。
	 * XXX - observe the "second" use of p.
	 */
#ifdef DEBUG
	fprintf(stderr, "%s: %d send all packet in sendbuffer at %f\n", __FUNCTION__, index, CURRENT_TIME);
#endif //DEBUG
#ifdef AODV_USERDATA_CONNECT
	userdata_callback(rt->rt_dst);
#endif // AODV_USERDATA_CONNECT
	Packet *buf_pkt;
	while((buf_pkt = rqueue.deque(rt->rt_dst))) {
		if(rt->rt_hops != INFINITY2) {
			assert (rt->rt_flags == RTF_UP);
			// Delay them a little to help ARP. Otherwise ARP 
			// may drop packets. -SRD 5/23/99
			forward(rt, buf_pkt, delay);
			delay += ARP_DELAY;
		}
	}
 }
#ifndef AODV_MULTIROUTE
 else {
	suppress_reply = 1;
#ifdef DEBUG
	fprintf(stderr, "%s: %d suppressed reply at %f\n", __FUNCTION__, index, CURRENT_TIME);
#endif // DEBUG
 }
#endif // AODV_MULTIROUTE

 /*
  * If reply is for me, discard it.
  */
 if(ih->daddr() == index || suppress_reply) {
	Packet::free(p);
 }
 /*
  * Otherwise, forward the Route Reply.
  */
 else {
	// Find the rt entry
	aodv_rt_entry *rt0 = rtable.rt_lookup(ih->daddr());
	// If the rt is up, forward
	if(rt0 && (rt0->rt_hops != INFINITY2)) {
		assert (rt0->rt_flags == RTF_UP);
		rp->rp_hop_count += 1;
		rp->rp_src = index;
		forward(rt0, p, NO_DELAY);
		// Insert the nexthop towards the RREQ source to 
		// the precursor list of the RREQ destination
		rt->pc_insert(rt0->rt_nexthop); // nexthop to RREQ source
     
#ifdef DEBUG
//     fprintf(stderr, "%s: %d forwarding Route Reply\n", __FUNCTION__, index);
		fprintf(stderr, "AODV %d %f f RREP forward to %d via %d\n",
			index, CURRENT_TIME, rt0->rt_dst, rt0->rt_nexthop);
#endif // DEBUG

	}
	else {
		// I don't know how to forward .. drop the reply. 

#ifdef DEBUG
		fprintf(stderr, "%s: %d dropping Route Reply\n", __FUNCTION__, index);
#endif // DEBUG

		drop(p, DROP_RTR_NO_ROUTE);
	}
 }
}


void
AODV::recvError(Packet *p) {
struct hdr_ip *ih = HDR_IP(p);
struct hdr_aodv_error *re = HDR_AODV_ERROR(p);
aodv_rt_entry *rt;
u_int8_t i;
Packet *rerr = Packet::alloc();
struct hdr_aodv_error *nre = HDR_AODV_ERROR(rerr);

#ifdef DEBUG
 fprintf(stderr, "AODV %d %f r RERR %d\n",
		index, CURRENT_TIME, re->DestCount);
#endif // DEBUG

 nre->DestCount = 0;
 for (i=0; i<re->DestCount; i++) { // For each unreachable destination
 /*
  * RERRに含まれる経路がルーティングテーブルに無いか、各エントリを調べる。
  *
  * AODV_MULTIROUTEでは、次のような処理を行う。
  *  1. マルチルート用ルーティングテーブルの検索・削除処理
  *  2. 通常のルーティングテーブルに対する確認・削除処理
  *  3. 通常のルーティングテーブルの内容が削除され、かつマルチルート用テーブルに
  *     エントリが残っていたら、マルチルート用のルートの一つをメインに昇格。
  *     残っていなかったらRTF_DOWN。
  *
  * ** 類似の処理がhandle_link_failure()にもあり。
  */ 

	rt = rtable.rt_lookup(re->unreachable_dst[i]);

	/*
	 * マルチルート用ルーティングテーブルに対する処理
	 */
#ifdef AODV_MULTIROUTE
	if ( rt && (rt->rt_seqno <= re->unreachable_dst_seqno[i]) ) {
		u_int8_t j;
		for (j=0; j<rt->rt_routes; j++) {
			if (rt->rt_m_nexthop[j] == ih->saddr()) {
				assert((rt->rt_seqno%2) == 0);
#ifdef DEBUG
				fprintf(stderr, "%s(%f): %d\t(%d\t%u\t%d)\t(%d\t%u\t%d)\n", __FUNCTION__,CURRENT_TIME,
						index, rt->rt_dst, rt->rt_seqno, rt->rt_nexthop,
						re->unreachable_dst[i],re->unreachable_dst_seqno[i],
						ih->saddr());
#endif // DEBUG
				rt->rt_seqno = re->unreachable_dst_seqno[i];
#ifdef DEBUG
				fprintf(stderr, "AODV %d %f t route #%d for %d removed\n",
					index, CURRENT_TIME, j, rt->rt_dst);
#endif // DEBUG
				rt->rt_m_delete(ih->saddr());
			}
        	}
	}
#endif // AODV_MULTIROUTE

	/*
	 * 通常のルーティングテーブルに対する処理
	 */
	if ( rt && (rt->rt_hops != INFINITY2) &&
		(rt->rt_nexthop == ih->saddr()) &&
		(rt->rt_seqno <= re->unreachable_dst_seqno[i]) )
	{
		assert(rt->rt_flags == RTF_UP);
		assert((rt->rt_seqno%2) == 0); // is the seqno even?

#ifdef DEBUG
		fprintf(stderr, "%s(%f): %d\t(%d\t%u\t%d)\t(%d\t%u\t%d)\n", __FUNCTION__,CURRENT_TIME,
				index, rt->rt_dst, rt->rt_seqno, rt->rt_nexthop,
				re->unreachable_dst[i],re->unreachable_dst_seqno[i],
				ih->saddr());
#endif // DEBUG

		rt->rt_seqno = re->unreachable_dst_seqno[i];
#ifndef AODV_MULTIROUTE
		rt_down(rt);
#else // AODV_MULTIROUTE
		if (rt->rt_routes == 0) {
			// マルチルート用ルーティングテーブルも空ならrt_down()
#ifdef DEBUG
			fprintf(stderr, "MULTIROUTE: %s: %d's multiple routing table is empty, down at %f.\n",
					 __FUNCTION__, index, CURRENT_TIME);
#endif // DEBUG
			rt_down(rt);
		} else {
			// マルチルート用ルーティングテーブルに経路が残っていたら復活
#ifdef DEBUG
			fprintf(stderr, "MULTIROUTE: %s: %d's multiple routing table is not empty, salvaged at %f.\n",
					 __FUNCTION__, index, CURRENT_TIME);
			//rt->rt_m_dump();
#endif // DEBUG
			rt->rt_hops = rt->rt_m_hops[0];
			rt->rt_nexthop = rt->rt_m_nexthop[0];
		}
#endif // AODV_MULTIROUTE

		// Not sure whether this is the right thing to do
		Packet *pkt;
		while((pkt = ifqueue->filter(ih->saddr()))) {
        		drop(pkt, DROP_RTR_MAC_CALLBACK);
     		}

		// if precursor list non-empty add to RERR and delete the precursor list
	     	if (!rt->pc_empty()) {
	     		nre->unreachable_dst[nre->DestCount] = rt->rt_dst;
	     		nre->unreachable_dst_seqno[nre->DestCount] = rt->rt_seqno;
     			nre->DestCount += 1;
			rt->pc_delete();
	     	}
	}
 } 

 if (nre->DestCount > 0) {
	// 削除したエントリがあったのでRERRを転送 (BROADCAST)
#ifdef DEBUG
	fprintf(stderr, "%s(%f): %d\t sending RERR...\n", __FUNCTION__, CURRENT_TIME, index);
#endif // DEBUG
	sendError(rerr);
 } else {
	Packet::free(rerr);
 }

 // 受信したRERRは用済み
 Packet::free(p);
}


/*
   Packet Transmission Routines
*/

void
AODV::forward(aodv_rt_entry *rt, Packet *p, double delay) {
struct hdr_cmn *ch = HDR_CMN(p);
struct hdr_ip *ih = HDR_IP(p);

 if(ih->ttl_ == 0) {

#ifdef DEBUG
  fprintf(stderr, "%s: calling drop()\n", __PRETTY_FUNCTION__);
#endif // DEBUG
 
  drop(p, DROP_RTR_TTL);
  return;
 }

 if (ch->ptype() != PT_AODV && ch->direction() == hdr_cmn::UP &&
	((u_int32_t)ih->daddr() == IP_BROADCAST)
		|| (ih->daddr() == here_.addr_)) {
	dmux_->recv(p,0);
	return;
 }

 if (rt) {
#ifndef AODV_MULTIROUTE // MULTIROUTE: rt_flags は RTF_WAIT である場合がある
   assert(rt->rt_flags == RTF_UP);
#endif // AODV_MULTIROUTE
   rt->rt_expire = CURRENT_TIME + ACTIVE_ROUTE_TIMEOUT;
#ifndef AODV_MULTIROUTE
   ch->next_hop_ = rt->rt_nexthop;
#else // AODV_MULTIROUTE
   if (ch->ptype() != PT_AODV && ih->saddr() == here_.addr_) {
     // 自分が送信したパケットの場合
     // XXX: データパケット送信先の変更はここ

     assert(rt->rt_flags == RTF_UP);
     assert(rt->rt_routes != 0);

/*
 * Application/UserDataが組み込まれているとき
 */
#ifdef ns_userdata_h

	// UserDataで割り当てた経路をパケットから取り出す
	int userpath = ((UserData*) p->userdata())->path();
	//fprintf(stderr, "MULTIROUTE: %s: userdata->path() = %d\n", __FUNCTION__, userpath);
	// 現在の経路数を元に経路割当を決め直す
	if (userpath >= rt->rt_routes) {
		// 割り当てた経路Noが保持経路数より多い場合は、カウンタの値を使う
		userpath = rt->rt_counter;
	}

#ifdef DEBUG
     fprintf(stderr, "MULTIROUTE: %s: %d sent data packet to %d via %d(#%d) at %f\n",
		 __FUNCTION__, index, rt->rt_dst,
		 rt->rt_m_nexthop[userpath], userpath,
		 CURRENT_TIME);
	//rt->rt_m_dump();
#endif // DEBUG
	ch->next_hop_ = rt->rt_m_nexthop[userpath];

/*
 * Application/UserDataが組み込まれていないとき
 */
#else // ns_userdata_h

#ifdef DEBUG
	fprintf(stderr, "MULTIROUTE: %s: %d sent data packet to %d via %d(#%d) at %f\n",
		__FUNCTION__, index, rt->rt_dst,
		rt->rt_m_nexthop[rt->rt_counter], rt->rt_counter,
		CURRENT_TIME);
	rt->rt_m_dump();
#endif // DEBUG
	ch->next_hop_ = rt->rt_m_nexthop[rt->rt_counter];

#endif // ns_userdata_h

	rt->rt_m_rotate();
 } else {
	// その他のパケット：通常どおり
#ifdef DEBUG
     if (ch->ptype() != PT_AODV) {
        fprintf(stderr, "MULTIROUTE: %s: %d forwards data packet to %d via %d at %f\n",
		 __FUNCTION__, index, rt->rt_dst,
		 rt->rt_nexthop,
		 CURRENT_TIME);
     }
#endif // DEBUG
     ch->next_hop_ = rt->rt_nexthop;
   }
#endif // AODV_MULTIROUTE
   ch->addr_type() = NS_AF_INET;
   ch->direction() = hdr_cmn::DOWN;       //important: change the packet's direction
 }
 else { // if it is a broadcast packet
   // assert(ch->ptype() == PT_AODV); // maybe a diff pkt type like gaf
   assert(ih->daddr() == (nsaddr_t) IP_BROADCAST);
   ch->addr_type() = NS_AF_NONE;
   ch->direction() = hdr_cmn::DOWN;       //important: change the packet's direction
 }

if (ih->daddr() == (nsaddr_t) IP_BROADCAST) {
 // If it is a broadcast packet
   assert(rt == 0);
   /*
    *  Jitter the sending of broadcast packets by 10ms
    */
   Scheduler::instance().schedule(target_, p,
      				   0.01 * Random::uniform());
 }
 else { // Not a broadcast packet 
   if(delay > 0.0) {
     Scheduler::instance().schedule(target_, p, delay);
   }
   else {
   // Not a broadcast packet, no delay, send immediately
     Scheduler::instance().schedule(target_, p, 0.);
   }
 }

}


void
AODV::sendRequest(nsaddr_t dst) {
// Allocate a RREQ packet 
Packet *p = Packet::alloc();
struct hdr_cmn *ch = HDR_CMN(p);
struct hdr_ip *ih = HDR_IP(p);
struct hdr_aodv_request *rq = HDR_AODV_REQUEST(p);
aodv_rt_entry *rt = rtable.rt_lookup(dst);

 assert(rt);

 /*
  *  Rate limit sending of Route Requests. We are very conservative
  *  about sending out route requests. 
  */

 if (rt->rt_flags == RTF_UP) {
   assert(rt->rt_hops != INFINITY2);
   Packet::free((Packet *)p);
   return;
 }

 if (rt->rt_req_timeout > CURRENT_TIME) {
   Packet::free((Packet *)p);
   return;
 }

 // rt_req_cnt is the no. of times we did network-wide broadcast
 // RREQ_RETRIES is the maximum number we will allow before 
 // going to a long timeout.

 if (rt->rt_req_cnt > RREQ_RETRIES) {
   rt->rt_req_timeout = CURRENT_TIME + MAX_RREQ_TIMEOUT;
   rt->rt_req_cnt = 0;
 Packet *buf_pkt;
   while ((buf_pkt = rqueue.deque(rt->rt_dst))) {
       drop(buf_pkt, DROP_RTR_NO_ROUTE);
   }
   Packet::free((Packet *)p);
   return;
 }

//#ifdef DEBUG
//   fprintf(stderr, "(%2d) - %2d sending Route Request, dst: %d\n",
//                    ++route_request, index, rt->rt_dst);
//#endif // DEBUG

 // Determine the TTL to be used this time. 
 // Dynamic TTL evaluation - SRD

 rt->rt_req_last_ttl = max(rt->rt_req_last_ttl,rt->rt_last_hop_count);

 if (0 == rt->rt_req_last_ttl) {
 // first time query broadcast
   ih->ttl_ = TTL_START;
 }
 else {
 // Expanding ring search.
   if (rt->rt_req_last_ttl < TTL_THRESHOLD)
     ih->ttl_ = rt->rt_req_last_ttl + TTL_INCREMENT;
   else {
   // network-wide broadcast
     ih->ttl_ = NETWORK_DIAMETER;
     rt->rt_req_cnt += 1;
   }
 }

 // remember the TTL used  for the next time
 rt->rt_req_last_ttl = ih->ttl_;

 // PerHopTime is the roundtrip time per hop for route requests.
 // The factor 2.0 is just to be safe .. SRD 5/22/99
 // Also note that we are making timeouts to be larger if we have 
 // done network wide broadcast before. 

 rt->rt_req_timeout = 2.0 * (double) ih->ttl_ * PerHopTime(rt); 
 if (rt->rt_req_cnt > 0)
   rt->rt_req_timeout *= rt->rt_req_cnt;
 rt->rt_req_timeout += CURRENT_TIME;

 // Don't let the timeout to be too large, however .. SRD 6/8/99
 if (rt->rt_req_timeout > CURRENT_TIME + MAX_RREQ_TIMEOUT)
   rt->rt_req_timeout = CURRENT_TIME + MAX_RREQ_TIMEOUT;
 rt->rt_expire = 0;

//#ifdef DEBUG
// fprintf(stderr, "(%2d) - %2d sending Route Request, dst: %d, tout %f ms\n",
//	         ++route_request, 
//		 index, rt->rt_dst, 
//		 rt->rt_req_timeout - CURRENT_TIME);
//#endif	// DEBUG
	

 // Fill out the RREQ packet 
 // ch->uid() = 0;
 ch->ptype() = PT_AODV;
 ch->size() = IP_HDR_LEN + rq->size();
 ch->iface() = -2;
 ch->error() = 0;
 ch->addr_type() = NS_AF_NONE;
 ch->prev_hop_ = index;          // AODV hack

 ih->saddr() = index;
 ih->daddr() = IP_BROADCAST;
 ih->sport() = RT_PORT;
 ih->dport() = RT_PORT;

 // Fill up some more fields. 
 rq->rq_type = AODVTYPE_RREQ;
 rq->rq_hop_count = 1;
 rq->rq_bcast_id = bid++;
 rq->rq_dst = dst;
 rq->rq_dst_seqno = (rt ? rt->rt_seqno : 0);
 rq->rq_src = index;
 seqno += 2;
 assert ((seqno%2) == 0);
 rq->rq_src_seqno = seqno;
 rq->rq_timestamp = CURRENT_TIME;

#ifdef DEBUG
	fprintf(stderr, "AODV %d %f s RREQ #%d to %d tout at %f\n",
		index, CURRENT_TIME, rq->rq_bcast_id, rt->rt_dst, rt->rt_req_timeout);
#endif	// DEBUG
	
 Scheduler::instance().schedule(target_, p, 0.);

}

void
AODV::sendReply(nsaddr_t ipdst, u_int32_t hop_count, nsaddr_t rpdst,
                u_int32_t rpseq, u_int32_t lifetime, double timestamp) {
Packet *p = Packet::alloc();
struct hdr_cmn *ch = HDR_CMN(p);
struct hdr_ip *ih = HDR_IP(p);
struct hdr_aodv_reply *rp = HDR_AODV_REPLY(p);
aodv_rt_entry *rt = rtable.rt_lookup(ipdst);

//#ifdef DEBUG
//fprintf(stderr, "sending Reply from %d at %.2f\n", index, Scheduler::instance().clock());
//#endif // DEBUG
 assert(rt);

 rp->rp_type = AODVTYPE_RREP;
 //rp->rp_flags = 0x00;
 rp->rp_hop_count = hop_count;
 rp->rp_dst = rpdst;
 rp->rp_dst_seqno = rpseq;
 rp->rp_src = index;
 rp->rp_lifetime = lifetime;
 rp->rp_timestamp = timestamp;
   
 // ch->uid() = 0;
 ch->ptype() = PT_AODV;
 ch->size() = IP_HDR_LEN + rp->size();
 ch->iface() = -2;
 ch->error() = 0;
 ch->addr_type() = NS_AF_INET;
#ifndef AODV_MULTIROUTE
 ch->next_hop_ = rt->rt_nexthop;
#ifdef DEBUG
 fprintf(stderr, "AODV %d %f s RREP from %d via %d for RREQ %f\n",
		index, CURRENT_TIME, rpdst, ch->next_hop_, timestamp);
#endif // DEBUG
#else // AODV_MULTIROUTE
 if(rpdst == index) {
	// 宛先ノード：最後に追加されたルートへ返信
	ch->next_hop_ = rt->rt_m_nexthop[rt->rt_routes - 1];
#ifdef DEBUG
	fprintf(stderr, "AODV %d %f s RREP from %d via %d (route #%d) for RREQ %f\n",
			index, CURRENT_TIME, rpdst, ch->next_hop_, rt->rt_routes - 1, timestamp);
	//rt->rt_m_dump();
#endif // DEBUG
 } else {
	// 中継ノード：通常どおり
	ch->next_hop_ = rt->rt_nexthop;
#ifdef DEBUG
	fprintf(stderr, "AODV %d %f f RREP from %d via %d for RREQ %f\n",
			index, CURRENT_TIME, rpdst, ch->next_hop_, timestamp);
	//rt->rt_m_dump();
#endif // DEBUG
 }
#endif // AODV_MULTIROUTE
 ch->prev_hop_ = index;          // AODV hack
 ch->direction() = hdr_cmn::DOWN;

 ih->saddr() = index;
 ih->daddr() = ipdst;
 ih->sport() = RT_PORT;
 ih->dport() = RT_PORT;
 ih->ttl_ = NETWORK_DIAMETER;

 Scheduler::instance().schedule(target_, p, 0.0);

}

void
AODV::sendError(Packet *p, bool jitter) {
struct hdr_cmn *ch = HDR_CMN(p);
struct hdr_ip *ih = HDR_IP(p);
struct hdr_aodv_error *re = HDR_AODV_ERROR(p);
    
#ifdef ERROR
//fprintf(stderr, "sending Error from %d at %.2f\n", index, Scheduler::instance().clock());
 fprintf(stderr, "AODV %d %f s RERR\n",
		index, CURRENT_TIME);
#endif // DEBUG

 re->re_type = AODVTYPE_RERR;
 //re->reserved[0] = 0x00; re->reserved[1] = 0x00;
 // DestCount and list of unreachable destinations are already filled

 // ch->uid() = 0;
 ch->ptype() = PT_AODV;
 ch->size() = IP_HDR_LEN + re->size();
 ch->iface() = -2;
 ch->error() = 0;
 ch->addr_type() = NS_AF_NONE;
 ch->next_hop_ = 0;
 ch->prev_hop_ = index;          // AODV hack
 ch->direction() = hdr_cmn::DOWN;       //important: change the packet's direction

 ih->saddr() = index;
 ih->daddr() = IP_BROADCAST;
 ih->sport() = RT_PORT;
 ih->dport() = RT_PORT;
 ih->ttl_ = 1;

 // Do we need any jitter? Yes
 if (jitter)
 	Scheduler::instance().schedule(target_, p, 0.01*Random::uniform());
 else
 	Scheduler::instance().schedule(target_, p, 0.0);

}


/*
   Neighbor Management Functions
*/

void
AODV::sendHello() {
Packet *p = Packet::alloc();
struct hdr_cmn *ch = HDR_CMN(p);
struct hdr_ip *ih = HDR_IP(p);
struct hdr_aodv_reply *rh = HDR_AODV_REPLY(p);

#ifdef DEBUG
//fprintf(stderr, "sending Hello from %d at %.2f\n", index, Scheduler::instance().clock());
	fprintf(stderr, "AODV %d %f s HELO\n",
		index, CURRENT_TIME);
#endif // DEBUG

 rh->rp_type = AODVTYPE_HELLO;
 //rh->rp_flags = 0x00;
 rh->rp_hop_count = 1;
 rh->rp_dst = index;
 rh->rp_dst_seqno = seqno;
 rh->rp_lifetime = (1 + ALLOWED_HELLO_LOSS) * HELLO_INTERVAL;

 // ch->uid() = 0;
 ch->ptype() = PT_AODV;
 ch->size() = IP_HDR_LEN + rh->size();
 ch->iface() = -2;
 ch->error() = 0;
 ch->addr_type() = NS_AF_NONE;
 ch->prev_hop_ = index;          // AODV hack

 ih->saddr() = index;
 ih->daddr() = IP_BROADCAST;
 ih->sport() = RT_PORT;
 ih->dport() = RT_PORT;
 ih->ttl_ = 1;

 Scheduler::instance().schedule(target_, p, 0.0);
}


void
AODV::recvHello(Packet *p) {
//struct hdr_ip *ih = HDR_IP(p);
struct hdr_aodv_reply *rp = HDR_AODV_REPLY(p);
AODV_Neighbor *nb;

#ifdef DEBUG
	fprintf(stderr, "AODV %d %f r HELO from %d\n",
		index, CURRENT_TIME, rp->rp_dst);
#endif // DEBUG

 nb = nb_lookup(rp->rp_dst);
 if(nb == 0) {
   nb_insert(rp->rp_dst);
 }
 else {
   nb->nb_expire = CURRENT_TIME +
                   (1.5 * ALLOWED_HELLO_LOSS * HELLO_INTERVAL);
 }

 Packet::free(p);
}

void
AODV::nb_insert(nsaddr_t id) {
AODV_Neighbor *nb = new AODV_Neighbor(id);

 assert(nb);
 nb->nb_expire = CURRENT_TIME +
                (1.5 * ALLOWED_HELLO_LOSS * HELLO_INTERVAL);
 LIST_INSERT_HEAD(&nbhead, nb, nb_link);
 seqno += 2;             // set of neighbors changed
 assert ((seqno%2) == 0);
}


AODV_Neighbor*
AODV::nb_lookup(nsaddr_t id) {
AODV_Neighbor *nb = nbhead.lh_first;

 for(; nb; nb = nb->nb_link.le_next) {
   if(nb->nb_addr == id) break;
 }
 return nb;
}


/*
 * Called when we receive *explicit* notification that a Neighbor
 * is no longer reachable.
 */
void
AODV::nb_delete(nsaddr_t id) {
AODV_Neighbor *nb = nbhead.lh_first;

 log_link_del(id);
 seqno += 2;     // Set of neighbors changed
 assert ((seqno%2) == 0);

 for(; nb; nb = nb->nb_link.le_next) {
   if(nb->nb_addr == id) {
     LIST_REMOVE(nb,nb_link);
     delete nb;
     break;
   }
 }

 handle_link_failure(id);

}


/*
 * Purges all timed-out Neighbor Entries - runs every
 * HELLO_INTERVAL * 1.5 seconds.
 */
void
AODV::nb_purge() {
AODV_Neighbor *nb = nbhead.lh_first;
AODV_Neighbor *nbn;
double now = CURRENT_TIME;

 for(; nb; nb = nbn) {
   nbn = nb->nb_link.le_next;
   if(nb->nb_expire <= now) {
     nb_delete(nb->nb_addr);
   }
 }

}

/*
 * Application/UserData 用インターフェース
 */
#ifdef AODV_MULTIROUTE
// ルート数を返す
int AODV::rt_count(nsaddr_t dst) {

	aodv_rt_entry *rt;

	rt = rtable.rt_lookup(dst);
	if (rt == 0) {
		// 該当ルートが無い
		return 0;
	} else {
		// 該当ルートがあった
		return rt->rt_routes;
	}

}
// 各ルートのホップ数を返す
int AODV::rt_hops(nsaddr_t dst, int path_id) {

	aodv_rt_entry *rt;

	rt = rtable.rt_lookup(dst);
	if (rt == 0) {
		// 該当エントリが無い
		return 0;
	} else {
		// 該当エントリがあった
		if (path_id < rt->rt_routes) {
			// 該当経路がある
			return rt->rt_m_hops[path_id];
		} else {
			// 該当経路がない
			return 0;
		}
	}
}
#endif // AODV_MULTIROUTE
#ifdef AODV_USERDATA_CONNECT
// 強制ルート構築
void AODV::force_rt_request(nsaddr_t dst, UserDataApp *callback) {

	aodv_rt_entry *rt;

#ifdef DEBUG
	fprintf(stderr, "UserData called AODV::force_rt_request for %d\n", dst);
#endif // DEBUG

	userdata_callback_ = callback;
	userdata_dst = dst;
	rt_request_status_ = 1;

	rt = rtable.rt_lookup(dst);
	if(rt == 0) {
		rt = rtable.rt_add(dst);
	}
	sendRequest(rt->rt_dst);

}
// UserDataへの通知
void AODV::userdata_callback(nsaddr_t dst) {

	if (dst == userdata_dst) {
		rt_request_status_ = 2;
		userdata_callback_->aodv_callback();
	}

}
// ルート構築失敗
void AODV::userdata_fail_callback(nsaddr_t dst) {

	if (dst == userdata_dst) {
		// もう一回force_rt_requestを呼ばせる
		rt_request_status_ = 0;
	}

}
#endif // AODV_USERDATA_CONNECT

/* vim: set fenc=utf-8 ff=unix */
