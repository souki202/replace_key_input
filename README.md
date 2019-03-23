# 自分用入力キー置換ソフト
キーボードから入力したキーを, 別のキーに置換します.

# 使い方
1. Interceptionを導入
   * http://www.oblita.com/interception.html
   * command line installerディレクトリにあるファイルをコマンドラインから実行して再起動
2. 設定ファイルに置換したいHID, 実行ファイル名, キーの組み合わせを記述
3. 実行

# 設定
iniもどきを使用しています.
本当はyamlのほうがいいと思いますが, そこは手抜きです.

コード一覧はとりあえずここ
* http://faq.creasus.net/04/0131/CharCode.html

このページのファンクションキーと制御キー以外は, Shift押していない方のそのままの文字でいけます.\
ただ, \ だけはキーコードか, Shiftを押した場合の文字でお願いします.\
違うキーが反応してうまく動かない場合は, numlockの状態を確認の後, scancode_scannerを使用してください. 
修飾キー等は下を参照

## キー別名一覧

* 左シフト lshift
* 右シフト rshift
* Alt lalt
* Ctrl lctrl
* スペースキー space
* エンターキー enter
* 変換キー convert
* 無変換キー noconvert
* バックスペースキー bs
* Fキー F1 F2 ... F12
* 全半キー zenhan
* タブ文字 tab
* capslock capslock
* 左Windows lwin
* 右Windows rwin
* ひら/カナ hirakana
* 方向キー上 up
* 方向キー下 down
* 方向キー右 right
* 方向キー左 left
* Esc esc

## 入力コード指定
codexx とすると, 入力コードを直接指定できます.

code10 = a\
code2 = code111

のような感じです.

# その他
あくまで自分用で, コンソールの画面は残ります.
多分消えることはないです.