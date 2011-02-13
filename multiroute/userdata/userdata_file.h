/* vim: set fenc=utf-8 ff=unix */

/*
 * Application/UserData : ユーザ定義データの送受信
 *
 * Wada Laboratory, Shizuoka University
 * Takahiro Sugimoto	<keyaki.no.kokage@gmail.com>
 *
 * Last Modified: 2008/11/30 21:32:52
 */

#ifndef ns_userdata_file_h
#define ns_userdata_file_h

#include "object.h"

// 一回に読む量の初期値 … set-unitlen で変更可能。Agent側のパケットサイズ上限に注意。
#define UNITLEN 512

/* ファイル処理クラス */
class UserDataFile : public NsObject {
 public:
	// コンストラクタ / デストラクタ
	UserDataFile();
	~UserDataFile();
	// 入出力インターフェース
	int get_next(unsigned char* data);
	void write(unsigned char* data, int datalen_);
	int mode() {
		return mode_;
	}
	int unitlen() {
		return unitlen_;
	}
 protected:
	// Tclインタプリタ
	virtual int command(int argc, const char*const* argv);
	// パケット受信 (NsObjectの子クラスなので必要、実際には使用しない)
	void recv(Packet*, Handler*);
 private:
	// ファイル名
	char *file_;
	// ファイルポインタ
	FILE *fp_;
	// ファイルの状態
	int mode_;		// ファイルの読み書きモード 0:Null 1:Read 2:Write
	int unitlen_;		// 一回の読み書きで処理する長さ
	int infile_pos_;	// Readモード用カウンタ
};

/* ログファイルクラス */
class UserDataLog : public NsObject {
 public:
	// コンストラクタ / デストラクタ
	UserDataLog();
	~UserDataLog();
	// 入力インターフェース
	void write(const char* action, double time, int packetid, int path, int size);
 protected:
	// Tclインタプリタ
	virtual int command(int argc, const char*const* argv);
	// パケット受信 (NsObjectの子クラスなので必要、実際には使用しない)
	void recv(Packet*, Handler*);
 private:
	// ファイル名
	char *file_;
	// ファイルポインタ
	FILE *fp_;
};

#endif // ns_userdata_file_h
