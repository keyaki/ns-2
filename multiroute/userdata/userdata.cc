/* vim: set fenc=utf-8 ff=unix */

/*
 * Application/UserData : ユーザ定義データの送受信
 *
 * Wada Laboratory, Shizuoka University
 * Takahiro Sugimoto	<keyaki.no.kokage@gmail.com>
 *
 * Last Modified: 2009/01/21 19:36:40
 */

#include <string.h>
#include "packet.h"
#include "userdata.h"

#ifdef AODV_MULTIROUTE
#include "rng.h"
#endif // AODV_MULTIROUTE

/* Tcl Hooks */
static class UserDataClass : public TclClass {
 public:
	UserDataClass() : TclClass("Application/UserData") {}
	TclObject* create(int, const char*const*) {
		return (new UserDataApp);
	}
} class_userdata;

/*
 *  "UserDataTimer" Class
 *            -- パケット送信タイマー
 */
void UserDataTimer::expire(Event *e) {

	// agentに送信要求
	if (a_->status() != UD_S_STOP) {
		a_->send();
	}

	// 次のexpireをschedule
	resched(a_->delay());

}

/*
 *  "UserDataApp" Class
 *            -- UserData アプリケーション
 */
/* コンストラクタ */
UserDataApp::UserDataApp() : timer_(this) { 

	// 変数の初期化
	target_ = (UserDataApp*) NULL;
	status_ = UD_S_NULL;
	interval_ = INTERVAL;
	packetID_ = 0;
	filein_ = (UserDataFile*) NULL;
	fileout_ = (UserDataFile*) NULL;
	filesorted_ = (UserDataFile*) NULL;
	logout_ = (UserDataLog*) NULL;
	cache_entrypoint = NULL;
	flushed_flag = 0;
#ifdef AODV_MULTIROUTE
	ragent_ = 0;
	multiroute_scheme_ = UD_UNIFORM;
	multiroute_rr_counter_ = 0;
#endif // AODV_MULTIROUTE

}
/* デストラクタ */
UserDataApp::~UserDataApp() {

	// XXX: 作っても呼ばれないようだ

}

/* Tclコマンドインタプリタ */
int UserDataApp::command(int argc, const char*const* argv) {

	/*
	 * Tcl側から UserDataApp に渡された引数はここで処理されます。
	 *
	 * コマンド一覧:
	 *  set userdata [new Application/UserData]
	 *    --- 新たな UserDataApp インスタンス $userdata を生成します。
	 *        (※ C++側でこの処理を担当するのはTcl Hooks)
	 *  $userdata attach-agent <agent object>
	 *    --- Agent に $userdata をアタッチします。
	 *  $userdata attach-file-in <UserDataFile>
	 *    --- $userdata に UserDataFileオブジェクト <UserDataFile> を入力先としてアタッチします。 
	 *  $userdata attach-file-out <UserDataFile>
	 *    --- $userdata に UserDataFileオブジェクト <UserDataFile> を出力先としてアタッチします。
	 *  $userdata attach-file-sorted <UserDataFile>
	 *    --- $userdata に UserDataFileオブジェクト <UserDataFile> を並替済出力先としてアタッチします。
	 *  $userdata attach-log <UserDataLog>
	 *    --- $userdata に UserDataLogオブジェクト <UserDataLog> をログ出力先としてアタッチします。
	 *  $userdata set-interval <interval>
	 *    --- 送信間隔を <interval> に変更します。変更しない場合は初期値INTERVALの値が使われます。
	 *  $userdata start
	 *    --- 送信を開始/再開します。
	 *  $userdata wait 
	 *    --- 送信を中断します。
	 *  $userdata stop
	 *    --- 送信を終了します。attach-file-sortedされている場合にはここで並べ替え済みデータの書き出しが行われます。
	 *
	 *  $userdata attach-ragent <routing agent object>
	 *    --- $userdata にルーティングエージェントをアタッチします (AODV_MULTIROUTE有効時のみ)
	 *  $userdata set-multiroute-scheme {round-robin|uniform|hop-weighted|clone}
	 *    --- 複数ルートへの割り当て方式を設定します。
	 *         round-robin  : 各経路を番号順に繰り替えして使用します。
	 *         uniform      : 各経路を等確率で選択して使用します。
	 *         hop-weighted : 短い経路に高い確率でパケットが割り当てられるようにして使用します。
	 *         clone        : 各経路に同一パケットをコピーして送信します。
	 *         short-only   : 経路の中で最も短いものだけを選択して送信します。
	 */

	Tcl& tcl = Tcl::instance();

	if (argc == 2) {	// 引数1個
		/* start: 送信開始 */
		if (strcmp(argv[1], "start") == 0) {
#ifdef UD_DEBUG
			printf("%s: start\n", __FUNCTION__);
#endif // UD_DEBUG
			if (status_ == UD_S_WAIT) {
				restart();
			} else {
				start();
			}
			return (TCL_OK);
		}
		/* wait: 送信中断 */
		else if (strcmp(argv[1], "wait") == 0) {
#ifdef UD_DEBUG
			printf("%s: wait\n", __FUNCTION__);
#endif // UD_DEBUG
			wait();
			return (TCL_OK);
		}
		/* stop: 送信終了 */
		else if (strcmp(argv[1], "stop") == 0) {
#ifdef UD_DEBUG
			printf("%s: stop\n", __FUNCTION__);
#endif // UD_DEBUG
			stop();
			return (TCL_OK);
		}
	} else if (argc == 3) {	// 引数2個
		/* attach-agent : エージェントにアタッチ */
		if (strcmp(argv[1], "attach-agent") == 0) {
			agent_ = (Agent*) TclObject::lookup(argv[2]);
			if (agent_ == 0) {
				// 指定されたエージェントが見つからない
				tcl.resultf("no such agent %s", argv[2]);
				return(TCL_ERROR);
			}
			agent_->attachApp(this);
#ifdef UD_DEBUG
			printf("%s: attach-agent %s: succeeded\n", __FUNCTION__, argv[2]);
#endif // UD_DEBUG
			return (TCL_OK);
		}
		/* set-interval : 送信間隔の設定*/
		else if (strcmp(argv[1], "set-interval") == 0) {
			interval_ = atof(argv[2]);
#ifdef UD_DEBUG
			printf("%s: set-interval %f\n", __FUNCTION__, interval_);
#endif // UD_DEBUG

			return (TCL_OK);
		}
#ifdef USE_AODV
		/* attach-ragent : ルーティングエージェントのアタッチ */
		else if (strcmp(argv[1], "attach-ragent") == 0) {
			ragent_ = (AODV*) TclObject::lookup(argv[2]);
			if (ragent_ == 0) {
				tcl.resultf("no such routing agent %s", argv[2]);
				return(TCL_ERROR);
			}
#ifdef UD_DEBUG
			printf("%s: attach-ragent %s: succeeded\n", __FUNCTION__, argv[2]);
#endif // UD_DEBUG
			return (TCL_OK);
		}
#else // USE_AODV
		else if (strcmp(argv[1], "attach-ragent") == 0) {
			tcl.resultf("%s: attach-ragent %s: ignoring (disabled by compiling options)\n", __FUNCTION__, argv[2]);
			return (TCL_OK); // 無視して処理を継続させる
		}
#endif // USE_AODV
		/* attach-file-in: 入力ファイルのアタッチ */
		else if (strcmp(argv[1], "attach-file-in") == 0) {
			filein_ = (UserDataFile*) TclObject::lookup(argv[2]);
			if (filein_ == 0) {
				// 指定されたUserDataFileオブジェクトが見つからない
				tcl.resultf("no such object %s", argv[2]);
				return(TCL_ERROR);
			}
			if (filein_->mode() != 1) {
				// ファイルがreadモードではない
				tcl.resultf("object %s is not read mode", argv[2]);
				return(TCL_ERROR);
			}
#ifdef UD_DEBUG
			printf("%s: attach-file-in %s: succeeded\n", __FUNCTION__, argv[2]);
#endif // UD_DEBUG
			return (TCL_OK);
		}
		/* attach-file-out: 出力ファイルのアタッチ */
		else if (strcmp(argv[1], "attach-file-out") == 0) {
			fileout_ = (UserDataFile*) TclObject::lookup(argv[2]);
			if (fileout_ == 0) {
				// 指定されたUserDataFileオブジェクトが見つからない
				tcl.resultf("no such object %s", argv[2]);
				return(TCL_ERROR);
			}
			if (fileout_->mode() != 2) {
				// ファイルがwriteモードではない
				tcl.resultf("object %s is not write mode", argv[2]);
				return(TCL_ERROR);
			}
#ifdef UD_DEBUG
			printf("%s: attach-file-out %s\n", __FUNCTION__, argv[2]);
#endif // UD_DEBUG
			return (TCL_OK);
		}
		/* attach-file-sorted: 並替済出力ファイルのアタッチ */
		else if (strcmp(argv[1], "attach-file-sorted") == 0) {
			filesorted_ = (UserDataFile*) TclObject::lookup(argv[2]);
			if (filesorted_ == 0) {
				// 指定されたUserDataFileオブジェクトが見つからない
				tcl.resultf("no such object %s", argv[2]);
				return(TCL_ERROR);
			}
			if (filesorted_->mode() != 2) {
				// ファイルがwriteモードではない
				tcl.resultf("object %s is not write mode", argv[2]);
				return(TCL_ERROR);
			}
#ifdef UD_DEBUG
			printf("%s: attach-file-sorted %s\n", __FUNCTION__, argv[2]);
#endif // UD_DEBUG
			return (TCL_OK);
		}
		/* attach-log: ログファイルのアタッチ */
		else if (strcmp(argv[1], "attach-log") == 0) {
			logout_ = (UserDataLog*) TclObject::lookup(argv[2]);
			if (logout_ == 0) {
				// 指定されたUserDataLogオブジェクトが見つからない
				tcl.resultf("no such object %s", argv[2]);
				return(TCL_ERROR);
			}
#ifdef UD_DEBUG
			printf("%s: attach-log %s\n", __FUNCTION__, argv[2]);
#endif // UD_DEBUG
			return (TCL_OK);
		}
#ifdef AODV_MULTIROUTE
		/* set-multiroute-scheme: 複数ルートへのパケット割り当て方式の設定 */
		else if (strcmp(argv[1], "set-multiroute-scheme") == 0) {
			if (strcmp(argv[2], "round-robin") == 0) {
				// ラウンドロビン
				multiroute_scheme_ = UD_ROUNDROBIN;
				return (TCL_OK);
			} else if (strcmp(argv[2], "uniform") == 0) {
				// 等確率
				multiroute_scheme_ = UD_UNIFORM;
				return (TCL_OK);
			} else if (strcmp(argv[2], "hop-weighted") == 0) {
				// 短ルート優先
				multiroute_scheme_ = UD_HOPWEIGHTED;
				return (TCL_OK);
			} else if (strcmp(argv[2], "clone") == 0) {
				// 同一パケットコピー
				multiroute_scheme_ = UD_CLONE;
				return (TCL_OK);
			} else if (strcmp(argv[2], "short-only") == 0) {
				// 最短ルートのみ
				multiroute_scheme_ = UD_SHORTONLY;
				return (TCL_OK);
			} else {
				// 方式名が無効
				tcl.resultf("invalid multiroute-scheme (%s)\n", argv[2]);
				return (TCL_ERROR);
			}
		}
#else // AODV_MULTIROUTE
		else if (strcmp(argv[1], "set-multiroute-scheme") == 0) {
			tcl.resultf("%s: set-multiroute-scheme %s: ignoring (disabled by compiling options)\n", __FUNCTION__, argv[2]);
			return (TCL_OK); // 無視して処理を継続させる
		}
#endif // AODV_MULTIROUTE

	}

	return(Application::command(argc, argv));

}

/* 送信処理 (Timerから呼び出される) */
void UserDataApp::send() {

	assert(status_ != UD_S_STOP);

	unsigned char *buf; int buf_len;
	UserData* data;
#ifdef AODV_MULTIROUTE
	int path_count, path_id, i, j, tmp, tmp2;
	RNG rng;
#endif // AODV_MULTIROUTE

#ifdef AODV_USERDATA_CONNECT
	/* フライング抑止 */
	if (ragent_->rt_request_status() == 0) {
		ragent_->force_rt_request(agent_->daddr(), this);
		wait();
		return;
	} else if (ragent_->rt_request_status() == 2) {
		if (status_ == UD_S_WAIT) {
			restart();
		}
	} else {
		return;
	}
#endif // AODV_USERDATA_CONNECT

	/* UserData自身の送信モードチェック */
	if (status_ != UD_S_SEND) {
		return;
	}

	/* パケットの生成 */
	// データの読み込み
	buf = new unsigned char[filein_->unitlen()];
	buf_len = filein_->get_next(buf);
	if (buf_len == -1 || buf_len == 0) {
		// 読み込み失敗(-1) or ファイル終了(0)
		stop();
		delete buf;
		return;
	}
	// AppDataオブジェクトの生成
	data = new UserData(buf_len);
	memcpy(data->data(), buf, buf_len);
#ifndef AODV_MULTIROUTE
	delete buf;
#else // AODV_MULTIROUTE
	if (multiroute_scheme_ != UD_CLONE) {
		// UD_CLONE時はbufを温存
		delete buf;
	}
#endif // AODV_MULTIROUTE
	packetID_++; data->packetID(packetID_);

#ifdef AODV_MULTIROUTE

	/* ルート割り当ての決定 */
	if (ragent_ != 0) {
		// ルート数の取得
		path_count = ragent_->rt_count(agent_->daddr());
		// ルート決定
		switch (multiroute_scheme_) {
		case UD_ROUNDROBIN:
			// ラウンドロビン
			if (multiroute_rr_counter_ >= path_count) {
				multiroute_rr_counter_ = 0;
			}
			path_id = multiroute_rr_counter_;
			multiroute_rr_counter_++;
			break;
		case UD_UNIFORM:
			// 等確率
			path_id = rng.uniform(path_count);
			break;
		case UD_HOPWEIGHTED:
			// 短ルート優先
			path_id = -1; tmp = 0;
			for (i=0; i<path_count; i++) {
				tmp += ragent_->rt_hops(agent_->daddr(), i);
			}
			// ルーレット選択
			tmp2 = rng.uniform(tmp) + 1;
			for (i=0; i<path_count; i++) {
				if (tmp2 <= ragent_->rt_hops(agent_->daddr(), i)) {
					path_id = i;
					break;
				} else {
					tmp2 -= ragent_->rt_hops(agent_->daddr(),i );
				}
			}
			if (path_id == -1) {
				fprintf(stderr, "UserDataApp::send(): UD_HOPWEIGHED failed\n");
				abort();
			}
			break;
		case UD_CLONE:
			// 同一パケットコピー
			// とりあえず今のデータについては最初のルートへ、他のルートは後で送信
			path_id = 0;
			break;
		case UD_SHORTONLY:
			// 最短ルートのみ
			path_id = 0;
			if (path_count == 1) {
				// ルートが1本だけならそれを使う
				break;
			} else {
				tmp = ragent_->rt_hops(agent_->daddr(), 0);
			}
			for (i=1; i<path_count; i++) {
				// 最短ルートを探す
				if (tmp > ragent_->rt_hops(agent_->daddr(), i)) {
					path_id = i; tmp = ragent_->rt_hops(agent_->daddr(), i);
				}
			}
			break;
		default:
			fprintf(stderr, "UserDataApp::send: invalid route selection scheme %d\n", multiroute_scheme_);
			abort();
			break;
		}
		// UserDataに割り当て情報を入れる
		data->path(path_id);
	} else {
		// ルーティングエージェントがアタッチされていない
		// 0を入れておく
		data->path(0);
	}

#else // AODV_MULTIROUTE

	// マルチルートOFF：0を入れておく
	data->path(0);

#endif // AODV_MULTIROUTE

#ifdef UD_DEBUG
	fprintf(stderr, "UserDataApp::send #%d, length %d, path %d at %f\n",
		data->packetID(), data->size(), data->path(), CURRENT_TIME);
#endif // UD_DEBUG

	/* 送信 */
	// ログに記録
	if (logout_ != 0) {
		logout_->write("s", CURRENT_TIME, data->packetID(), data->path(), data->size());
	}
	// 送信処理
	assert(agent_ != 0);
	agent_->sendmsg(data->size(), data);

#ifdef AODV_MULTIROUTE

	if (multiroute_scheme_ == UD_CLONE) {
		// UD_CLONE: 2番目以降のルートへの送信
		UserData *data[path_count];

		for (i=1; i<path_count; i++) {

			data[i] = new UserData(buf_len);
			memcpy(data[i]->data(), buf, buf_len);
			data[i]->packetID(packetID_);
			data[i]->path(i);
			agent_->sendmsg(data[i]->size(), data[i]);
		}

		delete buf;
	}

#endif // AODV_MULTIROUTE
	
}

/* 送信状態変更 */
void UserDataApp::start() { // 送信開始

	status_ = UD_S_SEND;

#ifdef UD_DEBUG
	fprintf(stderr, "UserDataApp: started.\n");
#endif // UD_DEBUG

	// Timerの開始
	timer_.sched(interval_);

}
void UserDataApp::wait() { // 送信中断

	status_ = UD_S_WAIT;

#ifdef UD_DEBUG
	fprintf(stderr, "UserDataApp: wait.\n");
#endif // UD_DEBUG

}
void UserDataApp::stop() { // 送信終了

	status_ = UD_S_STOP;

#ifdef UD_DEBUG
	fprintf(stderr, "UserDataApp: stopped.\n");
#endif // UD_DEBUG

	if (filesorted_ != 0) {
		// データ再構築キャッシュ書き出し
		cache_flush();
	}

}
void UserDataApp::restart() { // 送信再開

	status_ = UD_S_SEND;

#ifdef UD_DEBUG
	fprintf(stderr, "%s: UserDataApp::status_ is changed to %d\n", __FUNCTION__, status_);
#endif // UD_DEBUG

}

/* 受信データの処理 */
void UserDataApp::process_data(int size, AppData* appdata)
{
	UserData* data = (UserData*) appdata;

#ifdef UD_DEBUG
	fprintf(stderr, "UserDataApp::process_data #%d, length %d at %f\n",
		data->packetID(), data->size(), CURRENT_TIME);
#endif // UD_DEBUG

	// ログに記録
	if (logout_ != 0) {
		logout_->write("r", CURRENT_TIME, data->packetID(), data->path(), data->size());
	}

	// UserDataFile::write へデータを渡す
	fileout_->write(data->data(), data->size());

	// データ再構築キャッシュに挿入
	if (filesorted_ != 0) {
		cache_insert(data->data(), data->packetID(), data->size());
	}

}

/* 受信データ再構築キャッシュ：データ挿入 */
bool UserDataApp::cache_insert(unsigned char* data, int id, int size) {

	cache *current_node, *tmp;

	// TODO: リスト走査の高速化

	/*
	 * 連結リストの構造 
	 * struct cache {
	 * 	struct cache* previous;
	 * 	unsigned char* cached_data;
	 * 	int id;
	 * 	int size;
	 * 	struct cache* next;
	 * 	void init(struct cache* prev) {
	 * 		previous = prev;
	 * 		cached_data = NULL;
	 * 		id = 0;
	 * 		size = 0;
	 * 		next = NULL;
	 * 	}
	 *	void setdata(unsigned char* data, int data_id, int data_size) {
	 * 		cached_data = new unsigned char[data_size];
	 * 		memcpy(cached_data, data, data_size);
	 * 		id = data_id;
	 * 		size = data_size;
	 * 	}
	 * };
	 * cache* cache_entrypoint;
	 */

	// 最初のinsert
	if (cache_entrypoint == NULL) {
		cache_entrypoint = new cache;
		cache_entrypoint->init(NULL);
		cache_entrypoint->setdata(data, id, size);
		cache_fillsize = size;	// drop穴埋めのために記憶

		assert(cache_entrypoint != NULL);
		return true;
	}

	// 2回目以降
	current_node = cache_entrypoint;
	while(current_node != NULL) {
		if(current_node->id == id) {
			// 重複パケット
			return false;
		} else if(current_node->id < id && current_node->next == NULL) {
			// リスト末端まで到達 … 末尾に追加
			current_node->next = new cache;
			current_node->next->init(current_node);
			current_node->next->setdata(data, id, size);
#ifdef UD_DEBUG
			fprintf(stderr, "%s: insert #%d at the last of list, %x\n",
				__FUNCTION__, id, current_node->next);
#endif // UD_DEBUG
			return true;
		} else if(current_node->id > id) {
			// 追加するデータよりも新しいデータに到達 … 一つ前に追加
			tmp = new cache;
			tmp->init(current_node->previous);
			tmp->setdata(data, id, size);
			tmp->next = current_node;
			if(current_node->previous == NULL) { cache_entrypoint = tmp; }
			current_node->previous->next = tmp;
#ifdef UD_DEBUG
			fprintf(stderr, "%s: insert #%d in front of %d, %x\n",
				__FUNCTION__, id, current_node->id, tmp);
#endif // UD_DEBUG
			return true;
		}
		current_node = current_node->next;
	}

	// 追加失敗
	return false;

}
/* 受信データ再構築キャッシュ：データ書き出し */
void UserDataApp::cache_flush() {

	if (flushed_flag == 1) {
		// XXX: 多数回のflushに正しく対応できていないのでエラー扱いにしておく
		fprintf(stderr, "%s: Sorry, you can't flush the cache twice.\n", __FUNCTION__);
	}

	cache *current_node, *prev_node;
	unsigned char *fill;
	int prev_id;
	int i;

	prev_id = 0;
	current_node = cache_entrypoint;
	while(current_node != NULL) {

#ifdef UD_DEBUG
		fprintf(stderr, "%s: flushing cache #%d\n",
			__FUNCTION__, prev_id + 1);
#endif // UD_DEBUG

		if (current_node->id > prev_id + 1) {
			// データ抜けの取扱い
#ifdef UD_DEBUG
			fprintf(stderr, "%s: #%d not found\n",
				__FUNCTION__, prev_id + 1, current_node->id);
#endif // UD_DEBUG
			// XXX: 0で穴埋め … 要再検討
			// XXX: データ長は最初に到着したパケットと同じにしておく
			fill = new unsigned char[cache_fillsize];
			for (i=0; i<cache_fillsize; i++) { fill[i] = 0; }
			filesorted_->write(fill, cache_fillsize);
			delete [] fill;
			prev_id++; continue;
		}

		// 書き込み処理
		filesorted_->write(current_node->cached_data, current_node->size);

		// ノードの削除処理
		prev_id = current_node->id;
		prev_node = current_node;
		current_node = current_node->next;
		if (current_node != NULL) { current_node->previous = NULL; }
		cache_entrypoint = current_node;
		delete [] prev_node->cached_data;
		delete prev_node;
	}

	flushed_flag = 1;

}

/* パケット受信 (未使用) */
void UserDataApp::recv(Packet* pkt, Handler*)
{
	/*
	 *  UserDataAppはApplicationの子クラスなのでrecvが必要。
	 */

	fprintf(stderr, "UserDataApp::recv shouldn't be called\n");
	abort(); // このメンバ関数は使用しない:

}

#ifdef AODV_USERDATA_CONNECT
void UserDataApp::aodv_callback()
{
	if (status_ == UD_S_WAIT) {
		restart();
	}
}
#endif // AODV_USERDATA_CONNECT
