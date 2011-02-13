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

The AODV code developed by the CMU/MONARCH group was optimized and tuned by Samir Das and Mahesh Marina, University of Cincinnati. The work was partially done in Sun Microsystems.
*/

/*
 * AODV マルチルート拡張 (仮実装)
 * Last Modified: 2008/10/30 18:45:20
 */

#include <aodv/aodv_rtable.h>
//#include <cmu/aodv/aodv.h>

/*
  The Routing Table
*/

aodv_rt_entry::aodv_rt_entry()
{
int i;

 rt_req_timeout = 0.0;
 rt_req_cnt = 0;

 rt_dst = 0;
 rt_seqno = 0;
 rt_hops = rt_last_hop_count = INFINITY2;
 rt_nexthop = 0;
 LIST_INIT(&rt_pclist);
 rt_expire = 0.0;
 rt_flags = RTF_DOWN;

 /*
 rt_errors = 0;
 rt_error_time = 0.0;
 */

#ifdef AODV_MULTIROUTE
	for(i=0; i < ROUTE_COUNT; i++) {
		rt_m_hops[i] = INFINITY2;
		rt_m_nexthop[i] = 0;
	}
	rt_routes = 0;
	rt_counter = 0;
	rt_m_create = 0.0;
#endif // AODV_MULTIROUTE

 for (i=0; i < MAX_HISTORY; i++) {
   rt_disc_latency[i] = 0.0;
 }
 hist_indx = 0;
 rt_req_last_ttl = 0;

 LIST_INIT(&rt_nblist);

}


aodv_rt_entry::~aodv_rt_entry()
{
AODV_Neighbor *nb;

 while((nb = rt_nblist.lh_first)) {
   LIST_REMOVE(nb, nb_link);
   delete nb;
 }

AODV_Precursor *pc;

 while((pc = rt_pclist.lh_first)) {
   LIST_REMOVE(pc, pc_link);
   delete pc;
 }

}


void
aodv_rt_entry::nb_insert(nsaddr_t id)
{
AODV_Neighbor *nb = new AODV_Neighbor(id);
        
 assert(nb);
 nb->nb_expire = 0;
 LIST_INSERT_HEAD(&rt_nblist, nb, nb_link);

}


AODV_Neighbor*
aodv_rt_entry::nb_lookup(nsaddr_t id)
{
AODV_Neighbor *nb = rt_nblist.lh_first;

 for(; nb; nb = nb->nb_link.le_next) {
   if(nb->nb_addr == id)
     break;
 }
 return nb;

}


void
aodv_rt_entry::pc_insert(nsaddr_t id)
{
	if (pc_lookup(id) == NULL) {
	AODV_Precursor *pc = new AODV_Precursor(id);
        
 		assert(pc);
 		LIST_INSERT_HEAD(&rt_pclist, pc, pc_link);
	}
}


AODV_Precursor*
aodv_rt_entry::pc_lookup(nsaddr_t id)
{
AODV_Precursor *pc = rt_pclist.lh_first;

 for(; pc; pc = pc->pc_link.le_next) {
   if(pc->pc_addr == id)
   	return pc;
 }
 return NULL;

}

void
aodv_rt_entry::pc_delete(nsaddr_t id) {
AODV_Precursor *pc = rt_pclist.lh_first;

 for(; pc; pc = pc->pc_link.le_next) {
   if(pc->pc_addr == id) {
     LIST_REMOVE(pc,pc_link);
     delete pc;
     break;
   }
 }

}

void
aodv_rt_entry::pc_delete(void) {
AODV_Precursor *pc;

 while((pc = rt_pclist.lh_first)) {
   LIST_REMOVE(pc, pc_link);
   delete pc;
 }
}	

bool
aodv_rt_entry::pc_empty(void) {
AODV_Precursor *pc;

 if ((pc = rt_pclist.lh_first)) return false;
 else return true;
}	

/*
  The Routing Table
*/

aodv_rt_entry*
aodv_rtable::rt_lookup(nsaddr_t id)
{
aodv_rt_entry *rt = rthead.lh_first;

 for(; rt; rt = rt->rt_link.le_next) {
   if(rt->rt_dst == id)
     break;
 }
 return rt;

}

void
aodv_rtable::rt_delete(nsaddr_t id)
{
aodv_rt_entry *rt = rt_lookup(id);

 if(rt) {
   LIST_REMOVE(rt, rt_link);
   delete rt;
 }

}

aodv_rt_entry*
aodv_rtable::rt_add(nsaddr_t id)
{
aodv_rt_entry *rt;

 assert(rt_lookup(id) == 0);
 rt = new aodv_rt_entry;
 assert(rt);
 rt->rt_dst = id;
 LIST_INSERT_HEAD(&rthead, rt, rt_link);
 return rt;
}

#ifdef AODV_MULTIROUTE
/*
 * マルチルート用ルーティングテーブルへのルート追加
 */
bool
aodv_rt_entry::rt_m_add(nsaddr_t nexthop, u_int16_t hops)
{
	if ( rt_routes >= ROUTE_COUNT || rt_m_lookup(nexthop) != -1 ) {
		/* 
		 *  1) ルーティングテーブルが満杯
		 *  2) 同じルートが既にルーティングテーブル内にある
		 *  場合には追加せずにfalseを返す。
		 */
#ifdef DEBUG
		if(rt_routes >= ROUTE_COUNT) {
			fprintf(stderr, "MULTIROUTE: %s: %d full at %f\n",
				 __FUNCTION__, index, CURRENT_TIME);
		}
		if(rt_m_lookup(nexthop) != -1) {
			fprintf(stderr, "MULTIROUTE: %s: %d duplicate at %f\n",
				 __FUNCTION__, index, CURRENT_TIME);
		}
#endif // DEBUG
		return false;
	} else {
		rt_routes++;
		rt_m_nexthop[rt_routes - 1] = nexthop;
		rt_m_hops[rt_routes - 1] = hops;
		return true;
	}
}

/*
 * マルチルート用ルーティングテーブルからのルート削除
 */
bool
aodv_rt_entry::rt_m_delete(nsaddr_t nexthop)
{
	int i, m_rt = -1;

	m_rt = rt_m_lookup(nexthop);	// nexthopからルート番号を調べる
	if(m_rt == -1) return false;	// 削除対象が見つからなかった

	/* 対象エントリの削除 */
	for(i=0;i<ROUTE_COUNT;i++) {		
		// 対象よりも後ろのエントリで上書き
		if(i>m_rt) {
			rt_m_hops[i-1] = rt_m_hops[i];
			rt_m_nexthop[i-1] = rt_m_nexthop[i];
		}
	}
	rt_routes--;
	// rt_counterが存在しないルート番号を指していたらrt_routesの値にする
	if(rt_counter > rt_routes) rt_counter = rt_routes;

	return true;

}

/*
 * ルートの切り替え (ラウンドロビン用)
 */
void
aodv_rt_entry::rt_m_rotate(void)
{
	rt_counter++;
	if(rt_counter >= rt_routes) {
		rt_counter = 0;
	}	
}

/*
 * マルチルート用ルーティングテーブルのlookup (nexthop→ルート番号)
 */
int
aodv_rt_entry::rt_m_lookup(nsaddr_t nexthop)
{
	int i, m_rt = -1;	// ルートが見つからなければ -1 を返す

	for(i=0; i<rt_routes; i++) {
		if(rt_m_nexthop[i] == nexthop) {
			m_rt = i;
		}
	}

	return m_rt;
}

/*
 * DEBUG: マルチルート用ルーティングテーブルの中身を表示
 */
void
aodv_rt_entry::rt_m_dump()
{
	int i;

	for(i=0; i<rt_routes; i++) {
		fprintf(stderr, "MULTIROUTE: %s: [#%d] %d -- %d hops\n",
			 __FUNCTION__, i, rt_m_nexthop[i], rt_m_hops[i]);
	}
	fprintf(stderr, "MULTIROUTE: %s: number of routes is %d, current route is #%d, state is ",
			 __FUNCTION__, rt_routes, rt_counter);
	switch (rt_flags) {
		case RTF_UP:
			fprintf(stderr, "RTF_UP\n");
			break;
		case RTF_DOWN:
			fprintf(stderr, "RTF_DOWN\n");
			break;
		case RTF_IN_REPAIR:
			fprintf(stderr, "RTF_IN_REPAIR\n");
			break;
		case RTF_WAIT:
			fprintf(stderr, "RTF_WAIT\n");
			break;
		detault:
			fprintf(stderr, "unknown(%d)\n", rt_flags);
			break;
	}
}
#endif //AODV_MULTIROUTE

/* vim: set fenc=utf-8 ff=unix */
