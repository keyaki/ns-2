/* vim: set fenc=utf-8 ff=unix */

/*
 * Application/UserData : ユーザ定義データの送受信
 *
 * Wada Laboratory, Shizuoka University
 * Takahiro Sugimoto	<keyaki.no.kokage@gmail.com>
 *
 * Last Modified: 2008/12/19 17:30:14
 */

#ifndef ns_userdata_h
#define ns_userdata_h

#include <stdarg.h>
#include "app.h"
#include "userdata_file.h"

#define CURRENT_TIME Scheduler::instance().clock()

// デバッグメッセージ出力制御
#define UD_DEBUG

// 送信間隔の初期値 … set-interval で変更可能
#define INTERVAL 0.01

// 修正済AODVエージェントと連携するかどうか
// ここではなく、コンパイルオプションでの設定を推奨します。
// ** マルチルート関連の連携機能を有効にする **
//#define AODV_MULTIROUTE
// ** ルート構築要求関連の連携機能を有効にする **
//#define AODV_USERDATA_CONNECT

// オブジェクトへのポインタをメンバ変数に入れるために先に宣言
class UserData;
class UserDataApp;

// AODV関係
// AODV_MULTIROUTE/AODV_USERDATA_CONNECTのいずれかがdefineされていたら、
// USE_AODVをdefineする。
#ifdef AODV_MULTIROUTE
#define USE_AODV
#endif // AODV_MULTIROUTE
#ifdef AODV_USERDATA_CONNECT
#define USE_AODV
#endif // AODV_USERDATA_CONNECT

#ifdef USE_AODV
#include "../aodv/aodv.h"
#endif // USE_AODV

/* ファイル送信タイマー */
class UserDataTimer : public TimerHandler {
	/*
	 * TimerHandlerの仕様については common/timer-handler.{h,cc} を参照。
	 *
	 * マニュアルでは expire が virtual double expire() となっているが、
	 * 開発に使用しているバージョン(2.32)では virtual void expire() と
	 * なっていることに注意。Timer の reschedule には resched() を使用
	 * する(timer-handler.h の l.98-99 を参照のこと)。
	 */
 public:
	UserDataTimer(UserDataApp *a) : TimerHandler() {
		a_ = a;
	}
 protected:
	virtual void expire(Event *e);
	UserDataApp *a_;
};

/* データ送受信アプリーケーションクラス */
class UserDataApp : public Application {
 public:
	// コンストラクタ/デストラクタ
	UserDataApp();
	~UserDataApp();
	// 送信処理 (Timerから呼び出される)
	void send();
	// 変数取得
	int status() {
		// 送受信の状態
		return status_;
	}
	double delay() {
		// 送信周期
		return interval_;
	}
	Agent* agent() {
		return agent_;
	}
	AODV* ragent() {
		// ルーティングエージェントへのポインタ
		return ragent_;
	}
#ifdef AODV_USERDATA_CONNECT
	// AODVからのルート構築完了通知
	void aodv_callback();
#endif // AODV_USERDATA_CONNECT
	// UserDataApp間通信 (see "class Process" in ns-process.{h,cc})
	virtual void process_data(int size, AppData* data);
	virtual void send_data(int size, AppData* data = 0) {
		// 相手先のprocess_data()を呼び出す。UserDataAppでは使用しない。
		//if (target_) target_->process_data(size, data);
		abort();
	}
 protected:
	// Tclインタプリタ
	virtual int command(int argc, const char*const* argv);
	// パケット受信
	//  - Applicationの子クラスなので必要。
	//  - 実際の受信処理は process_data が行う。
	void recv(Packet* pkt, Handler*);
 private:
	// 送受信状態変更
	void start();
	void stop();
	void wait();
	void restart();
	// 送信周期
	double interval_;
	// 送受信の状態
	int status_;
#define UD_S_NULL 0
#define UD_S_SEND 1
#define UD_S_WAIT 2
#define UD_S_STOP 3
	// パケット番号
	int packetID_;
	// アタッチされたUserDataFileオブジェクトへのポインタ
	UserDataFile* filein_;
	UserDataFile* fileout_;
	UserDataFile* filesorted_;
	// Timer
	UserDataTimer timer_;
#ifdef USE_AODV
	// アタッチされたルーティングエージェントへのポインタ
	AODV* ragent_;
#endif // USE_AODV
#ifdef AODV_MULTIROUTE
	// マルチルートへのパケット割り当て方式
	int multiroute_scheme_;
#define UD_ROUNDROBIN 0
#define UD_UNIFORM 1
#define UD_HOPWEIGHTED 2
#define UD_CLONE 3
#define UD_SHORTONLY 4
	// UD_ROUNDROBIN用カウンタ
	int multiroute_rr_counter_;
#endif // AODV_MULTIROUTE
	// ログ関係
	UserDataLog* logout_;
	// 受信データ再構築用一時キャッシュ
	struct cache {
		struct cache* previous;
		unsigned char* cached_data;
		int id;
		int size;
		struct cache* next;
		void init(struct cache* prev) {
			previous = prev;
			cached_data = NULL;
			id = 0;
			size = 0;
			next = NULL;
		}
		void setdata(unsigned char* data, int data_id, int data_size) {
			cached_data = new unsigned char[data_size];
			memcpy(cached_data, data, data_size);
			id = data_id;
			size = data_size;
		}
	};
	cache* cache_entrypoint;
	int cache_fillsize;
	bool cache_insert(unsigned char* data, int id, int size);
	void cache_flush();
	int flushed_flag;
};

/* ADU for UserDataApp */
class UserData : public PacketData {
	/* 
	 *  ADU (Application-level Data Unit) については、common/ns-process.{h,cc}
	 *  および、マニュアル pp.341-343 "Web cache as an application" を参照。
	 *  ここで継承している PacketData は common/packet.{h,cc} で、AppData の
	 *  子クラスとして宣言されている。
	 */
 public:
	// コンストラクタ: 新規生成時
	UserData(int sz) : PacketData(sz) {
		// 変数を初期化
		path_ = 0;
		packetID_ = 0;
	}
	// コンストラクタ: コピー時
	UserData(UserData& d) : PacketData(d) {
		// 変数もコピー
		path_ = d.path_;
		packetID_ = d.packetID_;
	}
	// 変数のやりとり
	virtual int path() { return path_; }
	void path(int id) { path_ = id; }
	virtual int packetID() { return packetID_; };
	void packetID(int id) { packetID_ = id; }

	// パケットサイズを返す
	// TODO: 追加した情報分のサイズを足す
	//virtual int size() const { return datalen_; }

	// コピー関数のオーバーライド
	virtual UserData* copy() { return new UserData(*this); }
 private:
	// 割当先ルート (AODV_MULTIROUTE有効時に利用)
	int path_;
	// パケット番号
	int packetID_;
};

#endif // ns_userdata_h
