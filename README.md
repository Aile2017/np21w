# PC-9801 SERIES EMULATOR Neko Project 21/W

https://simk98.github.io/np21w/ で公開されているソースコードを、
リリースごとにコミットとして時系列に並べたリポジトリです。

This repository collects the source code published at
https://simk98.github.io/np21w/ , one commit per released version, in
chronological order.

## 内容

- 各コミットは、公開されている各版のアーカイブに含まれるソースをそのまま取り出したものです。
  バイナリ(実行ファイル)は含みません。
- コミットメッセージのタイトルと箇条書きは、上記サイトの
  [更新履歴](https://simk98.github.io/np21w/version.html) に基づいています。
  更新履歴に載っていない版(beta など)は、アーカイブ内のソースの更新日時をコミット日時としています。
- 各コミットの末尾に、取り出し元のアーカイブ名を記録しています。
- ソースの文字コードや改行コードは、アーカイブのまま変更していません。

## ブランチ

| ブランチ | 内容 |
|---|---|
| `main` | 0.85 から最新版までのメイン系列。 |
| `bsd` | `-bsd` 版(rev97 以降)。 |
| `haxm` | HAXM 版(rev57〜rev68)。 |
| `variant/*` | 試験版・派生版。対応する rev の `main` のコミットから分岐しています。 |
| `local-patches` | 公開元のソースではなく、rev107β6 のソースに個人用のパッチを当てたもの。下記を参照。 |

## 注意

- 非公式のまとめです。ソースの著作権およびライセンスは、各ソースに含まれる
  表記(例: `LICENSES` ディレクトリや各ファイルのヘッダー)に従います。
- ソースを含まないアーカイブ(テスト用バイナリのみの版など)は取り込んでいません。
- 取り込めなかったアーカイブがあります。正確な内容は、公開元のアーカイブを参照してください。

## `local-patches` ブランチについて

公開元のソースではありません。rev107β6 のソースのコミットから分岐し、
次の 4 つのコミットを重ねた、個人用のパッチ集です(公開元とは無関係です)。

1. **Build with Visual Studio 2026**(`build-vs2026`)
   Visual Studio 2026 でビルドできるようにします。プロジェクトの `PlatformToolset` を `v145` にし、
   リソースファイル(`.rc`)の `afxres.h` を `winres.h` に置き換え、`wab/npdisp_d3d.cpp` に BOM を付けます。
2. **Treat AX keyboard (type 7) like 101 when auto-detecting**(`key101`)
   キーボードの種類を自動判定するとき、AX キーボード(`GetKeyboardType(0) == 7`)を
   101 キーボードと同様に扱います(`win9x/np2.cpp`、`win9x/winkbd.cpp`)。
3. **Emulate the 386/486 prefetch queue**(`prefetch`)
   386/486 の命令プリフェッチキュー(16 バイト)をエミュレートします
   (`i386c/ia32/cpu.c`、`cpu_mem.c`、`interface.h`)。CPU ファミリが 4 以下で、
   ページングが無効のときだけ働きます。プリフェッチ済みのコードを書き換えて動作を判定する
   アンチデバッグ(Wizardry 7 PC-98 の `DS.EXE` など)が、再起動を繰り返す問題を避けるためのものです。
4. **Disable SUPPORT_NPDISP_W2K in the Visual Studio projects**
   上記 3 つとは別に、Visual Studio のプロジェクトから `SUPPORT_NPDISP_W2K`(Windows 2000 用 NPDISP)を
   外します。rev107β3・β4 のプロジェクトでは無効でしたが、β5 以降は有効です。
   DOS 専用の用途では不要で、有効のままだと、`prefetch` との組み合わせで動作が不安定になる場合が
   ありました。

新しい版には、その版のコミットから分岐したブランチに、`git cherry-pick` でこれらのコミットを当て直して使います。

---

This is an unofficial collection. Copyright and licensing follow the notices
included in the sources themselves (for example the `LICENSES` directory and
the headers of each file). Binaries are not included, and archives without
source code were skipped.
