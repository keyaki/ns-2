/* vim: set fenc=utf-8 ff=unix */

/*
 * Application/UserData : ユーザ定義データの送受信
 *
 * Wada Laboratory, Shizuoka University
 * Takahiro Sugimoto	<keyaki.no.kokage@gmail.com>
 *
 * Last Modified: 2008/12/08 07:56:08
 */

#include <string.h>
#include "userdata_file.h"

/* Tcl Hooks */
static class UserDataFileClass : public TclClass {
 public:
	UserDataFileClass() : TclClass("UserDataFile") {}
	TclObject* create(int, const char* const*) {
		return (new UserDataFile);
	}
} class_userdatafile;
static class UserDataLogClass : public TclClass {
 public:
	UserDataLogClass() : TclClass("UserDataLog") {}
	TclObject* create(int, const char* const*) {
		return (new UserDataLog);
	}
} class_userdatalog;

/*
 *  "UserDataFile" Class
 *              -- UserData 用ファイルクラス
 */
/* コンストラクタ */
UserDataFile::UserDataFile() {

	// 変数の初期化
	mode_ = 0;
	unitlen_ = UNITLEN; 
	infile_pos_ = 0;
	file_ = (char*) NULL;
	fp_ = (FILE*) NULL;

}
/* デストラクタ */
UserDataFile::~UserDataFile() {

	// XXX: 呼ばれていない？
	
	if (fp_ != NULL) {
		// ファイルがopenされていたらここで閉じる
		fclose(fp_);
	}

}

/* Tclコマンドインタプリタ */
int UserDataFile::command(int argc, const char*const* argv) {

	/*
	 * Tcl側から UserDataFile に渡された引数はここで処理されます。
	 *
	 * コマンド一覧:
	 *  set userfile [new UserDataFile]
	 *    --- 新たな UserDataFile インスタンス $userfile を生成します。
	 *        (※ C++側でこの処理を担当するのはTcl Hooks)
	 *  $userfile setfile <filename> <r/w>
	 *    --- ファイルと読み書きモードを設定します。
	 *  $userdata set-unitlen <length>
	 *    --- 一回に読むデータの長さを指定します。
	 *
	 */

	Tcl& tcl = Tcl::instance();

	if (argc == 3) { // 引数2個
		/* set-unitlen: 一回に読むデータの長さを指定 */
		if (strcmp(argv[1], "set-unitlen") == 0) {
			unitlen_ = atoi(argv[2]);
#ifdef UD_DEBUG
			printf("%s: set-unitlen %f\n", __FUNCTION__, unitlen_);
#endif // UD_DEBUG
			return (TCL_OK);
		}
	} else if (argc == 4) { // 引数3個
		/* setfile: ファイルの指定 */
		if (strcmp(argv[1], "setfile") == 0) {
			file_ = new char[strlen(argv[2])+1];
			strcpy(file_, argv[2]);
			if (strcmp(argv[3], "r") == 0) {
				mode_ = 1;
			} else if (strcmp(argv[3], "w") == 0) {
				mode_ = 2;
			} else {
				tcl.resultf("invalid file mode %s for %s", argv[3], argv[2]);
				return (TCL_ERROR);
			}
			return (TCL_OK);
		}
	}

	return(NsObject::command(argc, argv));

}

/* 次のメッセージを取得 */
int UserDataFile::get_next(unsigned char* data)
{

	int i, length = 0;
	unsigned char* pos = data;

	// 前処理
	if (mode_ != 1) {
		// ファイルがreadモードではない
		fprintf(stderr, "%s: %s is not readable (mode %d).\n", __FUNCTION__, file_, mode_);
		return -1;
	}
	if (fp_ == NULL) {
		// まだファイルがopenされていないので開く
		if ((fp_ = fopen(file_, "rb")) == NULL) {
			// openに失敗
			fprintf(stderr, "%s: failed to open %s\n", __FUNCTION__, file_);
			return -1;
		}
	}

	// 読み込み処理
	for (i=0; i<unitlen_; i++) {
		if (fread(pos, sizeof(unsigned char), 1, fp_) >= 1) {
			length++; pos++;
		} else {
			// ファイル終端またはエラー
			break;
		}
	}

	return length;

}

/* 受信したメッセージを書き込む */
void UserDataFile::write(unsigned char* data, int datalen_)
{

	// 前処理
	if (mode_ != 2) {
		// ファイルがwriteモードではない
		fprintf(stderr, "%s: %s is not writeable (mode %d).\n", __FUNCTION__, file_, mode_);
		abort();
	}
	if (fp_ == NULL) {
		// まだファイルがopenされていないので開く
		if ((fp_ = fopen(file_, "wb")) == NULL) {
			// openに失敗
			fprintf(stderr, "%s: failed to open %s\n", __FUNCTION__, file_);
			abort();
		}
	}

	// 書き込み処理
	fwrite(data, sizeof(unsigned char), datalen_, fp_);

}

/* パケット受信 (未使用) */
void UserDataFile::recv(Packet*, Handler*)
{
	/*
	 *  UserDataFileはNsObjectの子クラスなのでrecvが必要。
	 */

	fprintf(stderr, "UserDataFile::recv shouldn't be called\n");
        abort(); // このメンバ関数は使わない
}

/*
 *  "UserDataLog" Class
 *              -- UserData 用ログファイルクラス
 */
/* コンストラクタ */
UserDataLog::UserDataLog() {

	// 変数の初期化
	file_ = (char*) NULL;
	fp_ = (FILE*) NULL;

}
/* デストラクタ */
UserDataLog::~UserDataLog() {

	if (fp_ != NULL) {
		// ファイルがopenされていたらここで閉じる
		fclose(fp_);
	}
}

/* Tclコマンドインタプリタ */
int UserDataLog::command(int argc, const char*const* argv) {

	/*
	 * Tcl側から UserDataLog に渡された引数はここで処理されます。
	 *
	 * コマンド一覧:
	 *  set userlog [new UserDataLog]
	 *    --- 新たな UserDataLog インスタンス $userfile を生成します。
	 *  $userfile setfile <filename>
	 *    --- ファイルを指定します。
	 *
	 */

	if (argc == 3) { // 引数2個
		/* setfile: ファイルの指定 */
		if (strcmp(argv[1], "setfile") == 0) {
			file_ = new char[strlen(argv[2])+1];
			strcpy(file_, argv[2]);
			return (TCL_OK);
		}
	}

	return(NsObject::command(argc, argv));

}

/* ログを書き込む */
void UserDataLog::write(const char* action, double time, int packetid, int path, int size)
{

	// 前処理
	if (fp_ == NULL) {
		// まだファイルがopenされていないので開く
		if ((fp_ = fopen(file_, "w")) == NULL) {
			// openに失敗
			fprintf(stderr, "%s: failed to open %s\n", __FUNCTION__, file_);
			abort();
		}
	}

	// 書き込むデータの整形
	char buf[100];
	sprintf(buf, "%s %f %d %d %d\n", action, time, packetid, path, size);

	// 書き込み処理
	fputs(buf, fp_);

}

/* パケット受信 (未使用) */
void UserDataLog::recv(Packet*, Handler*)
{
	/*
	 *  UserDataLogはNsObjectの子クラスなのでrecvが必要。
	 */

	fprintf(stderr, "UserDataLog::recv shouldn't be called\n");
        abort(); // このメンバ関数は使わない
}

