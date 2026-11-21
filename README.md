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

## 注意

- 非公式のまとめです。ソースの著作権およびライセンスは、各ソースに含まれる
  表記(例: `LICENSES` ディレクトリや各ファイルのヘッダー)に従います。
- ソースを含まないアーカイブ(テスト用バイナリのみの版など)は取り込んでいません。
- 取り込めなかったアーカイブがあります。正確な内容は、公開元のアーカイブを参照してください。

## 個人用パッチについて

このリポジトリは、公開元のソースだけを保持します。ビルド用の個人パッチは、別のリポジトリ
https://github.com/Aile2017/np21w-local で管理しています(Visual Studio 2026 でのビルド、
101 キーボード(US 配列)への対応、386/486 のプリフェッチキューのエミュレーションなど。公開元とは無関係です)。
`np21w-local` は、このリポジトリの特定のコミットにパッチを当ててビルドします。
以前この README で説明していた `local-patches` ブランチは、削除しました。

---

This is an unofficial collection. Copyright and licensing follow the notices
included in the sources themselves (for example the `LICENSES` directory and
the headers of each file). Binaries are not included, and archives without
source code were skipped.
