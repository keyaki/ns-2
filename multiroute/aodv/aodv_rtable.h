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
 * Last Modified: 2008/10/22 23:53:10
 */

#ifndef __aodv_rtable_h__
#define __aodv_rtable_h__

#include <assert.h>
#include <sys/types.h>
#include <config.h>
#include <lib/bsd-list.h>
#include <scheduler.h>

#define CURRENT_TIME    Scheduler::instance().clock()
#define INFINITY2        0xff

/*
   AODV Neighbor Cache Entry
*/
class AODV_Neighbor {
        friend class AODV;
        friend class aodv_rt_entry;
 public:
        AODV_Neighbor(u_int32_t a) { nb_addr = a; }

 protected:
        LIST_ENTRY(AODV_Neighbor) nb_link;
        nsaddr_t        nb_addr;
        double          nb_expire;      // ALLOWED_HELLO_LOSS * HELLO_INTERVAL
};

LIST_HEAD(aodv_ncache, AODV_Neighbor);

/*
   AODV Precursor list data structure
*/
class AODV_Precursor {
        friend class AODV;
        friend class aodv_rt_entry;
 public:
        AODV_Precursor(u_int32_t a) { pc_addr = a; }

 protected:
        LIST_ENTRY(AODV_Precursor) pc_link;
        nsaddr_t        pc_addr;	// precursor address
};

LIST_HEAD(aodv_precursors, AODV_Precursor);


/*
  Route Table Entry
*/

class aodv_rt_entry {
        friend class aodv_rtable;
        friend class AODV;
	friend class LocalRepairTimer;
 public:
        aodv_rt_entry();
        ~aodv_rt_entry();

        void            nb_insert(nsaddr_t id);
        AODV_Neighbor*  nb_lookup(nsaddr_t id);

        void            pc_insert(nsaddr_t id);
        AODV_Precursor* pc_lookup(nsaddr_t id);
        void 		pc_delete(nsaddr_t id);
        void 		pc_delete(void);
        bool 		pc_empty(void);

        double          rt_req_timeout;         // when I can send another req
        u_int8_t        rt_req_cnt;             // number of route requests
	
 protected:
        LIST_ENTRY(aodv_rt_entry) rt_link;

        nsaddr_t        rt_dst;			// 送信先アドレス
        u_int32_t       rt_seqno;		// シーケンス番号
	/* u_int8_t 	rt_interface; */
        u_int16_t       rt_hops;       		// ホップ数 (hop count)
	int 		rt_last_hop_count;	// last valid hop count
        nsaddr_t        rt_nexthop;    		// 次ホップのIPアドレス (next hop IP address)
	/* list of precursors */ 
        aodv_precursors rt_pclist;
        double          rt_expire;     		// エントリ有効期限 (when entry expires)
        u_int8_t        rt_flags;		// ルートの状態(フラグ)

#define RTF_DOWN 0		// ルート無効(DOWN)
#define RTF_UP 1		// ルート有効(UP)
#define RTF_IN_REPAIR 2		// ルート修復中

#ifdef AODV_MULTIROUTE
	/* 
	 * マルチルート用追加フラグ
	 */
#define RTF_WAIT 3		// 複数ルート構築待ち状態(WAIT) データパケット以外に対してはRTF_UPと同等

	/* 
	 * マルチルート拡張関連メンバ変数・関数
	 */
#define ROUTE_COUNT 2		// 使用するルート数
//#define MULTIROUTE_TIMEOUT 5	// 複数ルート構築を待つ期限
	/* マルチルート用ルーティングテーブル */
        u_int16_t       rt_m_hops[ROUTE_COUNT];       	// ホップ数 (hop count)
        nsaddr_t        rt_m_nexthop[ROUTE_COUNT];    	// 次ホップのIPアドレス (next hop IP address)
	/* ルート切替のための変数 */
	int             rt_routes;			// 保持しているルートの数
	int             rt_counter;			// ルート選択カウンタ
	double          rt_m_create;			// エントリが生成された時間
	/* マルチルート用ルーティングテーブルの操作を行うメンバ関数 */
	bool            rt_m_add(nsaddr_t nexthop, u_int16_t hops);	// ルート追加
	bool            rt_m_delete(nsaddr_t nexthop);	// ルート削除
	void            rt_m_rotate(void);		// ルート切替
	int             rt_m_lookup(nsaddr_t nexthop);	// nexthopからルートをlookup
	void            rt_m_dump();                    // デバッグ用
#endif // AODV_MULTIROUTE

        /*
         *  Must receive 4 errors within 3 seconds in order to mark
         *  the route down.
        u_int8_t        rt_errors;      // error count
        double          rt_error_time;
#define MAX_RT_ERROR            4       // errors
#define MAX_RT_ERROR_TIME       3       // seconds
         */

#define MAX_HISTORY	3
	double 		rt_disc_latency[MAX_HISTORY];
	char 		hist_indx;
        int 		rt_req_last_ttl;        // last ttl value used
	// last few route discovery latencies
	// double 		rt_length [MAX_HISTORY];
	// last few route lengths

        /*
         * a list of neighbors that are using this route.
         */
        aodv_ncache          rt_nblist;
};


/*
  The Routing Table
*/

class aodv_rtable {
 public:
	aodv_rtable() { LIST_INIT(&rthead); }

        aodv_rt_entry*       head() { return rthead.lh_first; }

        aodv_rt_entry*       rt_add(nsaddr_t id);
        void                 rt_delete(nsaddr_t id);
        aodv_rt_entry*       rt_lookup(nsaddr_t id);

 private:
        LIST_HEAD(aodv_rthead, aodv_rt_entry) rthead;
};

#endif /* _aodv__rtable_h__ */

/* vim: set fenc=utf-8 ff=unix */
